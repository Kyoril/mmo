-- Global Chat Channels
-- Stores each character's persisted membership decision (delta from the channel default).
-- status = 1 means the character is a member, status = 0 means the character explicitly left.
-- channel_id references a proto ChatChannelEntry id and is intentionally NOT a foreign key, so
-- channels can be added, renamed or deleted in the game data without breaking membership rows.

CREATE TABLE `character_chat_channels` (
    `character_id` BIGINT UNSIGNED NOT NULL,
    `channel_id` INT UNSIGNED NOT NULL,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`character_id`, `channel_id`),
    INDEX `idx_chat_channel_character` (`character_id`),
    CONSTRAINT `fk_chat_channel_character` FOREIGN KEY (`character_id`) REFERENCES `characters`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
