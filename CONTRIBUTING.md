# Contributing

Start with the [architecture](docs/reference/architecture.md) and the guide for the component you are changing in
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
only when an interface or operating procedure changes.

Run `python3 tools/check_docs.py` and `git diff --check` after moving or editing documents.

<details>
<summary>Optional local branch cleanup</summary>

From the repository root:

```sh
./tools/host/remove-local-branches.sh
```

**Warning:** this immediately force-deletes every local branch except `main`, including unmerged branches.
There is no confirmation or `--apply` flag. Retain any needed commits before running; remote branches are untouched.

The script does not switch branches. If a non-main branch is checked out in any worktree, it stops before deleting
anything. Switch that worktree to `main` or detach it first.
Use `./tools/host/remove-local-branches.sh --help` to view usage without deleting branches.

</details>
