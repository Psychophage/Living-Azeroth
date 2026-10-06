# Configuration

Everything that belongs to one realm lives in its **realm folder**, outside this
repository. The repository holds only source, defaults and documentation.

## The realm folder

| Path | What it is |
| --- | --- |
| `realm.conf` | your settings (edit this) |
| `secrets/` | `database-password`, optionally `model.key` (your API key), and `ledger-password` when the realm shares another realm's budget; readable only by you |
| `realm.state` | facts the tools keep: Docker project name, database volume, budget id |
| `config/` | server config files generated from `realm.conf` (do not edit; regenerated on start) |
| `prompts/` | character prompts staged for this realm (regenerated on start) |
| `logs/` | `Server.log`, `Auth.log` and the other server logs |
| `build/`, `install/` | the compiled server for this realm |
| `backups/` | database backups from `living-azeroth backup` |

The database itself lives in a Docker volume named in `realm.state`. Shared,
re-creatable files live in `~/.cache/living-azeroth/` (map data, compile cache), and
`~/.config/living-azeroth/default-realm` remembers which realm commands use when no
`--realm` is given.

## realm.conf

`realm.conf` starts as a copy of [`realm/realm.example.conf`](../realm/realm.example.conf),
which documents the recommended values. After editing it, run
`living-azeroth restart`.

- `[realm]`: the realm's name, the address and ports players connect to, the
  spending ceiling for a new budget (or `shared_budget` to spend from another
  realm's budget; see [Running](running.md#the-spending-budget)), and build parallelism.
- `[worldserver]`, `[authserver]`, `[playerbots]`, `[characters]`, `[auctionhouse]`,
  `[progression]`: any key from that server config file. Each file's `.conf.dist`
  in the source lists every key with a description.

Database connections, paths and the key file are always set by the tools; setting
them in `realm.conf` has no effect (the tools say so when they render the config).

## Secrets

- `secrets/database-password` is generated at setup and used only inside the realm's
  containers.
- `secrets/model.key` holds only your model provider's API key. Add or replace it
  at any time, then restart; remove it to switch conversations off.
- Account passwords are never stored; the database keeps only the login verifier.

## Features that are off by default

These are part of the server and can be switched on in `realm.conf`:

| Feature | Section and key | Notes |
| --- | --- | --- |
| Auction house bot | `[auctionhouse]` `AuctionHouseBot.EnableSeller`, `AuctionHouseBot.Buyer.Enabled` | needs seller characters; see [Auction house](features/auction-house.md) |
| Realm progression phases | `[progression]` `ProgressionSystem.Bracket_*` | phase changes are permanent; see [Progression](features/progression.md) |
| Original PBC runtime | `[characters]` `PBC.Enable` | recommended off; see [Characters](features/characters/README.md) |
