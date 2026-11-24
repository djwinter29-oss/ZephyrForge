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
        self.write("zephyrforge/firmware/src/app/zephyrforge.h", '#define ZEPHYRFORGE_VERSION "0.1.0"\n')
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

    def package(self, board, target, storage="sdmmc"):
        package = self.repo / "build/firmware-package"
        if package.exists():
            shutil.rmtree(package)
        config = f'CONFIG_BOARD_TARGET="{target}"\n'
        if board == "esp32s3":
            config += "CONFIG_ESP_SIMPLE_BOOT=y\nCONFIG_FLASH_SIZE=16777216\n"
            self.write("firmware-build/zephyr/runners.yaml",
                       "--esp-app-address=0x0 --esp-flash-mode=dio --esp-flash-freq=80m --esp-flash-size=16MB")
        self.write("firmware-build/zephyr/.config", config)
        env = {**os.environ, "BOARD": board, "STORAGE": storage, "FIRMWARE_BUILD": str(self.build),
               "PATH": f"{self.workspace / 'bin'}:{os.environ['PATH']}"}
        result = subprocess.run(["bash", str(ROOT / "tools/release/package-firmware.sh")],
                                cwd=self.workspace, env=env, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return package

    def test_minimal_contents_for_each_board_and_storage(self):
        self.write("modules/hal/infineon/LICENSE", "radio license")
        resources = "modules/hal/infineon/zephyr/blobs/img/whd/resources"
        self.write(f"{resources}/firmware/COMPONENT_43439/43439A0.bin", "firmware")
        self.write(f"{resources}/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob", "clm")
        boards = {
            "esp32s3": "esp32s3_devkitc/esp32s3/procpu",
            "pico": "rpi_pico/rp2040",
            "pico_w": "rpi_pico/rp2040/w",
            "pico2": "rpi_pico2/rp2350a/m33",
            "pico2_w": "rpi_pico2/rp2350a/m33/w",
        }
        for board, target in boards.items():
            for storage in ("sdmmc", "sd-spi"):
                with self.subTest(board=board, storage=storage):
                    package = self.package(board, target, storage)
                    image = "zephyr.bin" if board == "esp32s3" else "zephyr.uf2"
                    self.assertEqual({path.name for path in package.iterdir()},
                                     {image, "BUILD-INFO.txt", "LICENSE", "licenses"})
                    self.assertEqual((package / image).read_text(), "fixture")
                    info = (package / "BUILD-INFO.txt").read_text()
                    for value in (target, storage, "fixture-commit", "0.1.0"):
                        self.assertIn(value, info)

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
            for board in ("esp32s3", "pico", "pico_w", "pico2", "pico2_w"):
                for storage in ("sdmmc", "sd-spi"):
                    artifact = root / f"build/firmware-packages/zephyrforge-{board}-{storage}-software-build"
                    image = "zephyr.bin" if board == "esp32s3" else "zephyr.uf2"
                    contents = {
                        image: f"{board}-{storage}".encode(),
                        "BUILD-INFO.txt": b"build identity",
                        "LICENSE": b"project license",
                        "licenses/dependency/LICENSE": b"dependency license",
                    }
                    for name, content in contents.items():
                        path = artifact / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(content)
                    target = "esp32s3-devkitc" if board == "esp32s3" else board
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
