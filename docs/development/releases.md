# Release procedure

[Documentation](../README.md) / Development / Releases

Use this guide to publish packages. For installation, see [Flashing](../guides/flashing.md).
Complete [one-time publisher setup](#before-the-first-release) for the channels you use, then follow the
stable-tag checklist below.

- [Publish a stable tag](#publish-a-stable-tag)
- [Inspect release assets](#release-assets)
- [Understand publication and retries](#direct-pages-pypi-and-retries)
- [Use packaging helpers locally](#package-and-publication-helpers)

## Publish a stable tag

1. On the intended source ref, run [Release build packages](../../.github/workflows/release.yml) manually with
   the proposed tag, for example `v0.1.0`. The preflight builds and tests packages and attaches artifacts.
   Creating the tag is a separate step.
2. Inspect the artifacts, dependency license notices, and [hardware qualification limits](../validation/README.md).
   Review radio and regulatory requirements before distributing radio-enabled binaries. Confirm preflight and
   PR checks passed on the exact commit you plan to tag.
3. Check out that commit, then create and push the tag. Use `vMAJOR.MINOR.PATCH` with components from 0 to 255
   and no leading zeros, as enforced by the [version helper](../../tools/firmware/release-version.sh).

   ```sh
   git tag v0.1.0
   git push origin v0.1.0
   ```

4. Check the published [release assets](#release-assets) and `SHA256SUMS.txt`. Verify that the release links to
   flashing, wiring, and qualification documentation at its source tag. Check
   [Direct Pages and PyPI](#direct-pages-pypi-and-retries) if those channels are configured.

Release and preflight builds use the validated version for firmware (`ZEPHYRFORGE_VERSION`), the .NET packages,
Direct, and the Python wheel. Package smoke checks verify the resulting identity before publication.
For source-checkout version derivation, see [host development](host-tools.md).

## Direct Pages, PyPI and retries

After a successful tag-triggered release, Pages downloads the **published release asset**, not
an untrusted PR artifact or a fresh build from `main`. It verifies the tag's source commit and
the Direct checksum and archive version/source metadata. The archive's `web/` directory,
with the project license, is served as the site root.

After the same release, **Publish Python to PyPI** downloads the released wheel, checks it against
`SHA256SUMS.txt` and the tag's source commit, and uploads that exact file. PyPI versions are immutable:
rerun only failed jobs after fixing external configuration, and publish corrections under a new version.

A manual **Publish Direct Pages** or **Publish Python to PyPI** run can republish an existing,
non-draft, non-prerelease tag.
A manual **Publish .NET Packages** run rebuilds an existing tag and attaches run artifacts;
it does not modify an existing GitHub release. A Pages or PyPI setup/deployment failure does not undo
the separately completed GitHub release.

## Before the first release

1. Enable GitHub Actions and allow the project's pinned actions in the repository or organization policy.
2. For Direct, set **Settings > Pages > Build and deployment > Source** to **GitHub Actions** and review
   protection rules for the `github-pages` environment.
3. For Python, configure a PyPI Trusted Publisher or pending publisher for project `zephyrforge`, owner
   `djwinter29-oss`, repository `ZephyrForge`, workflow `publish-pypi.yml`, and environment `pypi`.
   Confirm project-name availability for new publishers.
4. Create the GitHub `pypi` environment, restrict it to the default branch, and configure any required
   reviewer approval. Trusted Publishing uses OIDC.

## Release assets

| Asset | Contents/use |
| --- | --- |
| Firmware ZIPs | [Filename patterns and board/storage identifiers](../guides/flashing.md#flash-a-release-image) |
| `zephyrforge-direct-vVERSION.zip` | Independent Direct `web/` app, `server.py`, project license and `RELEASE.txt` source metadata; run `python3 server.py` |
| `zephyrforge-VERSION-py3-none-any.whl` | Python host package with the `zephyrforge` command, bundled `host/web/src` bridge dashboard and license; also published to PyPI |
| `zephyrforge-host-vVERSION-linux-x64.tar.gz` | Self-contained Linux .NET host, dashboard assets and dependency notices |
| `zephyrforge-host-vVERSION-win-x64.zip` | Self-contained Windows .NET host |
| `zephyrforge-host-vVERSION-osx-arm64.tar.gz` | Self-contained Apple Silicon .NET host |
| `SHA256SUMS.txt` | SHA-256 of every package |

Firmware packages contain the flashable image, `zephyr.elf` from the same build, `BUILD-INFO.txt`, `LICENSE` and
`licenses/`. The ELF carries symbols and load addresses: use it with a debug probe, `picotool`, GDB or to decode
crash addresses. ESP32 boards are flashed from `zephyr.bin`, not the ELF.
Packages omit `FLASHING.txt`, the documentation tree, Zephyr configuration, devicetree, and runner/dependency YAML
files.
`BUILD-INFO.txt` identifies the build-time firmware version, source commit and dirty state, target board, storage
variant, and the SHA-256 of the image and ELF. W firmware packages include Infineon HAL and blob license notices.
`BUILD-INFO.txt` also records SHA-256 hashes of the embedded CYW43439 Wi-Fi firmware and CLM blob.
These images provide Wi-Fi only; a release build does not qualify the radio hardware.

For host installation, see [Python](../guides/dashboards/python.md#install),
[.NET](../guides/dashboards/dotnet.md#run-the-host), and
[Direct](../guides/dashboards/direct.md#choose-how-to-run-it). Windows and macOS packages ship without publisher
signatures or notarization. Local OS security policy may require explicit approval.

## GitHub workflows

PR checks validate the source; release jobs package it; publication jobs deploy the verified release assets.

| Workflow | Trigger | Responsibility |
| --- | --- | --- |
| [PR Check](../../.github/workflows/pr-check.yml) | Pull requests, pushes to `main`, manual | Calls shared host validation and the firmware target builds |
| [Host Validation](../../.github/workflows/host-validation.yml) | Reusable, manual | Python 3.10/3.12 on Linux/macOS/Windows; Node 22 and .NET 10 on each OS; Ceedling; documentation links |
| [Firmware build](../../.github/workflows/firmware-build.yml) | Reusable, manual | Pinned Zephyr/SDK build of every board and storage image, generated configuration checks, flashable images, build identity and licenses |
| [Release build packages](../../.github/workflows/release.yml) | Stable version tags, manual preflight | Gates packages on shared validation and publishes checksummed GitHub release assets |
| [Publish .NET Packages](../../.github/workflows/publish-dotnet.yml) | Reusable, manual existing tag | Self-contained `linux-x64`, `win-x64`, `osx-arm64` archives; extracted-host smoke tests without hardware |
| [Publish Direct Pages](../../.github/workflows/publish-direct.yml) | Successful tagged release, manual published tag | Verifies the released Direct ZIP/checksum and source tag before deploying to Pages |
| [Publish Python to PyPI](../../.github/workflows/publish-pypi.yml) | Successful tagged release, manual published tag | Verifies the released wheel's checksum and source tag, then publishes it with Trusted Publishing |

Actions are pinned to commit SHAs. CI has read-only repository access; only the release job
can publish release assets, and only Pages and PyPI deployment get OIDC write permissions.
SDK release discovery uses the workflow's read-only GitHub token to avoid anonymous API rate limits.
PR checks cancel superseded runs; release/publication runs are not canceled mid-flight.
Host Validation runs the firmware tooling suite, including mocked workspace, build-wrapper, packaging and archive
checks on Linux. Release builds require these checks to pass. No workflow flashes hardware or tests an SD card.

## Package and publication helpers

Workflows call the scripts in [tools/release](../../tools/release) for packaging, archive smoke checks and release
downloads. Run them from the repository root with the same prerequisites and environment as the corresponding
workflow; firmware collection instead runs from the Zephyr workspace root with the checkout named `zephyrforge`.
The workflows retain triggers, permissions, tool installation, build commands, artifact transfer and publication
actions.

| Script | Required environment and inputs |
| --- | --- |
| [build-wheel.sh](../../tools/release/build-wheel.sh) | `SETUPTOOLS_SCM_PRETEND_VERSION`; Python/pip. Stages assets and builds into `dist/`. |
| [check-wheel.sh](../../tools/release/check-wheel.sh) | `VERSION`; wheel in `dist/`. Installs into `build/wheel-check` and runs the installed-wheel check. |
| [package-direct.py](../../tools/release/package-direct.py) | `VERSION`, `SOURCE_SHA`; Git checkout. Builds and checks the extracted Direct ZIP. |
| [package-dotnet.py](../../tools/release/package-dotnet.py) | `VERSION`, `RID`; self-contained publish in `build/host-release` and restored packages in `build/nuget`. |
| [package-firmware.sh](../../tools/release/package-firmware.sh) | `STORAGE`, `FIRMWARE_BUILD`; completed build and its identity snapshot. `BOARD` selects `esp32s3` (default), `esp32c6`, `pico`, `pico_w`, `pico2` or `pico2_w`. |
| [archive.sh](../../tools/release/archive.sh) | `VERSION`; downloaded firmware artifacts in `build/firmware-packages` and host packages in `build/packages`. |
| [download-direct.sh](../../tools/release/download-direct.sh) | `RELEASE_TAG`, `SOURCE_SHA`, `GH_TOKEN`, `GH_REPO`; fetches and verifies the released site for Pages. |
| [download-wheel.sh](../../tools/release/download-wheel.sh) | `RELEASE_TAG`, `SOURCE_SHA`, `GH_TOKEN`; fetches and verifies the released wheel for PyPI. |

Invoke Python scripts with `python` and shell scripts with `bash`. Use fresh staging/output directories, as in CI.
`SOURCE_SHA` may be empty for manual publication downloads; tag and checksum checks still apply.
The download helpers do not publish anything themselves. Package smoke checks start only local loopback hosts.

### Reproduce the Python release wheel

Use a fresh checkout of the release commit and an active Python 3.12 venv. The build helper stages the license
and shared dashboard in the package source, then writes the wheel to `dist/`. The check helper installs it
into `build/wheel-check` and tests the installed package with the expected version.

```sh
VERSION=$(bash tools/firmware/release-version.sh v0.1.0)
SETUPTOOLS_SCM_PRETEND_VERSION="$VERSION" bash tools/release/build-wheel.sh
VERSION="$VERSION" bash tools/release/check-wheel.sh
```
