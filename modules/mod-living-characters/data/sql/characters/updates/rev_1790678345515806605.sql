--
-- Living Azeroth: game-currency authorization is separate from the API budget.
CREATE TABLE IF NOT EXISTS `pbc_action_money_budget` (
  `id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `requester_guid` bigint unsigned NOT NULL,
  `limit_copper` bigint unsigned NOT NULL,
  `spent_copper` bigint unsigned NOT NULL DEFAULT 0,
  `held_copper` bigint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `pbc_action_money` (
  `operation_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `budget_id` varchar(96) CHARACTER SET `ascii` COLLATE `ascii_bin` NOT NULL,
  `reserved_copper` bigint unsigned NOT NULL,
  `spent_copper` bigint unsigned NOT NULL DEFAULT 0,
  `settled` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`operation_id`),
  KEY `budget_operations` (`budget_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
