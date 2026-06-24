-- Periodic samples of the concurrent player count per realm, written by the login server.
-- Used by the admin dashboard to render the historical concurrent-players chart.
CREATE TABLE IF NOT EXISTS `player_count_sample` (
  `realm_id` int unsigned NOT NULL,
  `timestamp` datetime NOT NULL,
  `player_count` int unsigned NOT NULL,
  KEY `timestamp` (`timestamp`),
  KEY `realm_id` (`realm_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
