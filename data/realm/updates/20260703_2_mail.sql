-- Mail system: persistent mails with money and item attachments.
-- Money and items stay attached to the mail until the recipient takes them at a mailbox.
CREATE TABLE `mail` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `sender` bigint unsigned NOT NULL,
  `sender_name` varchar(16) NOT NULL,
  `recipient` bigint unsigned NOT NULL,
  `subject` varchar(64) NOT NULL DEFAULT '',
  `body` varchar(512) NOT NULL DEFAULT '',
  `money` int unsigned NOT NULL DEFAULT 0,
  `cod` int unsigned NOT NULL DEFAULT 0,
  `sent_at` bigint unsigned NOT NULL,
  `expires_at` bigint unsigned NOT NULL,
  `read_flag` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_mail_recipient` (`recipient`)
) ENGINE=InnoDB;

CREATE TABLE `mail_items` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `mail_id` bigint unsigned NOT NULL,
  `entry` int unsigned NOT NULL,
  `count` smallint unsigned NOT NULL DEFAULT 1,
  `durability` smallint unsigned NOT NULL DEFAULT 0,
  `creator` bigint unsigned NOT NULL DEFAULT 0,
  `flags` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_mail_items_mail` (`mail_id`),
  CONSTRAINT `fk_mail_items_mail` FOREIGN KEY (`mail_id`) REFERENCES `mail` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB;
