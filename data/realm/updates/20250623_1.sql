ALTER TABLE `characters` 
ADD COLUMN `deleted_name` VARCHAR(16) NULL DEFAULT NULL AFTER `hp`;

UPDATE `characters` SET `deleted_name` = `name` WHERE `account_id` IS NULL;
UPDATE `characters` SET `name` = HEX(UUID_SHORT()) WHERE `account_id` IS NULL;
