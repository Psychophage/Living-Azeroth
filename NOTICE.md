# Credits and licensing

Living Azeroth combines several open-source projects with its own changes. Each part
keeps its original notices, authors and licence files.

| Part | Upstream source | Imported at | Licence |
| --- | --- | --- | --- |
| Server core (`src/`, `deps/`, `data/`, build files) | [AzerothCore, Playerbot branch](https://github.com/mod-playerbots/azerothcore-wotlk) | `7f12e89` | GPL-2.0-or-later ([text](LICENSES/GPL-2.0-AzerothCore.txt), [authors](LICENSES/AzerothCore-AUTHORS)) |
| `modules/mod-playerbots` | [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) | `7bae1b5c` | GPL-2.0-or-later |
| `modules/mod-living-characters` | [PBC (mod-pbc)](https://github.com/deseven/mod-pbc) by deseven | `ed15d3c` | Original PBC: Unlicense ([text](modules/mod-living-characters/LICENSES/PBC-Unlicense.txt)); bundled libraries: MIT ([text](modules/mod-living-characters/LICENSES/MIT-dependencies.txt)) |
| `modules/mod-ah-bot-plus` | [AHBot Plus](https://github.com/NathanHandley/mod-ah-bot-plus) by Nathan Handley | `f685832` | GPL-2.0-or-later (file headers) |
| `modules/mod-progression-system` | [Progression System](https://github.com/azerothcore/mod-progression-system) | `ae6a53c` | AGPL-3.0 |
| `modules/mod-bot-control` | Living Azeroth's own | — | GPL-2.0-or-later |

Each upstream project was imported as one snapshot commit at the revision shown,
keeping the parts Living Azeroth uses (source, build files, database scripts and
configuration; not upstream CI, editor, installer or contributor files). Its own
history stays in its own repository. Living Azeroth's changes follow the imports.

## The combined work

The repository as a whole is distributed under the
[GNU Affero General Public License v3.0](LICENSE). The GPL-2.0-or-later parts are
used under version 3, which can be combined with the AGPL-3.0 Progression System.
In practice: if you run a modified copy as a server that other people play on, you
must offer them the source code of that copy.

Living Azeroth's own changes are released under GPL-2.0-or-later where they sit in
GPL-2.0-or-later files, so they can flow back to those upstream projects.

Game client files, extracted client data, databases and model weights are not part
of this repository and are not covered by these licences.
