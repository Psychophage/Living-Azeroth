-- Living Azeroth: additive native-action journal. Existing histories and API budgets are untouched.
CREATE TABLE IF NOT EXISTS `pbc_action` (
  `operation_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `run_id` varchar(64) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `scene_id` varchar(64) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `actor_guid` bigint unsigned NOT NULL,
  `requester_guid` bigint unsigned NOT NULL,
  `input_sequence` bigint unsigned NOT NULL,
  `intent_json` text NOT NULL,
  `status` enum('prepared','started','completed','failed','cancelled','unresolved') NOT NULL,
  `detail` varchar(255) NOT NULL DEFAULT '',
  `created_ms` bigint unsigned NOT NULL,
  `updated_ms` bigint unsigned NOT NULL,
  PRIMARY KEY (`operation_id`),
  KEY `actor_actions` (`actor_guid`,`updated_ms`),
  KEY `pending_actions` (`status`,`run_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
