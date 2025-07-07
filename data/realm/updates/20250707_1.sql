ALTER TABLE `characters` 
ADD COLUMN `timePlayed` INT UNSIGNED NOT NULL DEFAULT 0 
COMMENT 'Total time played in seconds' 
AFTER `energy`;
