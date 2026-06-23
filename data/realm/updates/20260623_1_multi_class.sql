-- Multi-Class System (Phase 1: data spine)
--
-- A character can know multiple classes, switching the active one via a class-change spell.
-- Each known class carries its own class level, (reserved) class xp, attribute-point spending
-- profile and talent ranks. `characters.class` continues to mean the *active* class.
--
-- Talent points derive from class level (frozen at 1 for now), so existing characters' talents
-- effectively reset on first load under the new rules. This is acceptable during internal
-- development, so no talent-preserving migration is attempted.

-- One row per class a character has ever known.
CREATE TABLE IF NOT EXISTS `character_classes` (
  `character` bigint unsigned NOT NULL,
  `class`     int unsigned NOT NULL,
  `level`     tinyint unsigned NOT NULL DEFAULT 1,
  `xp`        int unsigned NOT NULL DEFAULT 0,
  `attr_0`    int unsigned NOT NULL DEFAULT 0,
  `attr_1`    int unsigned NOT NULL DEFAULT 0,
  `attr_2`    int unsigned NOT NULL DEFAULT 0,
  `attr_3`    int unsigned NOT NULL DEFAULT 0,
  `attr_4`    int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`character`, `class`),
  KEY `idx_character_classes_character` (`character`),
  CONSTRAINT `fk_character_classes_character` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

-- Backfill: every existing character knows their current class. Class level starts at 1; the
-- existing (character-wide) attribute spending is carried into this initial class profile.
INSERT INTO `character_classes` (`character`, `class`, `level`, `xp`, `attr_0`, `attr_1`, `attr_2`, `attr_3`, `attr_4`)
SELECT `id`, `class`, 1, 0, `attr_0`, `attr_1`, `attr_2`, `attr_3`, `attr_4`
FROM `characters`
WHERE `class` IS NOT NULL;

-- Scope talent ranks per class. Backfill existing rows with the character's (only) class so the
-- schema stays valid; the runtime talent-point formula (class level 1) will naturally reset talents
-- on the next save.
ALTER TABLE `character_talents` ADD COLUMN `class` int unsigned NOT NULL DEFAULT 0 AFTER `character`;

UPDATE `character_talents` `t`
JOIN `characters` `c` ON `t`.`character` = `c`.`id`
SET `t`.`class` = `c`.`class`;

ALTER TABLE `character_talents`
  DROP INDEX `talent_character_unique_pair`,
  ADD UNIQUE KEY `talent_class_character_unique` (`character`, `class`, `talent`);
