
ALTER TABLE `characters`
ADD COLUMN `bind_map` int unsigned NOT NULL DEFAULT 0 AFTER `o`,
ADD COLUMN `bind_x` float NOT NULL DEFAULT 0 AFTER `bind_map`,
ADD COLUMN `bind_y` float NOT NULL DEFAULT 0 AFTER `bind_x`,
ADD COLUMN `bind_z` float NOT NULL DEFAULT 0 AFTER `bind_y`,
ADD COLUMN `bind_o` float NOT NULL DEFAULT 0 AFTER `bind_z`;
