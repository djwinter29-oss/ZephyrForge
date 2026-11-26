"""Firmware package contents and release archives, without hardware or downloads."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


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
        self.write("zephyrforge/firmware/src/zephyrforge.h", '#define ZEPHYRFORGE_VERSION "0.1.0"\n')
        self.write("bin/git", "#!/bin/sh\nprintf '%s\\n' fixture-commit\n")
        (self.workspace / "bin/git").chmod(0o755)
        for component in ("zephyr", "modules/hal/rpi_pico", "modules/hal/cmsis_6",
                          "modules/hal/espressif", "modules/crypto/mbedtls"):
            self.write(f"{component}/LICENSE", f"{component} license")
        self.write("modules/fs/fatfs/ff.c", "/* FatFs license */\n")
        for name in ("zephyr.bin", "zephyr.uf2", "zephyr.elf", "zephyr.dts", "runners.yaml"):
            self.write(f"firmware-build/zephyr/{name}", "fixture")
        self.write("firmware-build/west-manifest.yml", "manifest: fixture\n")

    def write(self, path, content):
        target = self.workspace / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)

    def run_package(self, board, target, storage="sdmmc", config_storage=None, identity_storage=None,
                    package_version=None, c6_simple_boot=True, c6_flash_size=8388608,
                    c6_app_address="0x0", replace_image_after_snapshot=False, identity_version="0.1.0",
                    clear_package=True, replace_elf_after_snapshot=False):
        package = self.repo / "build/firmware-package"
        if clear_package and package.exists():
            shutil.rmtree(package)
        config = f'CONFIG_BOARD_TARGET="{target}"\n'
        config_storage = storage if config_storage is None else config_storage
        if config_storage != "none":
            config += "\n".join(f"CONFIG_{symbol}=y" for symbol in (
                "DISK_ACCESS", "DISK_DRIVER_SDMMC", "SDHC", "FILE_SYSTEM",
                "FAT_FILESYSTEM_ELM", "FS_FATFS_EXFAT"))
            config += "\n"
            if board in ("esp32s3", "pico", "pico_w", "pico2", "pico2_w"):
                config += "CONFIG_USBD_MSC_CLASS=y\n"
            driver = {
                "esp32s3": "SDHC_ESP32",
                "esp32c6": "SPI_SDHC",
                "pico": "ZF_SDHC_RPI_PICO_PIO",
                "pico_w": "ZF_SDHC_RPI_PICO_PIO",
                "pico2": "ZF_SDHC_RPI_PICO_PIO",
                "pico2_w": "ZF_SDHC_RPI_PICO_PIO",
            }[board] if config_storage == "sdmmc" else "SPI_SDHC"
            config += f"CONFIG_{driver}=y\n"
        if board == "esp32s3":
            config += "CONFIG_ESP_SIMPLE_BOOT=y\nCONFIG_FLASH_SIZE=16777216\n"
            self.write("firmware-build/zephyr/runners.yaml",
                       "  - --esp-app-address=0x0\n"
                       "  - --esp-flash-mode=dio\n"
                       "  - --esp-flash-freq=80m\n"
                       "  - --esp-flash-size=16MB\n")
        elif board == "esp32c6":
            config += ("CONFIG_ESP_SIMPLE_BOOT=y\n" if c6_simple_boot else
                       "# CONFIG_ESP_SIMPLE_BOOT is not set\n")
            config += f"CONFIG_FLASH_SIZE={c6_flash_size}\n"
            self.write("firmware-build/zephyr/runners.yaml",
                       f"  - --esp-app-address={c6_app_address}\n"
                       "  - --esp-flash-mode=dio\n"
                       "  - --esp-flash-freq=80m\n"
                       "  - --esp-flash-size=8MB\n")
        self.write("firmware-build/zephyr/.config", config)
        identity_storage = storage if identity_storage is None else identity_storage
        image_name = "zephyr.bin" if board.startswith("esp32") else "zephyr.uf2"
        image_hash = hashlib.sha256((self.build / "zephyr" / image_name).read_bytes()).hexdigest()
        elf_hash = hashlib.sha256((self.build / "zephyr/zephyr.elf").read_bytes()).hexdigest()
        identity = (
            f"FIRMWARE_VERSION={identity_version}\n"
            "SOURCE_COMMIT=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
            "SOURCE_DIRTY=false\n"
            f"BOARD={board}\nTARGET={target}\nSTORAGE={identity_storage}\n"
            f"IMAGE_SHA256={image_hash}\nELF_SHA256={elf_hash}\n"
        )
        if board in ("pico_w", "pico2_w"):
            resources = "modules/hal/infineon/zephyr/blobs/img/whd/resources"
            for name in ("firmware/COMPONENT_43439/43439A0.bin",
                         "clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob"):
                path = self.workspace / resources / name
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                key = "RADIO_FIRMWARE_SHA256" if name.startswith("firmware/") else "RADIO_CLM_SHA256"
                identity += f"{key}={digest}\n"
        self.write("firmware-build/zephyrforge-build-info.txt", identity)
        if replace_image_after_snapshot:
            self.write(f"firmware-build/zephyr/{image_name}", "replaced")
        if replace_elf_after_snapshot:
            self.write("firmware-build/zephyr/zephyr.elf", "replaced")
        env = {**os.environ, "BOARD": board, "STORAGE": storage, "FIRMWARE_BUILD": str(self.build),
               "PATH": f"{self.workspace / 'bin'}:{os.environ['PATH']}"}
        if package_version is not None:
            env["ZEPHYRFORGE_VERSION"] = package_version
        result = subprocess.run(["bash", str(ROOT / "tools/release/package-firmware.sh")],
                                cwd=self.workspace, env=env, capture_output=True, text=True, check=False)
        return result, package

    def package(self, board, target, storage="sdmmc"):
        result, package = self.run_package(board, target, storage)
        self.assertEqual(result.returncode, 0, result.stderr)
        return package

    def test_minimal_contents_for_each_board_and_storage(self):
        self.write("modules/hal/infineon/LICENSE", "radio license")
        resources = "modules/hal/infineon/zephyr/blobs/img/whd/resources"
        self.write(f"{resources}/firmware/COMPONENT_43439/43439A0.bin", "firmware")
        self.write(f"{resources}/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob", "clm")
        boards = {
            "esp32s3": "esp32s3_devkitc/esp32s3/procpu",
            "esp32c6": "esp32c6_devkitc/esp32c6/hpcore",
            "pico": "rpi_pico/rp2040",
            "pico_w": "rpi_pico/rp2040/w",
            "pico2": "rpi_pico2/rp2350a/m33",
            "pico2_w": "rpi_pico2/rp2350a/m33/w",
        }
        for board, target in boards.items():
            storage_profiles = ("sd-spi", "none") if board == "esp32c6" else ("sdmmc", "sd-spi", "none")
            for storage in storage_profiles:
                with self.subTest(board=board, storage=storage):
                    package = self.package(board, target, storage)
                    image = "zephyr.bin" if board in ("esp32s3", "esp32c6") else "zephyr.uf2"
                    self.assertEqual({path.name for path in package.iterdir()},
                                     {image, "zephyr.elf", "BUILD-INFO.txt", "LICENSE", "licenses"})
                    self.assertEqual((package / image).read_text(), "fixture")
                    self.assertEqual((package / "zephyr.elf").read_text(), "fixture")
                    info = (package / "BUILD-INFO.txt").read_text()
                    for value in (target, storage, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "0.1.0"):
                        self.assertIn(value, info)

    def test_package_uses_built_identity_instead_of_packaging_environment(self):
        image = b"built firmware image"
        self.write("firmware-build/zephyr/zephyr.uf2", image.decode())
        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none", package_version="9.8.7")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((package / "zephyr.uf2").read_bytes(), image)
        info = (package / "BUILD-INFO.txt").read_text()
        self.assertIn("Firmware version: 0.1.0", info)
        self.assertNotIn("9.8.7", info)
        self.assertIn("Source commit: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", info)

    def test_package_rejects_storage_profile_mismatch(self):
        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none", config_storage="sdmmc")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pico2 none packaging must not include CONFIG_", result.stderr)
        self.assertFalse(package.exists())

    def test_package_rejects_build_identity_profile_mismatch(self):
        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none",
            config_storage="none", identity_storage="sdmmc")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("build identity does not match BOARD=pico2 STORAGE=none", result.stderr)
        self.assertFalse(package.exists())

    def test_package_rejects_firmware_version_outside_identity_field_range(self):
        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none", identity_version="0.1.256")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported firmware version in build identity snapshot", result.stderr)
        self.assertFalse(package.exists())

    def test_package_rejects_image_replaced_after_build_snapshot(self):
        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none", replace_image_after_snapshot=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("firmware image does not match build identity snapshot", result.stderr)
        self.assertFalse(package.exists())

    def test_package_rejects_elf_replaced_after_build_snapshot(self):
        result, package = self.run_package(
            "esp32s3", "esp32s3_devkitc/esp32s3/procpu", storage="none", replace_elf_after_snapshot=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("firmware ELF does not match build identity snapshot", result.stderr)
        self.assertFalse(package.exists())

    def test_repackaging_replaces_previous_target_image(self):
        result, package = self.run_package("esp32s3", "esp32s3_devkitc/esp32s3/procpu", storage="none")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((package / "zephyr.bin").is_file())

        result, package = self.run_package(
            "pico2", "rpi_pico2/rp2350a/m33", storage="none", clear_package=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({path.name for path in package.iterdir()},
                         {"zephyr.uf2", "zephyr.elf", "BUILD-INFO.txt", "LICENSE", "licenses"})

    def test_c6_package_rejects_incompatible_flash_layout(self):
        target = "esp32c6_devkitc/esp32c6/hpcore"
        cases = (
            ({"c6_simple_boot": False}, "CONFIG_ESP_SIMPLE_BOOT=y"),
            ({"c6_flash_size": 4194304}, "CONFIG_FLASH_SIZE=8388608"),
            ({"c6_app_address": "0x10000"}, "--esp-app-address=0x0"),
        )
        for options, expected in cases:
            with self.subTest(options=options):
                result, package = self.run_package("esp32c6", target, storage="none", **options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)
                self.assertFalse(package.exists())

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


class ReleaseArchiveTests(unittest.TestCase):
    def test_all_firmware_variants_are_archived_unchanged_and_checksummed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packages = root / "build/packages"
            packages.mkdir(parents=True)
            expected = {}
            for board in ("esp32s3", "esp32c6", "pico", "pico_w", "pico2", "pico2_w"):
                for storage in (("sd-spi", "none") if board == "esp32c6" else ("sdmmc", "sd-spi", "none")):
                    artifact = root / f"build/firmware-packages/zephyrforge-{board}-{storage}-software-build"
                    image = "zephyr.bin" if board in ("esp32s3", "esp32c6") else "zephyr.uf2"
                    contents = {
                        image: f"{board}-{storage}".encode(),
                        "zephyr.elf": f"{board}-{storage}-elf".encode(),
                        "BUILD-INFO.txt": b"build identity",
                        "LICENSE": b"project license",
                        "licenses/dependency/LICENSE": b"dependency license",
                    }
                    for name, content in contents.items():
                        path = artifact / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(content)
                    target = f"{board}-devkitc" if board.startswith("esp32") else board
                    expected[f"zephyrforge-firmware-v1.2.3-{target}-{storage}.zip"] = contents
            for name in ("zephyrforge-direct-v1.2.3.zip", "zephyrforge-host-v1.2.3-win-x64.zip",
                         "zephyrforge-host-v1.2.3-linux-x64.tar.gz", "zephyrforge-1.2.3-py3-none-any.whl"):
                (packages / name).write_bytes(b"host package fixture")
            result = subprocess.run(["bash", str(ROOT / "tools/release/archive.sh")],
                                    cwd=root, env={**os.environ, "VERSION": "1.2.3"},
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = {path.name for path in packages.glob("zephyrforge-firmware-*.zip")}
            self.assertEqual(actual, set(expected))
            for name, contents in expected.items():
                with zipfile.ZipFile(packages / name) as archive:
                    files = {item.filename: archive.read(item) for item in archive.infolist() if not item.is_dir()}
                    self.assertEqual(files, contents)
            checksums = {}
            for line in (packages / "SHA256SUMS.txt").read_text().splitlines():
                digest, name = line.split(maxsplit=1)
                checksums[name] = digest
            self.assertEqual(set(checksums), {path.name for path in packages.iterdir()} - {"SHA256SUMS.txt"})
            for name, digest in checksums.items():
                self.assertEqual(digest, hashlib.sha256((packages / name).read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
