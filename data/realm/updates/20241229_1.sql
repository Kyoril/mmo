
CREATE TABLE `character_quests` (
  `character_id` BIGINT UNSIGNED NOT NULL,
  `quest` INT UNSIGNED NOT NULL,
  `status` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `explored` TINYINT NOT NULL DEFAULT 0,
  `timer` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `unit_kills` json DEFAULT NULL,
  PRIMARY KEY (`character_id`, `quest`),
  CONSTRAINT `fk_character_quest_character_id`
    FOREIGN KEY (`character_id`)
    REFERENCES `characters` (`id`)
    ON DELETE CASCADE
    ON UPDATE CASCADE);
