
DROP TABLE IF EXISTS `debug`;

CREATE TABLE `account_ban_history` (
  `account_id` bigint unsigned NOT NULL,
  `timestamp` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `expiration` datetime DEFAULT NULL,
  `reason` varchar(256) COLLATE latin1_german1_ci DEFAULT NULL,
  `banned` tinyint DEFAULT NULL,
  KEY `fk_account_ban_history_account_id_idx` (`account_id`),
  CONSTRAINT `fk_account_ban_history_account_id` FOREIGN KEY (`account_id`) REFERENCES `account` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;