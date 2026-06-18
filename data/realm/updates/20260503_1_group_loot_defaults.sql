-- Update default loot_method to GroupLoot (3) for new groups
ALTER TABLE `group` ALTER COLUMN `loot_method` SET DEFAULT 3;

-- Update existing groups that still have FreeForAll (0) to use GroupLoot (3)
UPDATE `group` SET `loot_method` = 3 WHERE `loot_method` = 0;
