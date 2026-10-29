-- Data-driven attributes
--
-- Attributes are game data now (attributes.proto), so attribute spending is stored per attribute id
-- instead of in five fixed columns. `attr_layout` records which attribute set the points were spent
-- under (mmo::ComputeAttributeLayoutHash); when the attribute set changes, the world server refunds
-- the spending of that class on login so the points can be spent again.
--
-- Additive: `character_classes`.`attr_0` .. `attr_4` stay and are still written for attribute ids
-- 0 - 4, so a rolled back server build keeps working. A row the old build saved has `attr_layout` 0
-- again; the new build then reads that row's spending from the legacy columns instead of the
-- (stale) `character_class_attributes` rows. Drop the columns in a later release.

CREATE TABLE IF NOT EXISTS `character_class_attributes` (
  `character` bigint unsigned NOT NULL,
  `class`     int unsigned NOT NULL,
  `attribute` int unsigned NOT NULL,
  `points`    int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`character`, `class`, `attribute`),
  CONSTRAINT `fk_character_class_attributes_character` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

ALTER TABLE `character_classes` ADD COLUMN `attr_layout` int unsigned NOT NULL DEFAULT 0 AFTER `attr_4`;

-- Attribute ids 0 - 4 are Stamina, Strength, Agility, Intellect and Spirit, the order of the old columns.
INSERT INTO `character_class_attributes` (`character`, `class`, `attribute`, `points`)
SELECT `character`, `class`, 0, `attr_0` FROM `character_classes` WHERE `attr_0` > 0;
INSERT INTO `character_class_attributes` (`character`, `class`, `attribute`, `points`)
SELECT `character`, `class`, 1, `attr_1` FROM `character_classes` WHERE `attr_1` > 0;
INSERT INTO `character_class_attributes` (`character`, `class`, `attribute`, `points`)
SELECT `character`, `class`, 2, `attr_2` FROM `character_classes` WHERE `attr_2` > 0;
INSERT INTO `character_class_attributes` (`character`, `class`, `attribute`, `points`)
SELECT `character`, `class`, 3, `attr_3` FROM `character_classes` WHERE `attr_3` > 0;
INSERT INTO `character_class_attributes` (`character`, `class`, `attribute`, `points`)
SELECT `character`, `class`, 4, `attr_4` FROM `character_classes` WHERE `attr_4` > 0;

-- The existing spending was made under the shipped attribute set (ids 0 - 4); 2393499409 is its
-- layout hash, so these characters keep their points.
UPDATE `character_classes` SET `attr_layout` = 2393499409
WHERE `attr_0` + `attr_1` + `attr_2` + `attr_3` + `attr_4` > 0;
