# Development

## Where things are

| Path | What it is |
| --- | --- |
| `src/`, `deps/`, `data/sql/`, `CMakeLists.txt` | AzerothCore (Playerbot branch). Living Azeroth's own edits are few and marked `Living Azeroth:` |
| `modules/mod-playerbots/` | bot AI; Living Azeroth adds the persistent population (`src/Bot/Population/`), conversation actions (`src/Bot/PlayerbotDialogue*`) and behaviour fixes |
| `modules/mod-living-characters/` | the character system (`src/character/`, `src/runtime/`, `src/llm/`) and the original PBC runtime it grew from |
| `modules/mod-ah-bot-plus/`, `modules/mod-progression-system/` | auction house bot and realm progression |
| `realm/` | the realm machinery: toolchain `Dockerfile`, `compose.yaml`, `build.sh`, the settings template and the Python behind `living-azeroth` (`realm/tool/`) |
| `docs/` | this documentation |

The core hooks are: a notification after chat is delivered, a marker when scripted
NPC speech plays, a shared repair-price function, equipment-set access and a channel
mute check.

## Build and test

Development uses the same machinery as playing. Use a separate realm for testing so
your playing realm is never disturbed:

```bash
./living-azeroth setup --realm ~/living-azeroth-test --auth-port 13724 --world-port 18085
./living-azeroth --realm ~/living-azeroth-test build      # compile only
./living-azeroth --realm ~/living-azeroth-test test       # unit tests
./living-azeroth --realm ~/living-azeroth-test update     # compile, install, restart
```

Builds use ccache (shared in `~/.cache/living-azeroth/ccache`), so building another
realm is quick. `LA_SANITIZE=address ./living-azeroth build worldserver` compiles an
AddressSanitizer server into the realm's `build-address/` for hunting memory errors.

## New behaviour

- Gameplay changes in a module are switched on by a setting in its `.conf.dist`,
  off by default, read once at startup and described next to the setting.
- Prefer module code and hooks to core edits; keep any core edit small and marked.
- Database changes are additive files in the owning module's `data/sql/.../updates/`.
- Settings a realm needs go in `realm/realm.example.conf`; paths, connections and
  secrets are set by `realm/tool/config.py`, never by hand.
- Keys, passwords and realm data never enter the repository.

## Taking upstream updates

Each upstream project was imported once (see [NOTICE.md](../NOTICE.md) for sources and
revisions), keeping only the parts Living Azeroth uses. To move one forward, apply
upstream's own changes between the imported and the new revision, limited to those
parts:

```bash
# AzerothCore (Playerbot branch)
git fetch https://github.com/mod-playerbots/azerothcore-wotlk.git Playerbot
git diff OLD NEW -- src deps data/sql CMakeLists.txt PreLoad.cmake conf/dist/config.cmake \
    modules/CMakeLists.txt modules/ModulesLoader.cpp.in.cmake modules/ModulesPCH.h \
    modules/ModulesScriptLoader.h ':!data/sql/old' | git apply -3

# a module, e.g. mod-playerbots
git fetch https://github.com/mod-playerbots/mod-playerbots.git master
git diff OLD NEW -- src conf/playerbots.conf.dist data tests mod-playerbots.cmake \
    | git apply -3 --directory=modules/mod-playerbots
```

Resolve conflicts, update a test realm, play-check it, update the revision in
`NOTICE.md`, and commit as "Update mod-playerbots to NEW".
