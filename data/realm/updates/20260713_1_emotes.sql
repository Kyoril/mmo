-- Emote system: persisted mood and per-context pose selections plus unlocked emotes.
ALTER TABLE `characters`
ADD COLUMN `mood_emote` int unsigned NOT NULL DEFAULT 0,
ADD COLUMN `idle_pose` int unsigned NOT NULL DEFAULT 0,
ADD COLUMN `sit_pose` int unsigned NOT NULL DEFAULT 0,
ADD COLUMN `sleep_pose` int unsigned NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS `character_emotes` (
  `character` bigint unsigned NOT NULL,
  `emote` int unsigned NOT NULL,
  PRIMARY KEY (`character`,`emote`),
  CONSTRAINT `fk_emote_character_id` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
