
CREATE TABLE IF NOT EXISTS `character_talents` (
  `character` bigint unsigned NOT NULL,
  `talent` int unsigned NOT NULL,
  `rank` tinyint unsigned NOT NULL,
  UNIQUE KEY `talent_character_unique_pair` (`character`,`talent`),
  KEY `talent_character_index` (`character`),
  KEY `talent_character_talent_index` (`talent`),
  CONSTRAINT `character_talent_fk` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
