"""Initial C6 commissioning safety and firmware record interoperability."""

import importlib.util
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
import warnings
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("provision_c6", ROOT / "tools/firmware/provision-c6.py")
PROVISION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROVISION)


class ProvisioningTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("esptool"), "Requires the initialized workspace's esptool")
    def test_esptool_does_not_retry_failed_native_flash_transfers(self):
        probe = r"""
import esptool
from esptool import loader
from esptool.util import FatalError
from unittest.mock import Mock
def check():
    for method in ("flash_block", "flash_defl_block"):
        esp = loader.ESPLoader.__new__(loader.ESPLoader)
        esp.IS_STUB = True
        esp.check_command = Mock(side_effect=FatalError("uncertain write"))
        try:
            getattr(esp, method)(bytes(256), 0)
        except FatalError:
            pass
        else:
            raise AssertionError("Failed write was reported as success")
        assert esp.check_command.call_count == 1
        assert esp.WRITE_FLASH_ATTEMPTS == 1
esptool.main = check
"""
        subprocess.run([sys.executable, "-c", probe + PROVISION.ESPTOOL_ENTRY], check=True, capture_output=True)

    def test_key_record_is_accepted_by_the_actual_firmware_decoder(self):
        source = r"""
#include "platform/boot_config.h"
#include "platform/boot_flash.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
int boot_flash_read(uint8_t *record, size_t size) {
    return fread(record, 1, size, stdin) == size ? 0 : -EIO;
}
int boot_flash_write(const uint8_t *record, size_t size) {
    (void)record; (void)size; return -ENOTSUP;
}
int main(void) {
    uint8_t key[ZF_BLE_KEY_SIZE];
    zf_boot_config_load();
    assert(zf_boot_config_source() == ZF_BOOT_CONFIG_SAVED);
    assert(zf_boot_config_ble_key_configured());
    assert(zf_boot_config_sysclk() == ZF_SYSCLK_DEFAULT);
    assert(zf_boot_config_radios() == (ZF_WIRELESS_WIFI | ZF_WIRELESS_BLE));
    zf_boot_config_ble_key(key);
    for (unsigned i = 0; i < sizeof(key); ++i) assert(key[i] == i);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "decode.c"
            binary = Path(directory) / "decode"
            fixture.write_text(source)
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "firmware/src"), str(fixture),
                str(ROOT / "firmware/src/platform/boot_config.c"), "-o", str(binary),
            ], check=True, capture_output=True)
            subprocess.run([str(binary)], input=PROVISION.settings_page(bytes(range(32))), check=True)

    def test_invalid_and_blank_keys_are_rejected_without_echoing_them(self):
        for key in (b"", b"a" * 31, bytes(32), b"a" * 33):
            with self.subTest(length=len(key)), self.assertRaises(ValueError):
                PROVISION.settings_page(key)

    def test_compiled_layout_never_places_settings_inside_the_firmware_image(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "zephyr").mkdir()
            (build / "zephyr/.config").write_text(
                'CONFIG_BOARD_TARGET="esp32c6_devkitc/esp32c6/hpcore"\nCONFIG_SOC_ESP32C6=y\n'
                'CONFIG_FLASH_SIZE=8388608\nCONFIG_ESP_SIMPLE_BOOT=y\nCONFIG_FLASH_LOAD_OFFSET=0x0\n')
            (build / "CMakeCache.txt").write_text(f"ZEPHYR_BASE:PATH={build}\n")
            parser = build / "scripts/dts/python-devicetree/src/devicetree/dtlib.py"
            parser.parent.mkdir(parents=True)
            parser.touch()
            flash = Mock(props={"reg": Mock(to_nums=lambda: [0, 8388608])})
            partition = Mock(props={"reg": Mock(to_nums=lambda: [4096, 4096])})
            partition.parent.parent = flash
            tree = SimpleNamespace(label2node={"flash0": flash, "storage_partition": partition})
            dtlib = SimpleNamespace(DT=lambda path: tree, DTError=ValueError)
            with patch.dict("sys.modules", {"devicetree.dtlib": dtlib}):
                (build / "zephyr/zephyr.bin").write_bytes(b"\xe9" + bytes(4095))
                self.assertEqual(PROVISION.settings_offset(build), 4096)
                (build / "zephyr/zephyr.bin").write_bytes(b"\xe9" + bytes(4096))
                with self.assertRaisesRegex(ValueError, "overlaps"):
                    PROVISION.settings_offset(build)

    def run_provision(self, *, initial=None, write_failure=False, write_timeout=False, mismatch=False,
                      flash_size="8MB", expected_writes=1):
        page = PROVISION.settings_page(bytes(range(32)))
        operations = []
        paths = []

        def esptool(command, **kwargs):
            operation = next(value for value in command if value in ("flash-id", "read-flash", "write-flash"))
            operations.append(operation)
            self.assertEqual(command[command.index("--chip") + 1], "esp32c6")
            self.assertEqual(command[command.index("--port") + 1], "/dev/explicit-fixture")
            self.assertEqual(command[command.index("--connect-attempts") + 1], "1")
            self.assertEqual(command[command.index("--after") + 1], "no-reset")
            self.assertNotIn("--erase-all", command)
            self.assertEqual(kwargs["env"]["ESPTOOL_OPEN_PORT_ATTEMPTS"], "1")
            if operation == "flash-id":
                return subprocess.CompletedProcess(command, 0, stdout=f"Detected flash size: {flash_size}\n".encode())
            path = Path(command[-1])
            paths.append(path)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            if operation == "read-flash":
                if len(operations) == 2:
                    data = initial if initial is not None else b"\xff" * PROVISION.SECTOR_SIZE
                else:
                    data = bytes(PROVISION.SECTOR_SIZE) if mismatch else page.ljust(PROVISION.SECTOR_SIZE, b"\xff")
                path.write_bytes(data)
            else:
                self.assertEqual(path.read_bytes(), page)
                if write_timeout:
                    raise subprocess.TimeoutExpired(command, 60)
            return subprocess.CompletedProcess(
                command, 1 if write_failure and operation == "write-flash" else 0, stdout=b"")

        with patch.object(PROVISION.subprocess, "run", side_effect=esptool):
            try:
                PROVISION.provision("/dev/explicit-fixture", 0x7B0000, page)
            finally:
                self.assertEqual(operations.count("write-flash"), expected_writes)
                self.assertTrue(all(not path.exists() for path in paths))
        return operations

    def test_provisioning_writes_only_once_and_verifies_private_temporary_data(self):
        self.assertEqual(self.run_provision(), ["flash-id", "read-flash", "write-flash", "read-flash"])

    def test_unknown_or_wrong_physical_flash_size_prevents_provisioning(self):
        for size in ("4MB", "Unknown"):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "refusing to write"):
                self.run_provision(flash_size=size, expected_writes=0)

    def test_existing_or_short_settings_read_is_never_overwritten(self):
        for data in (bytes(PROVISION.SECTOR_SIZE), b"\xff" * 100):
            with self.subTest(size=len(data)), self.assertRaisesRegex(ValueError, "not erased"):
                self.run_provision(initial=data, expected_writes=0)

    def test_failed_write_is_not_replayed_or_reported_as_success(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), self.assertRaisesRegex(RuntimeError, "write-flash (failed|timed out)"):
                self.run_provision(write_failure=not timeout, write_timeout=timeout)

    def test_readback_mismatch_is_an_explicit_failure(self):
        with self.assertRaisesRegex(RuntimeError, "readback did not match"):
            self.run_provision(mismatch=True)

    def test_dry_run_never_reads_a_key_or_accesses_a_device(self):
        arguments = ["provision-c6.py", "--build-dir", "fixture", "--port", "/dev/explicit", "--dry-run"]
        with patch.object(PROVISION.sys, "argv", arguments), \
                patch.object(PROVISION.importlib.util, "find_spec", return_value=object()), \
                patch.object(PROVISION, "settings_offset", return_value=0x7B0000), \
                patch.object(PROVISION.getpass, "getpass") as prompt, \
                patch.object(PROVISION.subprocess, "run") as device, contextlib.redirect_stdout(io.StringIO()):
            PROVISION.main()
            prompt.assert_not_called()
            device.assert_not_called()

    def test_write_requires_explicit_authorization_before_key_entry(self):
        arguments = ["provision-c6.py", "--build-dir", "fixture", "--port", "/dev/explicit"]
        with patch.object(PROVISION.sys, "argv", arguments), \
                patch.object(PROVISION.importlib.util, "find_spec", return_value=object()), \
                patch.object(PROVISION, "settings_offset", return_value=0x7B0000), \
                patch.object(PROVISION.getpass, "getpass") as prompt, \
                patch.object(PROVISION.subprocess, "run") as device, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                PROVISION.main()
            self.assertEqual(failure.exception.code, 1)
            prompt.assert_not_called()
            device.assert_not_called()

    def test_echoed_key_entry_fallback_is_rejected_before_device_access(self):
        arguments = ["provision-c6.py", "--build-dir", "fixture", "--port", "/dev/explicit", "--allow-settings-write"]

        def fallback_prompt(*args):
            warnings.warn("Password input may be echoed", PROVISION.getpass.GetPassWarning)
            self.fail("Echoed key entry must never be reached")

        with patch.object(PROVISION.sys, "argv", arguments), \
                patch.object(PROVISION.importlib.util, "find_spec", return_value=object()), \
                patch.object(PROVISION, "settings_offset", return_value=0x7B0000), \
                patch.object(PROVISION.sys.stdin, "isatty", return_value=True), \
                patch.object(PROVISION.getpass, "getpass", side_effect=fallback_prompt), \
                patch.object(PROVISION.subprocess, "run") as device, contextlib.redirect_stderr(io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as failure:
                PROVISION.main()
            self.assertEqual(failure.exception.code, 1)
            self.assertIn("refusing an echoed input fallback", errors.getvalue())
            device.assert_not_called()


if __name__ == "__main__":
    unittest.main()
