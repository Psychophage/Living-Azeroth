# Progression

The [Progression System](https://github.com/azerothcore/mod-progression-system)
(`modules/mod-progression-system`) can run the realm through the eras in order:
Vanilla brackets, then The Burning Crusade, then Wrath of the Lich King. Each bracket
loads its scripts and applies its database changes (content gating, loot, vendors,
attunements).

On this realm every bracket is off, so all Wrath of the Lich King content is open.

To run an earlier era, enable the brackets up to that point in the realm's
`[progression]` section (`ProgressionSystem.Bracket_0`, `Bracket_1_19`, ... as listed in
`modules/mod-progression-system/conf/progression_system.conf.dist`), set
`Expansion` and `MaxPlayerLevel` in `[worldserver]` and `AiPlayerbot.RandomBotMaxLevel`
in `[playerbots]` to match, then restart.

**Bracket changes are permanent.** A bracket's database changes are not undone by
switching it off again; only a later bracket's "down" files or a fresh world database
reverse them. Back up first (`living-azeroth backup`).
