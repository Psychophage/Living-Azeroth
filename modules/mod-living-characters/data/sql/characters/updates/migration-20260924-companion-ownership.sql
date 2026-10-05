-- Durable claim for bots admitted to PBC conversational context.
-- History, memories, and relationships are mutable content, not ownership.
CREATE TABLE IF NOT EXISTS `mod_pbc_companion_ownership` (
    `bot_guid` BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    `acquired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Bot identities whose conversation belongs to PBC';

-- Historical recovery for the installed roaming-bot pool. Real-player history
-- owners are excluded by account prefix. An account-owned bot cannot be
-- reconstructed reliably from old content; new admissions are claimed live.
INSERT IGNORE INTO `mod_pbc_companion_ownership` (`bot_guid`)
SELECT DISTINCT evidence.bot_guid
FROM (
    SELECT `guid` AS bot_guid FROM `mod_pbc_history_owners`
    UNION
    SELECT `bot_guid` FROM `mod_pbc_memories`
    UNION
    SELECT `bot_guid` FROM `mod_pbc_relationships`
) AS evidence
INNER JOIN `characters` AS c ON c.`guid` = evidence.bot_guid
INNER JOIN `acore_auth`.`account` AS a ON a.`id` = c.`account`
WHERE a.`username` LIKE 'RNDBOT%';
