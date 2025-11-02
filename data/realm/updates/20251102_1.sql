-- Add PRIMARY KEY to character_items table to prevent duplicates
-- This allows REPLACE INTO to work correctly by identifying unique rows by (owner, slot)

-- First, remove any existing duplicates (keep only one entry for each owner+slot combination)
-- We need to add a temporary auto-increment ID first to identify which rows to delete
ALTER TABLE `character_items` ADD COLUMN `temp_id` INT AUTO_INCREMENT PRIMARY KEY FIRST;

-- Delete all duplicates except the one with the highest temp_id (most recently inserted)
DELETE t1 FROM `character_items` t1
INNER JOIN (
    SELECT owner, slot, MAX(temp_id) as max_id
    FROM `character_items`
    GROUP BY owner, slot
) t2 ON t1.owner = t2.owner AND t1.slot = t2.slot
WHERE t1.temp_id < t2.max_id;

-- Remove the temporary ID column
ALTER TABLE `character_items` DROP PRIMARY KEY, DROP COLUMN `temp_id`;

-- Now add the proper PRIMARY KEY constraint
ALTER TABLE `character_items` 
  ADD PRIMARY KEY (`owner`, `slot`);
