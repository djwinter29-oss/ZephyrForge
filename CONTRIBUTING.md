# Contributing

Start with the [architecture](docs/development/architecture.md) and the guide for the component you are changing in
the [documentation index](docs/README.md). Keep changes focused. Preserve board pin/memory constraints and
no-format storage behavior; update all affected hosts when a protocol changes.

Run [Ceedling unit tests and firmware build checks](docs/development/firmware.md) and
[host checks](docs/development/host-tools.md) for affected code.
Pull requests use the same reusable [GitHub validation workflows](docs/development/releases.md#github-workflows).
Keep CI and release builds on the same pinned dependencies; do not bypass release validation.
State whether results came from simulated devices or physical hardware. Hardware access is not required for
software contributions; it is required for hardware qualification claims.

Do not commit credentials, local environments, generated builds, or personal device data.
See [security reporting](SECURITY.md) for sensitive reports.

## Documentation changes

Put each contract or procedure in one place and link to it. Keep guides task-oriented, references precise, and
dated validation results unchanged except for link maintenance. Internal refactors need documentation changes
only when an interface or operating procedure changes. Write for the reader's task: explain why a constraint
matters, state each limitation in its authoritative home, and avoid repeated closing disclaimers.

Choose the content's home before adding a page:

- `docs/getting-started.md`: one first-run path, not an inventory of every option.
- `docs/hardware/`: board identity, connectors, wiring, and physical commissioning.
- `docs/guides/`: operating tasks; dashboard guides share controls and recovery in their landing page.
- `docs/development/`: architecture, builds, extensions, software checks, and releases.
- `docs/reference/`: exact protocol, API, persistence, storage, and security contracts.
- `docs/validation/`: current coverage and test procedures; dated observations stay under `records/`.

Keep component READMEs as entry points to these pages rather than second copies of installation or API guides.
Add a new task to the documentation index, and update incoming links when moving its page.

Run `python3 tools/check_docs.py` and `git diff --check` after moving or editing documents.

## Repository maintenance

`./tools/host/remove-local-branches.sh --help` shows usage. Running the script force-deletes every local branch
except `main`, including unmerged branches. It does not touch remote branches and stops if another worktree has a
non-main branch checked out. Retain needed commits and move affected worktrees before running it.
