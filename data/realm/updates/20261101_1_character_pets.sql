-- Persistent pets: the stored pets of each character and the spells they learned. The id of a
-- stored pet is part of its guid, so rows are updated in place and never reinserted.
CREATE TABLE IF NOT EXISTS `character_pets` (
	`id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
	`character_id` BIGINT UNSIGNED NOT NULL,
	`class_id` INT UNSIGNED NOT NULL,
	`pet_entry` INT UNSIGNED NOT NULL,
	`unit_entry` INT UNSIGNED NOT NULL DEFAULT 0,
	`name` VARCHAR(32) NOT NULL,
	`level` INT UNSIGNED NOT NULL DEFAULT 1,
	`xp` INT UNSIGNED NOT NULL DEFAULT 0,
	`health` INT UNSIGNED NOT NULL DEFAULT 0,
	`power` INT UNSIGNED NOT NULL DEFAULT 0,
	`is_dead` TINYINT(1) NOT NULL DEFAULT 0,
	`was_summoned` TINYINT(1) NOT NULL DEFAULT 0,
	`created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (`id`),
	KEY `character_id` (`character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_pet_spells` (
	`pet_id` BIGINT UNSIGNED NOT NULL,
	`spell_id` INT UNSIGNED NOT NULL,
	PRIMARY KEY (`pet_id`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
