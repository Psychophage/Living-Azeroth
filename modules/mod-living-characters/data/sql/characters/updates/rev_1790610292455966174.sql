-- Living Azeroth character system. Additive: legacy PBC/Chatter history is retained.
-- Dollar amounts are integer nanodollars (USD * 1,000,000,000), never floating point.
CREATE TABLE IF NOT EXISTS `pbc_api_budget` (
  `budget_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `ceiling_nano` bigint unsigned NOT NULL,
  `spent_nano` bigint unsigned NOT NULL DEFAULT 0,
  `held_nano` bigint unsigned NOT NULL DEFAULT 0,
  `background_floor_nano` bigint unsigned NOT NULL DEFAULT 0,
  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`budget_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `pbc_api_request` (
  `request_id` varchar(64) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `budget_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `reason` varchar(32) NOT NULL,
  `actor_id` varchar(120) NOT NULL DEFAULT '',
  `scene_id` varchar(64) NOT NULL DEFAULT '',
  `model` varchar(160) NOT NULL,
  `provider` varchar(96) NOT NULL DEFAULT '',
  `map_id` int unsigned NOT NULL DEFAULT 0,
  `instance_id` int unsigned NOT NULL DEFAULT 0,
  `zone_id` int unsigned NOT NULL DEFAULT 0,
  `attempt` smallint unsigned NOT NULL DEFAULT 1,
  `held_nano` bigint unsigned NOT NULL,
  `actual_nano` bigint unsigned DEFAULT NULL,
  `state` enum('reserved','dispatched','reconciled','cancelled') NOT NULL DEFAULT 'reserved',
  `provider_request_id` varchar(160) NOT NULL DEFAULT '',
  `usage_json` json DEFAULT NULL,
  `latency_ms` int unsigned DEFAULT NULL,
  `delivery` varchar(32) NOT NULL DEFAULT 'pending',
  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`request_id`),
  KEY `budget_state` (`budget_id`,`state`),
  CONSTRAINT `pbc_request_budget` FOREIGN KEY (`budget_id`) REFERENCES `pbc_api_budget` (`budget_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pbc_actor` (
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `kind` enum('player','bot','named_npc','generic_npc','watch') NOT NULL,
  `owner_guid` bigint unsigned DEFAULT NULL,
  `display_name` varchar(120) NOT NULL,
  `version` bigint unsigned NOT NULL DEFAULT 1,
  `foundation` text NOT NULL,
  `summary` text NOT NULL,
  `recall` mediumtext NOT NULL,
  `recall_version` bigint unsigned NOT NULL DEFAULT 0,
  `last_observed_ms` bigint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`actor_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pbc_npc_identity` (
  `spawn_id` int unsigned NOT NULL,
  `map_id` int unsigned NOT NULL,
  `instance_id` int unsigned NOT NULL,
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `watch_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` DEFAULT NULL,
  PRIMARY KEY (`spawn_id`,`map_id`,`instance_id`),
  KEY `actor_id` (`actor_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `pbc_observation` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_key` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `scene_id` varchar(64) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `author_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `channel` varchar(32) NOT NULL,
  `map_id` int unsigned NOT NULL,
  `instance_id` int unsigned NOT NULL,
  `zone_id` int unsigned NOT NULL,
  `created_ms` bigint unsigned NOT NULL,
  `version` int unsigned NOT NULL DEFAULT 1,
  `evidence` enum('observed','delivered','legacy') NOT NULL,
  `payload` mediumtext NOT NULL,
  `excluded` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `event_key` (`event_key`),
  KEY `scene_id` (`scene_id`,`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pbc_witness` (
  `observation_id` bigint unsigned NOT NULL,
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `processed_version` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`actor_id`,`observation_id`),
  KEY `observation_id` (`observation_id`),
  CONSTRAINT `pbc_witness_source` FOREIGN KEY (`observation_id`) REFERENCES `pbc_observation` (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `pbc_note` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `operation_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `subject_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL DEFAULT '',
  `kind` enum('memory','relationship','commitment','report','correction','fact') NOT NULL,
  `content` text NOT NULL,
  `authority` enum('model','owner','canon') NOT NULL DEFAULT 'model',
  `version` int unsigned NOT NULL DEFAULT 1,
  `valid` tinyint unsigned NOT NULL DEFAULT 1,
  `resolved` tinyint unsigned NOT NULL DEFAULT 0,
  `compacted_version` bigint unsigned DEFAULT NULL,
  `created_ms` bigint unsigned NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `operation_actor` (`operation_id`,`actor_id`),
  KEY `actor_notes` (`actor_id`,`valid`,`id`),
  KEY `subject_id` (`subject_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pbc_note_source` (
  `note_id` bigint unsigned NOT NULL,
  `observation_id` bigint unsigned NOT NULL,
  `source_version` int unsigned NOT NULL,
  PRIMARY KEY (`note_id`,`observation_id`),
  KEY `observation_id` (`observation_id`),
  CONSTRAINT `pbc_note_source_note` FOREIGN KEY (`note_id`) REFERENCES `pbc_note` (`id`),
  CONSTRAINT `pbc_note_source_event` FOREIGN KEY (`observation_id`) REFERENCES `pbc_observation` (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `pbc_recall_note` (
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `recall_version` bigint unsigned NOT NULL,
  `note_id` bigint unsigned NOT NULL,
  `note_version` int unsigned NOT NULL,
  PRIMARY KEY (`actor_id`,`recall_version`,`note_id`),
  KEY `note_id` (`note_id`)
) ENGINE=InnoDB;

-- Pending sends survive a crash as ambiguous attempts; startup never replays them.
CREATE TABLE IF NOT EXISTS `pbc_delivery` (
  `event_key` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `actor_id` varchar(120) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `actor_version` bigint unsigned NOT NULL,
  `observation_json` json NOT NULL,
  `witnesses_json` json NOT NULL,
  `state` enum('prepared','committed','cancelled') NOT NULL DEFAULT 'prepared',
  `source_id` bigint unsigned DEFAULT NULL,
  PRIMARY KEY (`event_key`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `pbc_observation_revision` (
  `observation_id` bigint unsigned NOT NULL,
  `version` int unsigned NOT NULL,
  `payload` mediumtext NOT NULL,
  `excluded` tinyint unsigned NOT NULL,
  PRIMARY KEY (`observation_id`,`version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
