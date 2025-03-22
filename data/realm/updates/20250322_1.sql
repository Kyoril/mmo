
CREATE TABLE `guilds` (
  `id` bigint unsigned NOT NULL,
  `name` varchar(90) COLLATE latin1_german1_ci NOT NULL,
  `leader` bigint unsigned NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `name_UNIQUE` (`name`),
  UNIQUE KEY `leader_UNIQUE` (`leader`),
  CONSTRAINT `fk_character_leader_id` FOREIGN KEY (`leader`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

CREATE TABLE `guild_ranks` (
  `guild_id` bigint unsigned NOT NULL,
  `id` smallint unsigned NOT NULL,
  `name` varchar(45) COLLATE latin1_german1_ci NOT NULL,
  `permissions` int unsigned NOT NULL,
  PRIMARY KEY (`guild_id`,`id`),
  CONSTRAINT `fk_guild_id_id` FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

CREATE TABLE `guild_members` (
  `guild_id` bigint unsigned NOT NULL,
  `guid` bigint unsigned NOT NULL,
  `rank` smallint unsigned NOT NULL,
  PRIMARY KEY (`guild_id`,`guid`),
  KEY `fk_guild_rank_idx` (`guild_id`,`rank`),
  CONSTRAINT `fk_guild_rank` FOREIGN KEY (`guild_id`, `rank`) REFERENCES `guild_ranks` (`guild_id`, `id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
