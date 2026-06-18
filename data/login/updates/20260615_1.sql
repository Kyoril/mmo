
CREATE TABLE `feature` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL COMMENT 'Unique feature key',
  `description` varchar(255) CHARACTER SET latin1 COLLATE latin1_german1_ci DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

CREATE TABLE `account_feature` (
  `account_id` bigint unsigned NOT NULL,
  `feature_id` int unsigned NOT NULL,
  `granted_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `expiration` datetime DEFAULT NULL COMMENT 'NULL means the grant never expires',
  PRIMARY KEY (`account_id`,`feature_id`),
  KEY `feature_id` (`feature_id`),
  CONSTRAINT `account_feature_ibfk_1` FOREIGN KEY (`account_id`) REFERENCES `account` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `account_feature_ibfk_2` FOREIGN KEY (`feature_id`) REFERENCES `feature` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

CREATE TABLE `realm_feature_requirement` (
  `realm_id` int unsigned NOT NULL,
  `feature_id` int unsigned NOT NULL,
  `require_visibility` tinyint NOT NULL DEFAULT 0 COMMENT 'Account must own the feature to see the realm',
  `require_login` tinyint NOT NULL DEFAULT 0 COMMENT 'Account must own the feature to log in to the realm',
  PRIMARY KEY (`realm_id`,`feature_id`),
  KEY `feature_id` (`feature_id`),
  CONSTRAINT `realm_feature_requirement_ibfk_1` FOREIGN KEY (`realm_id`) REFERENCES `realm` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `realm_feature_requirement_ibfk_2` FOREIGN KEY (`feature_id`) REFERENCES `feature` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
