DELETE FROM `guild_members`
WHERE `guid` IN (
    SELECT `id`
    FROM `characters`
    WHERE `account_id` IS NULL
);

DELETE FROM `guilds`
WHERE `leader` IN (
    SELECT `id`
    FROM `characters`
    WHERE `account_id` IS NULL
);

DELETE FROM `group_members`
WHERE `guid` IN (
    SELECT `id`
    FROM `characters`
    WHERE `account_id` IS NULL
);

DELETE FROM `group`
WHERE `leader` IN (
    SELECT `id`
    FROM `characters`
    WHERE `account_id` IS NULL
);

