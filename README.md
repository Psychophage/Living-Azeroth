# Living Azeroth

A World of Warcraft 3.3.5a (Wrath of the Lich King) server where the world feels
inhabited. It is built on [AzerothCore](https://www.azerothcore.org/) and
[Playerbots](https://github.com/mod-playerbots/mod-playerbots), with:

- **A persistent population.** Bot characters arrive gradually, gather where players
  are, remember who they have met, and stay recognisable across sessions.
- **Characters you can talk to.** Companions and named NPCs answer in party chat,
  whispers and local speech, remember what happened, and act on natural requests
  through the normal game rules. This uses a language model you choose, with a hard
  spending limit.
- **Optional auction house bot and realm progression**, off by default.

It runs on Linux with Docker. It is a personal project shared as-is, and so far it has
been played on one machine rather than as a public server.

## Quick start

```bash
git clone https://github.com/Psychophage/Living-Azeroth.git
cd Living-Azeroth
./living-azeroth setup --realm ~/living-azeroth-realm
./living-azeroth start
```

Then point a 3.3.5a client at `127.0.0.1`. The [setup guide](docs/setup.md) covers
requirements and details; everything else is in [the documentation](docs/README.md).

## Layout

| Path | What it is |
| --- | --- |
| `living-azeroth` | the one command for setting up and running realms |
| `realm/` | the machinery behind it: toolchain image, services, settings template |
| `docs/` | documentation |
| `modules/` | Playerbots, the character system, auction house bot, progression system |
| `src/`, `deps/`, `data/` | AzerothCore server source and database scripts |

A realm's settings, secrets, logs and data live in its own folder, outside this
repository.

## Credits and licence

Living Azeroth stands on the work of the AzerothCore, Playerbots, PBC, AHBot Plus and
Progression System authors; see [NOTICE.md](NOTICE.md). The combined work is
licensed under the [AGPL-3.0](LICENSE).
