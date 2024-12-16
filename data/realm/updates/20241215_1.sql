
CREATE TABLE `character_actions` (
  `character_id` bigint unsigned NOT NULL,
  `button` tinyint(3) unsigned NOT NULL,
  `action` smallint(5) unsigned NOT NULL,
  `type` tinyint(3) unsigned NOT NULL,
  PRIMARY KEY (`character_id`,`button`),
  CONSTRAINT `character_actions_ibfk_1` FOREIGN KEY (`character_id`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
