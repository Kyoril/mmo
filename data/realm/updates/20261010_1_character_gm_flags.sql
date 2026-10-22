-- GM observation: persistent game master state per character (bit 1 = invisible, bit 2 = godmode).
ALTER TABLE `characters`
ADD COLUMN `gm_flags` tinyint unsigned NOT NULL DEFAULT 0;
