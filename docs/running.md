# Running a realm

Run these from the repository folder. Add `--realm FOLDER` to choose a realm other
than the default one.

| Task | Command |
| --- | --- |
| Start | `./living-azeroth start` |
| Follow the server log | `./living-azeroth logs` (Ctrl+C stops watching; the realm keeps running) |
| Check it is running | `./living-azeroth status` |
| Stop | `./living-azeroth stop` (characters and data are kept) |
| Apply `realm.conf` changes | `./living-azeroth restart` |
| Add an account, or change a password | `./living-azeroth account NAME` (add `--gm 3` for an administrator) |
| See conversation spending | `./living-azeroth budget` |
| Back up the databases | `./living-azeroth backup` |

The server is ready for logins when `ready...` appears in the log.

## Updating

After pulling new source (`git pull`), run:

```bash
./living-azeroth update
```

It compiles while the realm keeps running, then stops it, installs the new build,
applies new database updates and starts it again.

## The spending budget

Conversations cost money when they use a paid model. Each request is reserved
against the realm's budget before it is sent, and settled with the actual cost
afterwards. When the budget is used up, characters stop generating new dialogue;
the rest of the game is unaffected. The ceiling is set once at setup
(`budget_dollars` in `realm.conf`) and is never raised or reset by the tools.

## Troubleshooting

- **Nobody can log in:** wait for `ready...` in the log; check `status`. The realm
  list address must be one the client can reach (`address` in `realm.conf`).
- **The server stopped unexpectedly:** copy `logs/` from the realm folder before
  restarting, which starts new logs. Linux keeps crash dumps (`coredumpctl list`).
- **A build runs out of memory:** lower `build_jobs` in `realm.conf`.
- **Population numbers look wrong:** the server log's `Population place` lines (debug
  level) are the truth; the database's `online` and `zone` columns lag behind.
