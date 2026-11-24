"""Firmware wrapper checks with mocked downloads, installers and device access."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[3] / "tools/firmware"
CHECKER_SPEC = importlib.util.spec_from_file_location(
    "check_build_cmake_args", TOOLS / "check-build-cmake-args.py")
CHECKER = importlib.util.module_from_spec(CHECKER_SPEC)
CHECKER_SPEC.loader.exec_module(CHECKER)
MOCK = r"""#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$MOCK_LOG"
case "$*" in
    "-")
        printf '%s\n' "${MOCK_SERIAL_PORT:-}" ;;
    *check-build-cmake-args.py*)
        if [[ -n "${MOCK_WEST_CMAKE_ARGS:-}" ]]; then
            case "$MOCK_WEST_CMAKE_ARGS" in
                *ZEPHYR_TOOLCHAIN_VARIANT*|*ZEPHYR_SDK_INSTALL_DIR*|*Zephyr-sdk_DIR*|\
                *CROSS_COMPILE*|*CMAKE_TOOLCHAIN_FILE*|*CMAKE_PROJECT_*|*ZEPHYRFORGE_VERSION*|\
                *ZF_ENABLE_STORAGE*|*CONF_FILE*|*DTC_OVERLAY_FILE*|*-UBOARD*|*-U\**|\
                *--toolchain*|*"-C "*)
                    printf 'Error: west build.cmake-args must not override ZEPHYR_TOOLCHAIN_VARIANT.\n' >&2
                    exit 1 ;;
            esac
        fi ;;
    "-c "*)
        if [[ "$*" == *"configparser"* ]]; then
            awk -v key="$3" '
                function lower(value) { return tolower(value) }
                lower($0) ~ /^[[:space:]]*\[manifest\][[:space:]]*$/ { in_manifest=1; next }
                /^[[:space:]]*\[/ { in_manifest=0 }
                in_manifest && lower($0) ~ "^[[:space:]]*" lower(key) "[[:space:]]*=" {
                    sub(/^[^=]*=[[:space:]]*/, "")
                    sub(/[[:space:]]+$/, "")
                    print
                    exit
                }' "$4"
        else
            exit "${MOCK_PYTHON_STATUS:-0}"
        fi ;;
    "-m venv "*)
        mkdir -p "$3/bin"
        touch "$3/pyvenv.cfg"
        cp "$0" "$3/bin/python"
        printf 'export PATH=%q:"$PATH"\n' "$3/bin" > "$3/bin/activate" ;;
    "-m pip install "*)
        exit "${MOCK_PIP_STATUS:-0}" ;;
    "-m west init "*)
        mkdir -p .west
        printf '[manifest]\npath = %s\nfile = west.yml\n' "$(basename "$MOCK_ROOT")" > .west/config ;;
    "-m west config --local manifest.path")
        sed -n 's/^path = //p' .west/config ;;
    "-m west config --local manifest.path "*)
        sed -i "s|^path = .*|path = $6|" .west/config ;;
    "-m west update "*)
        manifest=$(sed -n 's/^path = //p' .west/config)
        [[ -f "$manifest/west.yml" ]] || { printf 'Missing west manifest\n' >&2; exit 17; }
        mkdir -p zephyr/scripts modules/hal/espressif/zephyr
        touch zephyr/scripts/requirements-base.txt modules/hal/espressif/zephyr/requirements.txt ;;
    "-m west list zephyr "*)
        printf '%s/zephyr\n' "$(dirname "$MOCK_ROOT")" ;;
    "-m west list hal_espressif "*)
        printf '%s/modules/hal/espressif\n' "$PWD" ;;
    "-m west blobs fetch "*)
        if [[ "$*" == *"hal_infineon"* ]]; then
            resources=modules/hal/infineon/zephyr/blobs/img/whd/resources
            mkdir -p "$resources/firmware/COMPONENT_43439" \
                "$resources/clm/COMPONENT_43439/COMPONENT_MURATA-1YN"
            printf 'mock firmware\n' > "$resources/firmware/COMPONENT_43439/43439A0.bin"
            printf 'mock clm\n' > "$resources/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob"
        fi
        exit "${MOCK_BLOB_STATUS:-0}" ;;
    "-m west sdk list")
        printf '0.17.4:\n  path: %s\n' "${MOCK_SDK_PATH:-$HOME/zephyr-sdk-0.17.4}"
        # Output larger than a pipe buffer fails under pipefail if the reader stops early.
        printf '0.18.0:\n'
        printf '  toolchain: %s\n' $(seq 20000) ;;
    "-m west sdk install "*)
        sdk="${MOCK_SDK_PATH:-$HOME/zephyr-sdk-0.17.4}"
        toolchain=arm-zephyr-eabi
        while (($#)); do
            if [[ "$1" == --toolchains ]]; then toolchain="$2"; shift; fi
            shift
        done
        mkdir -p "$sdk/$toolchain/bin"
        printf '0.17.4\n' > "$sdk/sdk_version"
        touch "$sdk/$toolchain/bin/$toolchain-gcc"
        chmod +x "$sdk/$toolchain/bin/$toolchain-gcc" ;;
    "-m west build "*)
        [[ "${MOCK_BUILD_STATUS:-0}" == 0 ]] || exit "$MOCK_BUILD_STATUS"
        printf 'ZEPHYR_SDK_INSTALL_DIR=%s\n' "${ZEPHYR_SDK_INSTALL_DIR:-}" >> "$MOCK_LOG"
        build="$MOCK_ROOT/build/firmware"
        driver=SDHC_ESP32
        target=esp32s3_devkitc/esp32s3/procpu
        while (($#)); do
            case "$1" in
                -d) build="$2"; shift ;;
                -b) target="$2"; shift ;;
                -DDTC_OVERLAY_FILE=*sd_spi.overlay) driver=SPI_SDHC ;;
            esac
            shift
        done
        if [[ -f "$build/CMakeCache.txt" ]] &&
            ! grep -Fxq "APPLICATION_BINARY_DIR:PATH=$build" "$build/CMakeCache.txt"; then
            printf 'Build directory moved; cached pristine path is stale\n' >&2
            exit 18
        fi
        mkdir -p "$build/zephyr"
        printf 'APPLICATION_BINARY_DIR:PATH=%s\nAPPLICATION_SOURCE_DIR:PATH=%s/firmware\n' \
            "$build" "$MOCK_ROOT" > "$build/CMakeCache.txt"
        for file in zephyr.elf runners.yaml; do
            printf 'artifact\n' > "$build/zephyr/$file"
        done
        image=zephyr.bin
        [[ "$target" != rpi* ]] || image=zephyr.uf2
        if [[ "${MOCK_INVALID_IMAGE:-0}" == 1 ]]; then
            printf 'incomplete\n' > "$build/zephyr/$image"
        else
            printf '\xe9image\n' > "$build/zephyr/$image"
        fi
        printf 'CONFIG_BOARD_TARGET="%s"\n' "$target" > "$build/zephyr/.config"
        if [[ "$build" == *-no-storage ]]; then
            printf '%s\n' '# CONFIG_USBD_MSC_CLASS is not set' \
                '# CONFIG_DISK_ACCESS is not set' \
                '# CONFIG_FILE_SYSTEM is not set' >> "$build/zephyr/.config"
        else
            printf 'CONFIG_%s=y\n' "$driver" >> "$build/zephyr/.config"
        fi ;;
    "-m west flash "*)
        exit "${MOCK_FLASH_STATUS:-0}" ;;
    *)
        printf 'Unexpected invocation: %s\n' "$*" >&2
        exit 99 ;;
esac
"""


class WrapperTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="zephyrforge-wrappers-")
        self.addCleanup(temp.cleanup)
        self.workspace = Path(temp.name)
        # Confine mocked workspace discovery even when fixtures live beneath a real west workspace.
        (self.workspace / ".west").mkdir()
        (self.workspace / ".west/config").write_text("[manifest]\npath = ZephyrForge\nfile = west.yml\n")
        self.root = self.workspace / "ZephyrForge"
        shutil.copytree(TOOLS, self.root / "tools/firmware")
        shutil.copytree(TOOLS.parents[1] / "patches", self.root / "patches")
        shutil.copy(TOOLS.parents[1] / "west.yml", self.root / "west.yml")
        (self.root / "firmware/src/app").mkdir(parents=True)
        (self.root / "firmware/src/app/zephyrforge.h").write_text(
            '#define ZEPHYRFORGE_VERSION "0.1.0"\n')
        self.bin = self.workspace / "mock-bin"
        self.bin.mkdir()
        python = self.bin / "python3"
        python.write_text(MOCK)
        python.chmod(0o755)
        shutil.copy(python, self.bin / "python")
        bash = self.bin / "bash"
        bash.write_text("""#!/bin/bash
set -euo pipefail
if [[ "$1" == *"/check-pico-pio.sh" ]]; then
    printf 'check-pico-pio %s\\n' "$2" >> "$MOCK_LOG"
    exit "${MOCK_PIO_STATUS:-0}"
fi
exec /bin/bash "$@"
""")
        bash.chmod(0o755)
        for tool in ("git", "cmake", "ninja", "gperf", "dtc", "make", "gcc", "g++", "xz", "wget"):
            path = self.bin / tool
            path.write_text("#!/usr/bin/env bash\n"
                            'if [[ "$*" == *"rev-parse HEAD"* ]]; then '
                            "echo 28af05ca658cbacf745fd5b099566311c58ccb1d; fi\n"
                            "exit 0\n")
            path.chmod(0o755)
        (self.bin / "cmake").write_text("""#!/usr/bin/env bash
set -euo pipefail
printf 'cmake %s\\n' "$*" >> "$MOCK_LOG"
if [[ "$*" == *"/cmake/pristine.cmake" ]]; then
    case "$1" in
    "-DBINARY_DIR=$MOCK_ROOT/build/firmware"|"-DBINARY_DIR=$MOCK_ROOT/build/firmware-sd-spi"|\
    "-DBINARY_DIR=$MOCK_ROOT/build/firmware-no-storage"|\
    "-DBINARY_DIR=$MOCK_ROOT/build/firmware-pico2-no-storage")
            rm "${1#-DBINARY_DIR=}/CMakeCache.txt" ;;
        *) printf 'Refusing to clean outside the selected build\\n' >&2; exit 20 ;;
    esac
fi
""")
        self.log = self.workspace / "commands.log"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", PYTHON=str(python),
                        HOME=str(self.workspace), MOCK_ROOT=str(self.root), MOCK_LOG=str(self.log))
        for key in ("VENV_DIR", "VIRTUAL_ENV", "ZEPHYR_BASE", "ZEPHYR_SDK_INSTALL_DIR", "GH_TOKEN",
                    "ZEPHYR_TOOLCHAIN_VARIANT", "CROSS_COMPILE", "WEST_CONFIG_LOCAL",
                    "WEST_CONFIG_GLOBAL", "WEST_CONFIG_SYSTEM", "MOCK_SDK_PATH", "MOCK_WEST_CMAKE_ARGS",
                    "MOCK_SERIAL_PORT", "MOCK_PIO_STATUS",
                    "MOCK_BUILD_STATUS", "MOCK_BLOB_STATUS", "MOCK_PIP_STATUS", "MOCK_FLASH_STATUS",
                    "MOCK_PYTHON_STATUS", "MOCK_INVALID_IMAGE"):
            self.env.pop(key, None)

    def run_script(self, name, *args):
        return subprocess.run(["bash", str(self.root / "tools/firmware" / name), *args],
                              cwd=self.root, env=self.env, capture_output=True, text=True)

    def commands(self):
        return self.log.read_text() if self.log.exists() else ""

    def test_build_argument_validator_rejects_wrapper_setting_overrides_and_unsets(self):
        for arguments in (
            ["-DZEPHYRFORGE_VERSION=9.8.7"],
            ["-DBOARD=some_other_board"],
            ["-DZEPHYR_BASE=/tmp/other-zephyr"],
            ["-DZephyr_DIR:PATH=/tmp/other-zephyr/share/zephyr-package/cmake"],
            ["-DCONF_FILE=other.conf"],
            ["-DDTC_OVERLAY_FILE=other.overlay"],
            ["-DZF_ENABLE_STORAGE=OFF"],
            ["-U", "ZEPHYRFORGE_VERSION"],
            ["-U", "ZF_ENABLE_STORAGE*"],
            ["-U*"],
        ):
            with self.subTest(arguments=arguments):
                with self.assertRaises(ValueError):
                    CHECKER.validate_arguments(arguments)

    def test_build_argument_validator_allows_unrelated_cmake_defaults(self):
        CHECKER.validate_arguments(["-DAPP_OPTION:BOOL=ON", "-USCRATCH_CACHE_*"])

    def test_build_bootstraps_then_reuses_venv_workspace_and_sdk(self):
        for _ in range(2):
            result = self.run_script("build.sh")
            self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        for command in ("-m venv ", "-m west sdk install "):
            self.assertEqual(commands.count(command), 1)
        self.assertNotIn("-m west init ", commands)
        self.assertEqual(commands.count("-m west build "), 2)
        self.assertEqual(commands.count("--no-sysbuild"), 2)
        self.assertIn("-m west update --narrow -o=--depth=1", commands)
        self.assertIn("-m west blobs fetch hal_espressif", commands)
        self.assertIn("-b esp32s3_devkitc/esp32s3/procpu", commands)
        self.assertIn("-DDTC_OVERLAY_FILE=boards/esp32s3/board.overlay;"
                      "boards/esp32s3/storage/sdmmc.overlay", commands)
        self.assertIn("-DCONF_FILE=prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;"
                      "profiles/tcp-shell.conf;boards/esp32s3/board.conf;profiles/usb.conf;"
                      "profiles/storage.conf;profiles/usb-msc.conf", commands)
        self.assertIn("-DZF_ENABLE_STORAGE_BENCHMARK=ON", commands)

    def test_dependency_patch_applies_once_accepts_reverse_match_and_rejects_conflict(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        git = self.bin / "git"
        git.write_text("""#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$MOCK_LOG"
case "$*" in
    *"rev-parse HEAD"*) echo 28af05ca658cbacf745fd5b099566311c58ccb1d ;;
    *"apply --reverse --check"*) [[ "$PATCH_STATE" == applied ]] ;;
    *"apply --check"*) [[ "$PATCH_STATE" == fresh ]] ;;
    *"apply "*) exit 0 ;;
    *) exit 99 ;;
esac
""")
        for state, expected in (("fresh", 0), ("applied", 0), ("conflict", 1)):
            self.env["PATCH_STATE"] = state
            self.log.write_text("")
            result = self.run_script("apply-zephyr-patches.sh", str(self.workspace / "zephyr"))
            self.assertEqual(result.returncode, expected, result.stderr)
            calls = self.commands()
            self.assertIn("apply --check", calls)
            if state == "fresh":
                self.assertNotIn("apply --reverse", calls)
            else:
                self.assertIn("apply --reverse --check", calls)
            if state == "conflict":
                self.assertIn("neither applies nor matches", result.stderr)
        self.env["PATCH_STATE"] = "fresh"
        self.log.write_text("")
        result = self.run_script("apply-zephyr-patches.sh", str(self.root))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("apply", self.commands())

    def test_flash_builds_missing_artifacts_then_flashes_once(self):
        result = self.run_script("flash.sh", "--port", "/dev/mock", "--baud", "115200")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertEqual(commands.count("-m west build "), 1)
        self.assertEqual(commands.count("-m west flash "), 1)
        self.assertLess(commands.index("-m west build "), commands.index("-m west flash "))
        self.assertIn("--skip-rebuild -r esp32 --esp-device /dev/mock --esp-baud-rate 115200 --no-erase",
                      commands)

    def test_flash_reuses_existing_artifacts(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        self.log.write_text("")
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("-m west build ", self.commands())

    def test_flash_repairs_missing_moved_checkout_before_loading_manifest(self):
        config = self.workspace / ".west/config"
        previous = self.workspace / "previous-checkout"
        for storage, directory in (("sdmmc", "firmware"), ("sd-spi", "firmware-sd-spi")):
            with self.subTest(storage=storage):
                self.assertEqual(self.run_script("build.sh", "--storage", storage).returncode, 0)
                cache = self.root / f"build/{directory}/CMakeCache.txt"
                current = cache.read_text()
                cache.write_text(current.replace(str(self.root / "firmware"), str(previous / "firmware")))
                config.write_text(
                    "[manifest]\npath = previous-checkout\nfile = west.yml\n[zephyr]\nbase = zephyr\n")
                self.log.write_text("")
                result = self.run_script("flash.sh", "--storage", storage, "--port", "/dev/mock", "--dry-run")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("path = ZephyrForge", config.read_text())
                self.assertIn("base = zephyr", config.read_text())
                calls = self.commands()
                self.assertLess(calls.index("-m west config --local manifest.path ZephyrForge"),
                                calls.index("-m west update "))
                self.assertNotIn("-m west build ", calls)
                self.assertNotIn("-m west flash ", calls)
                cache.write_text(current)

    def test_existing_unrelated_manifest_is_refused_without_redirecting_it(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        previous = self.workspace / "previous-checkout"
        previous.mkdir()
        (previous / "west.yml").touch()
        cache = self.root / "build/firmware/CMakeCache.txt"
        cache.write_text(cache.read_text().replace(str(self.root / "firmware"), str(previous / "firmware")))
        config = self.workspace / ".west/config"
        for manifest in ("previous-checkout", "another-project"):
            with self.subTest(manifest=manifest):
                content = f"[manifest]\npath = {manifest}\nfile = west.yml\n"
                config.write_text(content)
                self.log.write_text("")
                result = self.run_script("flash.sh", "--port", "/dev/mock", "--dry-run")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("west workspace manifest must point to this checkout", result.stderr)
                self.assertEqual(config.read_text(), content)
                self.assertNotIn(f"-m west config --local manifest.path {self.root.name}", self.commands())
                self.assertNotIn("-m west update ", self.commands())
                self.assertNotIn("-m west flash ", self.commands())

    def test_build_refuses_to_update_an_unrelated_workspace_manifest(self):
        foreign = self.workspace / "another-app"
        foreign.mkdir()
        (foreign / "west.yml").write_text("manifest:\n  projects: []\n")
        (self.workspace / ".west/config").write_text(
            "[manifest]\npath = another-app\nfile = west.yml\n")
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("west workspace manifest must point to this checkout", result.stderr)
        self.assertNotIn("-m pip install --disable-pip-version-check --no-input west", self.commands())
        self.assertNotIn("-m west update ", self.commands())

    def test_build_refuses_an_alternate_manifest_file_before_updating(self):
        (self.root / "alternate.yml").write_text("manifest:\n  projects: []\n")
        (self.workspace / ".west/config").write_text(
            "[manifest]\npath = ZephyrForge\nFILE = alternate.yml\n")
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must select this checkout's west.yml", result.stderr)
        self.assertNotIn("-m pip install --disable-pip-version-check --no-input west", self.commands())
        self.assertNotIn("-m west update ", self.commands())

    def test_build_refuses_manifest_selected_by_west_config_override(self):
        override = self.workspace / "west-override.conf"
        override.write_text("[manifest]\npath = ZephyrForge\nfile = alternate.yml\n")
        (self.root / "alternate.yml").write_text("manifest:\n  projects: []\n")
        self.env["WEST_CONFIG_LOCAL"] = str(override)
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must select this checkout's west.yml", result.stderr)
        self.assertNotIn("-m pip install --disable-pip-version-check --no-input west", self.commands())
        self.assertNotIn("-m west update ", self.commands())

    def test_invalid_sdk_environment_uses_the_pinned_sdk_for_the_build(self):
        wrong_sdk = self.workspace / "zephyr-sdk-0.18.0"
        wrong_sdk.mkdir()
        (wrong_sdk / "sdk_version").write_text("0.18.0\n")
        self.env["ZEPHYR_SDK_INSTALL_DIR"] = str(wrong_sdk)
        self.env["GH_TOKEN"] = "mock-token"
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        pinned_sdk = self.workspace / "zephyr-sdk-0.17.4"
        self.assertIn("-m west sdk install --version 0.17.4 --toolchains arm-zephyr-eabi "
                      "--personal-access-token mock-token", commands)
        self.assertIn(f"ZEPHYR_SDK_INSTALL_DIR={pinned_sdk}", commands)
        self.assertIn(f"-DZEPHYR_SDK_INSTALL_DIR={pinned_sdk}", commands)
        self.assertNotIn(f"ZEPHYR_SDK_INSTALL_DIR={wrong_sdk}", commands)

    def test_build_overrides_an_inherited_external_toolchain_variant(self):
        self.env["ZEPHYR_TOOLCHAIN_VARIANT"] = "cross-compile"
        self.env["CROSS_COMPILE"] = "/missing/review-toolchain-"
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-DZEPHYR_TOOLCHAIN_VARIANT=zephyr", self.commands())

    def test_build_rejects_west_cmake_args_that_override_the_sdk(self):
        self.env["MOCK_WEST_CMAKE_ARGS"] = "-DZEPHYR_TOOLCHAIN_VARIANT=cross-compile"
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("west build.cmake-args must not override", result.stderr)
        self.assertNotIn("-m west update ", self.commands())
        self.assertNotIn("-m west build ", self.commands())

    def test_build_rejects_west_cmake_args_that_override_firmware_version(self):
        self.env["MOCK_WEST_CMAKE_ARGS"] = "-DZEPHYRFORGE_VERSION=9.8.7"
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("-m west build ", self.commands())

    def test_flash_reuses_existing_artifacts_with_build_only_overrides(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        self.env["MOCK_WEST_CMAKE_ARGS"] = "-DZEPHYRFORGE_VERSION=9.8.7"
        self.env["MOCK_SERIAL_PORT"] = "/dev/mock"
        self.log.write_text("")
        result = self.run_script("flash.sh", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Auto-detected serial port: /dev/mock", result.stdout)
        self.assertNotIn("-m west build ", self.commands())
        self.assertNotIn("-m west flash ", self.commands())

    def test_build_cleans_cache_when_cached_sdk_or_toolchain_changes(self):
        sdk = self.workspace / "sdk-0.17.4"
        compiler = sdk / "arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
        compiler.parent.mkdir(parents=True)
        compiler.touch()
        compiler.chmod(0o755)
        (sdk / "sdk_version").write_text("0.17.4\n")
        self.env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk)
        build_dir = self.root / "build/firmware-pico2-no-storage"
        build_dir.mkdir(parents=True)
        (build_dir / "CMakeCache.txt").write_text(
            f"APPLICATION_BINARY_DIR:PATH={build_dir}\n"
            f"ZEPHYR_SDK_INSTALL_DIR:PATH={sdk}\n"
            "Zephyr-sdk_DIR:PATH=/old/sdk/cmake\n"
            "ZEPHYR_TOOLCHAIN_VARIANT:STRING=zephyr\n"
            "CMAKE_C_COMPILER:FILEPATH=/old/sdk/bin/compiler\n"
        )
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"cmake -DBINARY_DIR={build_dir} -DSOURCE_DIR={self.root / 'firmware'}", self.commands())
        self.assertIn("/cmake/pristine.cmake", self.commands())

    def test_build_cleans_cache_when_cached_source_or_sysbuild_changes(self):
        build_dir = self.root / "build/firmware-pico2-no-storage"
        build_dir.mkdir(parents=True)
        (build_dir / "CMakeCache.txt").write_text(
            f"APPLICATION_BINARY_DIR:PATH={build_dir}\n"
            "CMAKE_HOME_DIRECTORY:INTERNAL=/tmp/old-zephyr-share-sysbuild\n"
            "ZEPHYR_BASE:UNINITIALIZED=/tmp/old-zephyr\n"
            "Zephyr_DIR:PATH=/tmp/old-zephyr/share/zephyr-package/cmake\n"
        )
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Build system changed; cleaning", result.stdout)
        self.assertIn("Zephyr installation changed; cleaning", result.stdout)
        self.assertIn("/cmake/pristine.cmake", self.commands())

    def test_sdk_registered_outside_home_is_selected_after_install(self):
        wrong_sdk = self.workspace / "zephyr-sdk-0.18.0"
        wrong_sdk.mkdir()
        (wrong_sdk / "sdk_version").write_text("0.18.0\n")
        registered_sdk = self.workspace / "registered" / "zephyr-sdk-0.17.4"
        self.env["ZEPHYR_SDK_INSTALL_DIR"] = str(wrong_sdk)
        self.env["MOCK_SDK_PATH"] = str(registered_sdk)
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn(f"ZEPHYR_SDK_INSTALL_DIR={registered_sdk}", commands)
        self.assertIn(f"-DZEPHYR_SDK_INSTALL_DIR={registered_sdk}", commands)

    def test_flash_rebuilds_moved_source_cache_without_flashing_in_dry_run(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        build = self.root / "build/firmware"
        (build / "zephyr/zephyr.bin").unlink()
        old_build = self.workspace / "previous-checkout/build/firmware"
        old_build.mkdir(parents=True)
        marker = old_build / "keep"
        marker.touch()
        (build / "CMakeCache.txt").write_text(f"APPLICATION_BINARY_DIR:PATH={old_build}\n")
        self.log.write_text("")
        result = self.run_script("flash.sh", "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"cmake -DBINARY_DIR={build} -DSOURCE_DIR={self.root / 'firmware'}", self.commands())
        self.assertIn("-m west build ", self.commands())
        self.assertNotIn("-m west flash ", self.commands())
        self.assertTrue(marker.exists(), "a cached old build path must never be cleaned")

    def test_spi_build_and_flash_use_a_separate_image(self):
        result = self.run_script("build.sh", "--storage", "sd-spi")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-d " + str(self.root / "build/firmware-sd-spi"), self.commands())
        self.assertIn("-DDTC_OVERLAY_FILE=boards/esp32s3/board.overlay;"
                      "boards/esp32s3/storage/sd_spi.overlay", self.commands())
        self.assertIn("-DCONF_FILE=prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;"
                      "profiles/tcp-shell.conf;boards/esp32s3/board.conf;profiles/usb.conf;"
                      "profiles/storage.conf;profiles/usb-msc.conf", self.commands())
        self.log.write_text("")
        result = self.run_script("flash.sh", "--storage", "sd-spi", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("-m west build ", self.commands())
        self.assertIn("-m west flash -d " + str(self.root / "build/firmware-sd-spi"), self.commands())
        self.assertFalse((self.root / "build/firmware").exists())

    def test_pico_build_selects_board_overlays_and_arm_toolchain_without_esp_blobs(self):
        result = self.run_script("build.sh", "--board", "pico2_w", "--storage", "sd-spi")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn("-b rpi_pico2/rp2350a/m33/w", commands)
        self.assertIn("-d " + str(self.root / "build/firmware-pico2_w-sd-spi"), commands)
        self.assertIn("-DDTC_OVERLAY_FILE=boards/rpi_pico/board.overlay;"
                      "boards/rpi_pico/soc/rp2350.overlay;boards/rpi_pico/storage/sd_spi.overlay;"
                      "boards/rpi_pico/wireless/w.overlay", commands)
        self.assertIn("-DCONF_FILE=prj.conf;profiles/wifi.conf;boards/rpi_pico/wireless/w.conf;"
                      "profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf", commands)
        self.assertIn("-m west sdk install --version 0.17.4 --toolchains arm-zephyr-eabi", commands)
        self.assertIn("-m west blobs fetch hal_infineon --allow-regex", commands)
        self.assertNotIn("-m west blobs fetch hal_espressif", commands)
        self.assertFalse((self.root / "build/firmware").exists())

    def test_pico_build_stops_when_pio_consistency_check_fails(self):
        self.env["MOCK_PIO_STATUS"] = "17"
        result = self.run_script("build.sh", "--board", "pico")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("check-pico-pio ", self.commands())
        self.assertNotIn("-m west build ", self.commands())

    def test_rp2350_sdmmc_build_uses_shared_storage_overlay(self):
        result = self.run_script("build.sh", "--board", "pico2", "--storage", "sdmmc")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn("-DDTC_OVERLAY_FILE=boards/rpi_pico/board.overlay;"
                      "boards/rpi_pico/soc/rp2350.overlay;boards/rpi_pico/storage/sdmmc.overlay", commands)
        self.assertNotIn("rp2350_sdmmc.overlay", commands)

    def test_pico_w_uses_the_shared_wifi_configuration_and_radio_firmware(self):
        result = self.run_script("build.sh", "--board", "pico_w")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn("-b rpi_pico/rp2040/w", commands)
        self.assertIn("-DCONF_FILE=prj.conf;profiles/wifi.conf;boards/rpi_pico/wireless/w.conf;"
                      "profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf", commands)
        self.assertIn("-m west blobs fetch hal_infineon --allow-regex", commands)
        self.assertNotIn("profiles/bluetooth.conf", commands)

    def test_no_storage_build_omits_sd_overlays_and_stack_for_each_family(self):
        cases = (
            ("esp32s3", "esp32s3_devkitc/esp32s3/procpu",
             "boards/esp32s3/board.overlay",
             "prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;profiles/tcp-shell.conf;"
             "boards/esp32s3/board.conf;profiles/usb.conf",
             "firmware-no-storage"),
            ("pico2_w", "rpi_pico2/rp2350a/m33/w",
             "boards/rpi_pico/board.overlay;boards/rpi_pico/soc/rp2350.overlay;"
             "boards/rpi_pico/wireless/w.overlay",
             "prj.conf;profiles/wifi.conf;boards/rpi_pico/wireless/w.conf;profiles/usb.conf",
             "firmware-pico2_w-no-storage"),
            ("esp32c6", "esp32c6_devkitc/esp32c6/hpcore",
             "boards/esp32c6/board.overlay",
             "prj.conf;profiles/wifi.conf;profiles/bluetooth.conf",
             "firmware-esp32c6-no-storage"),
        )
        for board, target, overlays, config, directory in cases:
            with self.subTest(board=board):
                self.log.write_text("")
                result = self.run_script("build.sh", "--board", board, "--storage", "none")
                self.assertEqual(result.returncode, 0, result.stderr)
                commands = self.commands()
                self.assertIn(f"-b {target}", commands)
                self.assertIn(f"-d {self.root / 'build' / directory}", commands)
                self.assertIn(f"-DDTC_OVERLAY_FILE={overlays}", commands)
                self.assertIn(f"-DCONF_FILE={config} ", commands)
                self.assertIn("-DZF_ENABLE_STORAGE=OFF", commands)
                self.assertIn("-DZF_ENABLE_STORAGE_BENCHMARK=OFF", commands)
                self.assertNotIn("sdmmc.overlay", commands)
                self.assertNotIn("profiles/storage.conf", commands)
                self.assertNotIn("sd_spi.overlay", commands)

    def test_esp32c6_defaults_to_no_storage_and_selects_riscv_sdk_and_radio_blobs(self):
        result = self.run_script("build.sh", "--board", "esp32c6")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn("-b esp32c6_devkitc/esp32c6/hpcore", commands)
        self.assertIn("-d " + str(self.root / "build/firmware-esp32c6-no-storage"), commands)
        self.assertIn("-m west sdk install --version 0.17.4 --toolchains riscv64-zephyr-elf", commands)
        self.assertIn("-m west blobs fetch hal_espressif", commands)
        self.assertIn("-DDTC_OVERLAY_FILE=boards/esp32c6/board.overlay", commands)
        self.assertNotIn("profiles/usb.conf", commands)
        self.assertNotIn("profiles/storage.conf", commands)
        self.assertNotIn("profiles/tcp-shell.conf", commands)
        self.assertNotIn("-m west blobs fetch hal_infineon", commands)
        self.log.write_text("")
        result = self.run_script("build.sh", "--board", "esp32c6", "--storage", "sdmmc")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not native SDMMC", result.stderr)
        self.assertEqual(self.commands(), "")

    def test_esp32c6_spi_build_and_flash_use_their_own_profile(self):
        result = self.run_script("build.sh", "--board", "esp32c6", "--storage", "sd-spi")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertIn("-d " + str(self.root / "build/firmware-esp32c6-sd-spi"), commands)
        self.assertIn("-DDTC_OVERLAY_FILE=boards/esp32c6/board.overlay;"
                      "boards/esp32c6/storage/sd_spi.overlay", commands)
        self.assertIn("-DZF_ENABLE_STORAGE_BENCHMARK=OFF", commands)
        self.assertNotIn("profiles/usb.conf", commands)
        self.assertNotIn("profiles/storage.conf;profiles/usb-msc.conf", commands)
        self.log.write_text("")
        result = self.run_script("flash.sh", "--board", "esp32c6", "--storage", "sd-spi",
                                 "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-d " + str(self.root / "build/firmware-esp32c6-sd-spi"), result.stdout)
        self.assertIn("--esp-device /dev/mock", result.stdout)
        self.assertNotIn("-m west flash ", self.commands())

    def test_non_w_pico_boards_do_not_enable_wireless_or_fetch_radio_firmware(self):
        for board in ("pico", "pico2"):
            with self.subTest(board=board):
                self.log.write_text("")
                result = self.run_script("build.sh", "--board", board)
                self.assertEqual(result.returncode, 0, result.stderr)
                commands = self.commands()
                self.assertIn("-DCONF_FILE=prj.conf;profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf "
                              "-DDTC_OVERLAY_FILE=boards/rpi_pico/",
                              commands)
                self.assertNotIn("profiles/wifi.conf", commands)
                self.assertNotIn("-m west blobs fetch", commands)

    def test_spi_dry_run_builds_only_the_selected_image(self):
        result = self.run_script("flash.sh", "--storage", "sd-spi", "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "build/firmware-sd-spi/zephyr/zephyr.bin").exists())
        self.assertFalse((self.root / "build/firmware").exists())
        self.assertNotIn("-m west flash ", self.commands())

    def test_no_storage_esp_image_is_flashable_and_profile_checked(self):
        result = self.run_script("build.sh", "--storage", "none")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.log.write_text("")
        result = self.run_script("flash.sh", "--storage", "none", "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-d " + str(self.root / "build/firmware-no-storage"), result.stdout)
        self.assertIn("Dry run only (no device access)", result.stdout)
        self.assertNotIn("-m west flash ", self.commands())
        self.assertNotIn("-m west build ", self.commands())
        self.assertFalse((self.root / "build/firmware").exists())

    def test_esp32c6_no_storage_flash_accepts_absent_usb_msc_symbol(self):
        self.assertEqual(self.run_script("build.sh", "--board", "esp32c6").returncode, 0)
        config = self.root / "build/firmware-esp32c6-no-storage/zephyr/.config"
        config.write_text(config.read_text().replace("# CONFIG_USBD_MSC_CLASS is not set\n", ""))
        result = self.run_script("flash.sh", "--board", "esp32c6", "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Dry run only (no device access)", result.stdout)
        self.assertNotIn("-m west flash ", self.commands())

    def test_wrong_storage_image_is_rejected_without_flashing(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        config = self.root / "build/firmware/zephyr/.config"
        config.write_text(config.read_text().replace("CONFIG_SDHC_ESP32=y", "CONFIG_SPI_SDHC=y"))
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match --storage sdmmc", result.stderr)
        self.assertNotIn("-m west flash ", self.commands())

    def test_incomplete_image_is_rebuilt_and_failed_conversion_prevents_flash(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        image = self.root / "build/firmware/zephyr/zephyr.bin"
        image.write_bytes(b"raw data")
        self.log.write_text("")
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-m west build ", self.commands())
        image.write_bytes(b"raw data")
        self.env["MOCK_INVALID_IMAGE"] = "1"
        self.log.write_text("")
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("valid ESP image", result.stderr)
        self.assertNotIn("-m west flash ", self.commands())

    def test_invalid_storage_does_not_install_build_or_flash(self):
        for name in ("build.sh", "flash.sh"):
            result = self.run_script(name, "--storage", "sdio")
            self.assertNotEqual(result.returncode, 0)
        self.assertNotEqual(self.run_script("build.sh", "--board", "pico3").returncode, 0)
        self.assertEqual(self.commands(), "")

    def test_flash_auto_detects_serial_port_when_omitted(self):
        self.env["MOCK_SERIAL_PORT"] = "/dev/mock-auto"
        result = self.run_script("flash.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Auto-detected serial port: /dev/mock-auto", result.stdout)
        self.assertIn("--esp-device /dev/mock-auto", self.commands())

    def test_flash_without_detectable_port_fails_before_build(self):
        result = self.run_script("flash.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not detect a serial port", result.stderr)
        self.assertNotIn("-m west build ", self.commands())

    def test_dry_run_builds_but_never_flashes(self):
        result = self.run_script("flash.sh", "--port", "/dev/mock", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-m west build ", self.commands())
        self.assertNotIn("-m west flash ", self.commands())
        self.assertIn("Dry run only (no device access)", result.stdout)

    def test_build_failure_prevents_flash(self):
        self.env["MOCK_BUILD_STATUS"] = "31"
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 31)
        self.assertNotIn("-m west flash ", self.commands())

    def test_dependency_failure_prevents_build_and_flash(self):
        for key in ("MOCK_PIP_STATUS", "MOCK_BLOB_STATUS"):
            with self.subTest(key=key):
                self.env[key] = "23"
                self.log.write_text("")
                result = self.run_script("flash.sh", "--port", "/dev/mock")
                self.assertEqual(result.returncode, 23)
                self.assertNotIn("-m west build ", self.commands())
                self.assertNotIn("-m west flash ", self.commands())
                del self.env[key]

    def test_wrong_board_is_rejected_without_flashing(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        (self.root / "build/firmware/zephyr/.config").write_text('CONFIG_BOARD_TARGET="other"\n')
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Build is not for", result.stderr)
        self.assertNotIn("-m west flash ", self.commands())

    def test_help_and_invalid_options_do_not_install(self):
        self.assertEqual(self.run_script("flash.sh", "--help").returncode, 0)
        self.assertNotEqual(self.run_script("flash.sh", "--port", "/dev/mock", "--baud", "0").returncode, 0)
        self.assertEqual(self.commands(), "")

    def test_relative_custom_venv_and_existing_workspace(self):
        (self.workspace / ".west").mkdir(exist_ok=True)
        (self.workspace / ".west/config").touch()
        self.env["VENV_DIR"] = "custom venv"
        result = self.run_script("build.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "custom venv/pyvenv.cfg").exists())
        self.assertNotIn("-m west init ", self.commands())

    def test_invalid_venv_is_not_overwritten(self):
        (self.workspace / ".venv").mkdir()
        result = self.run_script("build.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a Python venv", result.stderr)
        self.assertNotIn("-m venv ", self.commands())

    def test_runner_error_is_not_retried(self):
        self.env["MOCK_FLASH_STATUS"] = "19"
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 19)
        self.assertEqual(self.commands().count("-m west flash "), 1)

    def mock_package_manager(self):
        apt = self.bin / "apt-get"
        apt.write_text(
            '#!/usr/bin/env bash\n'
            'printf "apt-get %s\\n" "$*" >> "$MOCK_LOG"\n'
            'exit "${MOCK_APT_STATUS:-0}"\n'
        )
        apt.chmod(0o755)
        sudo = self.bin / "sudo"
        sudo.write_text('#!/usr/bin/env bash\nexec "$@"\n')
        sudo.chmod(0o755)
        self.env["MOCK_PYTHON_STATUS"] = "1"

    def test_missing_venv_support_installs_host_package(self):
        self.mock_package_manager()
        result = self.run_script("build.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("apt-get update", self.commands())
        self.assertIn("apt-get install -y --no-install-recommends python3-venv", self.commands())

    def test_host_install_failure_stops_setup(self):
        self.mock_package_manager()
        self.env["MOCK_APT_STATUS"] = "29"
        result = self.run_script("flash.sh", "--port", "/dev/mock")
        self.assertEqual(result.returncode, 29)
        self.assertNotIn("-m venv ", self.commands())
        self.assertNotIn("-m west flash ", self.commands())

    def test_release_version_components_fit_firmware_identity_fields(self):
        helper = self.root / "tools/firmware/release-version.sh"
        for tag, expected_status, expected_output in (
            ("v255.255.255", 0, "255.255.255"),
            ("v0.1.0", 0, "0.1.0"),
            ("v256.0.0", 1, ""),
            ("v0.256.0", 1, ""),
            ("v0.0.256", 1, ""),
        ):
            with self.subTest(tag=tag):
                result = subprocess.run(["bash", str(helper), tag], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected_status, result.stderr)
                self.assertEqual(result.stdout.strip(), expected_output)


if __name__ == "__main__":
    unittest.main()
