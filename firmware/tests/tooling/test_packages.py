"""Firmware package radio notices and provenance, without hardware or downloads."""

import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class FirmwarePackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        self.repo = self.workspace / "zephyrforge"
        self.build = self.workspace / "firmware-build"
        self.write("zephyrforge/LICENSE", "project license")
        self.write("zephyrforge/docs/README.md", "package documentation")
        self.write("zephyrforge/firmware/src/app/zephyrforge.h", '#define ZEPHYRFORGE_VERSION "0.1.0"\n')
        self.write("bin/git", "#!/bin/sh\nprintf '%s\\n' fixture-commit\n")
        (self.workspace / "bin/git").chmod(0o755)
        for component in ("zephyr", "modules/hal/rpi_pico", "modules/hal/cmsis_6"):
            self.write(f"{component}/LICENSE", f"{component} license")
        self.write("modules/fs/fatfs/ff.c", "/* FatFs license */\n")
        for name in ("zephyr.uf2", "zephyr.elf", "zephyr.dts", "runners.yaml"):
            self.write(f"firmware-build/zephyr/{name}", "fixture")
        self.write("firmware-build/west-manifest.yml", "manifest: fixture\n")

    def write(self, path, content):
        target = self.workspace / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)

    def package(self, board, target):
        self.write("firmware-build/zephyr/.config", f'CONFIG_BOARD_TARGET="{target}"\n')
        env = {**os.environ, "BOARD": board, "STORAGE": "sdmmc", "FIRMWARE_BUILD": str(self.build),
               "PATH": f"{self.workspace / 'bin'}:{os.environ['PATH']}"}
        result = subprocess.run(["bash", str(ROOT / "tools/release/package-firmware.sh")],
                                cwd=self.workspace, env=env, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return self.repo / "build/firmware-package"

    def test_w_package_includes_infineon_blob_license_and_embedded_blob_hashes(self):
        self.write("modules/hal/infineon/LICENSE", "Infineon HAL license")
        self.write("modules/hal/infineon/zephyr/blobs/license.txt", "radio firmware license")
        blobs = ("firmware/COMPONENT_43439/43439A0.bin",
                 "clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob")
        for blob in blobs:
            self.write(f"modules/hal/infineon/zephyr/blobs/img/whd/resources/{blob}", blob)
        package = self.package("pico2_w", "rpi_pico2/rp2350a/m33/w")
        self.assertEqual((package / "licenses/modules/hal/infineon/zephyr/blobs/license.txt").read_text(),
                         "radio firmware license")
        info = (package / "BUILD-INFO.txt").read_text()
        self.assertIn("Wi-Fi only; Bluetooth transport is unsupported", info)
        for blob in blobs:
            self.assertIn(hashlib.sha256(blob.encode()).hexdigest(), info)

    def test_non_w_package_does_not_require_radio_dependencies(self):
        package = self.package("pico", "rpi_pico/rp2040")
        self.assertTrue((package / "zephyr.uf2").is_file())
        self.assertFalse((package / "licenses/modules/hal/infineon").exists())
        self.assertNotIn("CYW43439", (package / "BUILD-INFO.txt").read_text())


if __name__ == "__main__":
    unittest.main()
