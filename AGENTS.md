# AGENTS.md

Living Azeroth: an AzerothCore (Playerbot branch) server with bundled modules, run
through `./living-azeroth`. Read `README.md`, then the relevant page in `docs/`.

## Rules

- Use the realm machinery for everything: `./living-azeroth --realm <test realm> build|test|update`.
  Never build or start servers another way, and never touch a realm you were not asked to.
- A realm's folder (settings, secrets, logs, data) never enters the repository.
- Never reset, recreate or raise a spending budget (`pbc_api_budget`). Paid model calls
  spend real money; make them only when asked.
- Gameplay changes in a module are opt-in through its `.conf.dist` (default off),
  documented next to the setting and read once at startup.
- Core edits stay small and carry a `Living Azeroth:` comment. Never edit
  `data/sql/base/`, `data/sql/archive/` or `data/sql/updates/db_*/`; database changes
  are additive files in the owning module's `data/sql/.../updates/`.
- Formatting follows `.editorconfig`: UTF-8, LF, 120 columns, 4-space C++ indent.
- Keep documentation in `docs/`, short and current. Update the existing page rather
  than adding one.
- Credit upstream authors: code mirrored from another project is committed with the
  original author (see `NOTICE.md` for the bundled projects).
