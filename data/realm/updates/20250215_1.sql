
CREATE TABLE `group` (
  `id` bigint(20) unsigned NOT NULL,
  `leader` bigint(20) unsigned NOT NULL,
  `loot_method` smallint(5) unsigned NOT NULL DEFAULT '0',
  `loot_master` bigint(20) unsigned DEFAULT NULL,
  `loot_treshold` smallint(5) unsigned NOT NULL DEFAULT '2',
  `icon_1` bigint(20) unsigned DEFAULT NULL,
  `icon_2` bigint(20) unsigned DEFAULT NULL,
  `icon_3` bigint(20) unsigned DEFAULT NULL,
  `icon_4` bigint(20) unsigned DEFAULT NULL,
  `icon_5` bigint(20) unsigned DEFAULT NULL,
  `icon_6` bigint(20) unsigned DEFAULT NULL,
  `icon_7` bigint(20) unsigned DEFAULT NULL,
  `icon_8` bigint(20) unsigned DEFAULT NULL,
  `type` smallint(5) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `group_leader_key` (`leader`),
  KEY `group_loot_master_key` (`loot_master`),
  CONSTRAINT `group_ibfk_1` FOREIGN KEY (`leader`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `group_ibfk_2` FOREIGN KEY (`loot_master`) REFERENCES `characters` (`id`) ON DELETE SET NULL ON UPDATE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

DROP TABLE IF EXISTS `group_members`;

CREATE TABLE `group_members` (
  `group` bigint(20) unsigned NOT NULL,
  `guid` bigint(20) unsigned NOT NULL,
  `assistant` smallint(5) unsigned DEFAULT '0',
  KEY `guid` (`guid`),
  KEY `group_members_ibfk_1` (`group`),
  CONSTRAINT `group_members_ibfk_1` FOREIGN KEY (`group`) REFERENCES `group` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `group_members_ibfk_2` FOREIGN KEY (`guid`) REFERENCES `characters` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

ALTER TABLE `characters` ADD COLUMN `last_group` bigint(20) unsigned DEFAULT NULL COMMENT 'Characters last group id.';
