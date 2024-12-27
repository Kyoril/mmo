
/* Convert all items with entry 0 to entry 36 because we changed the item's id in the editor data. */
UPDATE `character_items` SET `entry` = 36 WHERE `entry` = 0;