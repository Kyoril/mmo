-- Friend List System
-- Create table to store friendships between characters

CREATE TABLE `friend_list` (
    `character_id` BIGINT UNSIGNED NOT NULL,
    `friend_id` BIGINT UNSIGNED NOT NULL,
    `timestamp` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`character_id`, `friend_id`),
    INDEX `idx_friend_character` (`character_id`),
    INDEX `idx_friend_friend` (`friend_id`),
    CONSTRAINT `fk_friend_character` FOREIGN KEY (`character_id`) REFERENCES `characters`(`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_friend` FOREIGN KEY (`friend_id`) REFERENCES `characters`(`id`) ON DELETE CASCADE,
    CHECK (`character_id` < `friend_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
