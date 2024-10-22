
DROP TABLE IF EXISTS `character_items`;

CREATE TABLE `character_items` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT COMMENT 'Unique id of this item on this realm.',
  `owner` bigint unsigned NOT NULL COMMENT 'GUID of the character who owns this item.',
  `entry` int(10) unsigned NOT NULL COMMENT 'Entry of the item template.',
  `slot` smallint(5) unsigned NOT NULL COMMENT 'Slot of this item.',
  `creator` bigint unsigned DEFAULT NULL COMMENT 'GUID of the creator of this item (may be 0 if no creator information available)',
  `count` smallint(5) unsigned NOT NULL DEFAULT '1' COMMENT 'Number of items',
  `durability` smallint(5) unsigned NOT NULL DEFAULT '0' COMMENT 'Item''s durability (if this item has any durability).',
  PRIMARY KEY (`id`),
  KEY `owner_field` (`owner`),
  KEY `creator_field` (`creator`),
  CONSTRAINT `characters_items_ibfk_1` FOREIGN KEY (`owner`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `characters_items_ibfk_2` FOREIGN KEY (`creator`) REFERENCES `characters` (`id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;