# Version control policy

The repository tracks source code, build definitions, tests, scripts,
documentation, and the exact upstream `ldacBT` commit used by the project.

`vendor/ldacBT` is a Git submodule. Keep it pinned to a reviewed upstream
commit; do not copy its nested object database into the main repository.

The following data stays outside Git:

- all `build`, `artifacts`, `tmp`, and reference checkout directories;
- WDK/MSBuild binaries, symbols, catalogs, logs, and runtime state;
- test certificates and every private-key format;
- exported OEM driver packages and local rollback backups;
- the generated XM5 Container ID header;
- Codex and IDE workspace metadata.

Before each commit:

1. inspect `git status --short` and the staged diff;
2. run the tests appropriate to the changed layer;
3. never stage ignored artifacts with `git add -f`;
4. keep driver-install or system-state changes out of source commits;
5. record important true-device results in `STATUS.md` or
   `docs/DEVELOPMENT_HISTORY.md`, without attaching raw logs.

Branches that contain unsanitized development history must remain local or
private. The public branch is rebuilt from a sanitized root and must never
include ignored rollback packages, raw hardware evidence or private-key
material. Numeric `oem9xxx.inf` values in public tests are synthetic fixtures.
