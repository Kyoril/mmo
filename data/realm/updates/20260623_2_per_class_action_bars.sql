-- Multi-Class System: per-class action bars
--
-- Each known class keeps its own action bar layout (FFXIV-like). Add a `class` column to
-- `character_actions` and key the bar by (character, class, button). Switching the active class
-- swaps the live action bar to that class's saved layout.
--
-- Existing rows belong to the character's currently active class, so backfill `class` from
-- `characters.class` before widening the primary key.

ALTER TABLE `character_actions` ADD COLUMN `class` int unsigned NOT NULL DEFAULT 0 AFTER `character_id`;

UPDATE `character_actions` `a`
JOIN `characters` `c` ON `a`.`character_id` = `c`.`id`
SET `a`.`class` = `c`.`class`;

ALTER TABLE `character_actions`
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`character_id`, `class`, `button`);
