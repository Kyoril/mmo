-- Teleport all human characters back to their new starting point in Oakenshire when they are on the dev map
UPDATE `characters` SET `x` = 288.253, `y` = 5.330, `z` = 552.571
WHERE `map` = 0 AND `race` = 0;

-- Update all human character bind position as well
UPDATE `characters` SET `bind_x` = 288.253, `bind_y` = 5.330, `bind_z` = 552.571
WHERE `bind_map` = 0 AND `race` = 0;
