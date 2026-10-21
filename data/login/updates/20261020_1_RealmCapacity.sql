-- Player capacity per realm. The realm list shows each realm's population (Low / Medium / High /
-- Full) relative to this value; administrators tune it per realm through the login server REST API.
ALTER TABLE `realm` ADD COLUMN `capacity` int unsigned NOT NULL DEFAULT 1000 COMMENT 'Players at which the realm list shows the realm as full';
