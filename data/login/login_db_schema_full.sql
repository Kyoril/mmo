/*
SQLyog Community v13.1.7 (64 bit)
MySQL - 8.0.22 : Database - mmo_login
*********************************************************************
*/

USE `mmo_login`;

/*!40101 SET NAMES utf8 */;

/*!40101 SET SQL_MODE=''*/;

/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;
/*Table structure for table `account` */

DROP TABLE IF EXISTS `account`;

CREATE TABLE `account` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT 'User account id',
  `username` varchar(32) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL COMMENT 'Users account name',
  `s` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci,
  `v` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci,
  `k` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci COMMENT 'Session key',
  `status` tinyint unsigned DEFAULT '0' COMMENT 'Account status',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_login` datetime DEFAULT NULL,
  `last_ip` varchar(39) CHARACTER SET latin1 COLLATE latin1_german1_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `username` (`username`)
) ENGINE=InnoDB AUTO_INCREMENT=15 DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

/*Table structure for table `account_info` */

DROP TABLE IF EXISTS `account_info`;

CREATE TABLE `account_info` (
  `account` bigint unsigned NOT NULL,
  `name` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  `lastname` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  `email` varchar(128) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  PRIMARY KEY (`account`),
  CONSTRAINT `account_info_ibfk_1` FOREIGN KEY (`account`) REFERENCES `account` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

/*Table structure for table `account_login` */

DROP TABLE IF EXISTS `account_login`;

CREATE TABLE `account_login` (
  `account_id` bigint unsigned NOT NULL,
  `timestamp` datetime NOT NULL,
  `ip_address` varchar(48) COLLATE latin1_german1_ci NOT NULL,
  `succeeded` tinyint unsigned DEFAULT NULL,
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

/*Table structure for table `realm` */

DROP TABLE IF EXISTS `realm`;

CREATE TABLE `realm` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  `address` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci NOT NULL,
  `port` smallint unsigned NOT NULL,
  `s` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci,
  `v` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci,
  `k` longtext CHARACTER SET latin1 COLLATE latin1_german1_ci,
  `last_login` datetime DEFAULT NULL,
  `last_ip` varchar(39) CHARACTER SET latin1 COLLATE latin1_german1_ci DEFAULT NULL,
  `last_build` varchar(64) CHARACTER SET latin1 COLLATE latin1_german1_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB AUTO_INCREMENT=3 DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;
