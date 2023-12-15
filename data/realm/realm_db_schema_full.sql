/*
SQLyog Community v13.1.7 (64 bit)
MySQL - 8.0.22 : Database - mmo_realm
*********************************************************************
*/

/*!40101 SET NAMES utf8 */;

/*!40101 SET SQL_MODE=''*/;

/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;
/*Table structure for table `characters` */

DROP TABLE IF EXISTS `characters`;

CREATE TABLE `characters` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `account_id` bigint unsigned DEFAULT NULL,
  `flags` int unsigned DEFAULT '0',
  `name` varchar(16) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  `gender` tinyint unsigned NOT NULL DEFAULT '0',
  `race` int DEFAULT NULL,
  `class` int DEFAULT NULL,
  `level` tinyint unsigned NOT NULL DEFAULT '1',
  `map` int unsigned NOT NULL,
  `instance` varchar(64) DEFAULT NULL,
  `zone` int DEFAULT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `o` float NOT NULL DEFAULT '0',
  `hp` int NOT NULL,
  `deleted_at` datetime DEFAULT NULL,
  `deleted_account` bigint unsigned DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `UNIQUE_NAME` (`name`)
) ENGINE=InnoDB AUTO_INCREMENT=9 DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

DROP TABLE IF EXISTS `character_chat`;

CREATE TABLE `character_chat` (
  `character` bigint unsigned NOT NULL,
  `type` smallint NOT NULL,
  `message` varchar(255) NOT NULL,
  `timestamp` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY `fk_character_id_idx` (`character`),
  CONSTRAINT `fk_character_id` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

/*Table structure for table `world` */

DROP TABLE IF EXISTS `world`;

CREATE TABLE `world` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(64) COLLATE latin1_german1_ci NOT NULL,
  `s` longtext COLLATE latin1_german1_ci NOT NULL,
  `v` longtext COLLATE latin1_german1_ci NOT NULL,
  `k` longtext COLLATE latin1_german1_ci,
  `last_login` datetime DEFAULT NULL,
  `last_ip` varchar(39) COLLATE latin1_german1_ci DEFAULT NULL,
  `last_build` varchar(32) COLLATE latin1_german1_ci DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;
