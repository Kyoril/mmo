-- Bank system: number of purchased bank bag slots per character.
ALTER TABLE `characters`
ADD COLUMN `bank_bag_slots` tinyint unsigned NOT NULL DEFAULT 0 AFTER `money`;
