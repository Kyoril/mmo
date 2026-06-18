-- Aura & Cooldown Persistence
-- Stores non-passive, non-equipment auras and active spell cooldowns so they survive
-- logout/login and teleports between map instances / world nodes.

-- Auras (header row): expiration is stored as remaining duration (milliseconds); offline time
-- does not elapse. The `realtime` column is reserved for a future per-aura wall-clock expiry mode.
CREATE TABLE `character_auras` (
    `character_id`       BIGINT UNSIGNED NOT NULL,
    `slot`               INT UNSIGNED NOT NULL,
    `spell`              INT UNSIGNED NOT NULL,
    `caster`             BIGINT UNSIGNED NOT NULL,
    `remaining_duration` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `stacks`             INT UNSIGNED NOT NULL DEFAULT 1,
    `realtime`           TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`character_id`, `slot`),
    INDEX `idx_aura_character` (`character_id`),
    CONSTRAINT `fk_aura_character` FOREIGN KEY (`character_id`) REFERENCES `characters`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

-- Aura effects (child rows): one row per ApplyAura effect, holding the cast-time base points so a
-- restored aura keeps its exact magnitudes. Cascades from its parent aura header.
CREATE TABLE `character_aura_effects` (
    `character_id` BIGINT UNSIGNED NOT NULL,
    `slot`         INT UNSIGNED NOT NULL,
    `effect_index` INT UNSIGNED NOT NULL,
    `base_points`  INT NOT NULL DEFAULT 0,
    PRIMARY KEY (`character_id`, `slot`, `effect_index`),
    INDEX `idx_aura_effect_character` (`character_id`),
    CONSTRAINT `fk_aura_effect_aura` FOREIGN KEY (`character_id`, `slot`) REFERENCES `character_auras`(`character_id`, `slot`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

-- Cooldowns: stored as a wall-clock end timestamp (unix seconds). Expired entries are dropped
-- and pruned when the character is loaded.
CREATE TABLE `character_spell_cooldowns` (
    `character_id` BIGINT UNSIGNED NOT NULL,
    `spell`        INT UNSIGNED NOT NULL,
    `end_time`     BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (`character_id`, `spell`),
    INDEX `idx_cooldown_character` (`character_id`),
    CONSTRAINT `fk_cooldown_character` FOREIGN KEY (`character_id`) REFERENCES `characters`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
