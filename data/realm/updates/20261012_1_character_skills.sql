-- Professions: per-character trade skill values and caps.
CREATE TABLE IF NOT EXISTS `character_skills` (
  `character` bigint unsigned NOT NULL,
  `skill` int unsigned NOT NULL,
  `value` smallint unsigned NOT NULL DEFAULT 1,
  `max` smallint unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`character`,`skill`),
  CONSTRAINT `fk_skill_character_id` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
