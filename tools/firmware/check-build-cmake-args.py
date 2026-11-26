#!/usr/bin/env python3
"""Reject west CMake defaults that can override the firmware wrapper's build contract."""

import fnmatch
import shlex
import sys
from pathlib import Path


PROTECTED = {
    "BOARD",
    "CONFIG_ZF_BLUETOOTH",
    "CONFIG_ZF_STORAGE",
    "CONFIG_ZF_STORAGE_BENCHMARK",
    "CONFIG_ZF_TCP_SHELL",
    "CONFIG_ZF_USB_HID",
    "CONFIG_ZF_USB_MSC",
    "CONFIG_ZF_WIFI",
    "CMAKE_ASM_COMPILER",
    "CMAKE_C_COMPILER",
    "CMAKE_CXX_COMPILER",
    "CMAKE_LINKER",
    "CMAKE_SYSROOT",
    "CMAKE_TOOLCHAIN_FILE",
    "CONF_FILE",
    "CROSS_COMPILE",
    "CROSS_COMPILE_TOOLCHAIN_PATH",
    "DTC_OVERLAY_FILE",
    "TOOLCHAIN_ROOT",
    "Zephyr_DIR",
    "Zephyr-sdk_DIR",
    "ZEPHYR_BASE",
    "ZEPHYR_SDK_INSTALL_DIR",
    "ZEPHYR_TOOLCHAIN_VARIANT",
    "ZEPHYRFORGE_VERSION",
}
PROJECT_VARIABLES = (
    "CMAKE_PROJECT_INCLUDE",
    "CMAKE_PROJECT_INCLUDE_BEFORE",
    "CMAKE_PROJECT_ZephyrForge_INCLUDE",
)


def validate_arguments(arguments):
    """Reject CMake arguments that supersede wrapper-owned configuration."""
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument in ("-C", "--toolchain") or argument.startswith(("--toolchain=", "-C")):
            raise ValueError("must not select a toolchain or preload file")
        if argument in ("-D", "-U"):
            index += 1
            setting = arguments[index] if index < len(arguments) else ""
        elif argument.startswith(("-D", "-U")):
            setting = argument[2:]
        else:
            index += 1
            continue

        is_unset = argument == "-U" or argument.startswith("-U")
        name = setting.split("=", 1)[0]
        if not is_unset:
            name = name.split(":", 1)[0]
        protected = next((item for item in PROTECTED if fnmatch.fnmatchcase(item, name)), None)
        project_variable = name.startswith("CMAKE_PROJECT_") or any(
            fnmatch.fnmatchcase(variable, name) if is_unset else False
            for variable in PROJECT_VARIABLES
        )
        if protected is not None:
            raise ValueError(f"must not override wrapper-owned CMake setting {protected}")
        if project_variable:
            raise ValueError("must not inject or unset CMAKE_PROJECT_* variables")
        index += 1


def main():
    from west.configuration import ConfigFile, Configuration

    config = Configuration(topdir=Path(sys.argv[1]))
    configured = config.get("build.cmake-args", default="", configfile=ConfigFile.ALL) or ""
    try:
        validate_arguments(shlex.split(configured))
    except ValueError as exc:
        raise SystemExit(f"Error: west build.cmake-args {exc}.") from exc


if __name__ == "__main__":
    main()
