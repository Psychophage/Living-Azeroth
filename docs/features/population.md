# Population

The persistent population (`modules/mod-playerbots/src/Bot/Population/`) replaces
Playerbots' random roster with characters that persist and behave like a world's
residents. It makes no model calls.

- **Arrivals, not spawns.** Characters are created gradually up to a reserve
  (`ReserveTarget`, 2,000 by default) and log in over time up to `OnlineTarget` (500).
- **Places fill around players.** Each zone a player is in gets about `RegionTarget`
  seats (40 on this realm), filled over a minute and emptied over ten minutes after the
  player leaves; coming back before it empties finds the same people. Characters are
  the zone's level; capitals host all levels.
- **Familiar faces.** Characters a player sees and talks to earn familiarity and return
  more often; strangers fill the rest. Introduced characters are kept, and the least
  valued are retired only beyond `KnownLimit`.
- **Arrivals look natural.** Most arrive out of sight at inns and towns and walk in;
  young characters appear only near their race's start; crowds spread out.
- **Offline time.** Characters can stay active while nobody plays, within an allowance
  earned during play (`OfflineOnline`, 0 on this realm: nobody stays online).
- **City errands** (`AiPlayerbot.RpgCityErrands`): in capitals, characters visit a few
  NPCs further apart and stay a while, instead of hopping between neighbours.

Every setting is described under "Living Azeroth persistent population" in
`modules/mod-playerbots/conf/playerbots.conf.dist`; set them in the realm's
`[playerbots]` section. `.playerbots population` in game reports counts and health.
The policy's unit tests are in `modules/mod-playerbots/tests/population/`.
