-- Phase 5: Guild MOTD -- add motd column to guilds table
-- Safe to run on servers that have not yet applied this migration.
ALTER TABLE `guilds`
    ADD COLUMN `motd` VARCHAR(255) NOT NULL DEFAULT ''
    AFTER `leader`;
