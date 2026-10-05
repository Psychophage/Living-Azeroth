-- Living Azeroth population. Additive and safe to reapply; no character or API data is reset.
CREATE TABLE IF NOT EXISTS `playerbots_population_character` (
  `bot` INT UNSIGNED NOT NULL,
  `account` INT UNSIGNED NOT NULL,
  `planned_level` INT UNSIGNED NOT NULL DEFAULT 1,
  `initialized` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `introduced` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `map` INT UNSIGNED NOT NULL DEFAULT 0,
  `zone` INT UNSIGNED NOT NULL DEFAULT 0,
  `session_started` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `rest_until` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `last_seen` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`bot`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `playerbots_population_familiarity` (
  `human` INT UNSIGNED NOT NULL,
  `bot` INT UNSIGNED NOT NULL,
  `score` INT UNSIGNED NOT NULL DEFAULT 0,
  `sightings` INT UNSIGNED NOT NULL DEFAULT 0,
  `session_sightings` INT UNSIGNED NOT NULL DEFAULT 0,
  `interactions` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_seen` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `last_sighting` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `last_interaction` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `last_session` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`human`, `bot`),
  KEY `bot` (`bot`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `playerbots_population_world` (
  `id` TINYINT UNSIGNED NOT NULL,
  `offline_remaining` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `last_checkpoint` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `next_creation` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;
