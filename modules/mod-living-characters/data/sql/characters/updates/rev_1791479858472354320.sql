-- Living Azeroth: a player guild's own identity, written by its officers (see pbc_guild.h).
CREATE TABLE IF NOT EXISTS `pbc_guild_identity` (
  `guild_id` int unsigned NOT NULL,
  `identity` varchar(4096) NOT NULL,
  `updated_by` int unsigned NOT NULL,
  `updated_ms` bigint unsigned NOT NULL,
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
