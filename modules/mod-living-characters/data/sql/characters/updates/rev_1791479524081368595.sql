-- Living Azeroth: what each player chooses to hear from characters (see pbc_listener.h).
-- A missing row means the realm's own settings.
CREATE TABLE IF NOT EXISTS `pbc_listener` (
  `player_guid` int unsigned NOT NULL,
  `settings` varchar(1024) NOT NULL,
  `updated_ms` bigint unsigned NOT NULL,
  PRIMARY KEY (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
