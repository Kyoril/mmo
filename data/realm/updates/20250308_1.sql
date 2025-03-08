
CREATE TABLE `character_customization` (
  `character_id` bigint unsigned NOT NULL,
  `property_group_id` int unsigned NOT NULL,
  `property_value_id` int DEFAULT NULL,
  `scalar_value` float DEFAULT NULL,
  PRIMARY KEY (`character_id`,`property_group_id`),
  CONSTRAINT `customization_character_id_fk` FOREIGN KEY (`character_id`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
