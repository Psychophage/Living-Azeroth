-- Living Azeroth trade shares: what each bot got while grouped with each player, and so hands back without
-- payment. Entry 0 holds gold (copper). Additive and safe to reapply.
CREATE TABLE IF NOT EXISTS `playerbots_trade_share` (
  `bot` INT UNSIGNED NOT NULL,
  `human` INT UNSIGNED NOT NULL,
  `entry` INT UNSIGNED NOT NULL,
  `amount` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`bot`, `human`, `entry`)
) ENGINE=InnoDB;
