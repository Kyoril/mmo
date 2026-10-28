-- Per-account character list order set by the client; 0 means never sorted (listed after sorted ones, by creation).
ALTER TABLE `characters` ADD COLUMN `sort_index` int unsigned NOT NULL DEFAULT 0;
