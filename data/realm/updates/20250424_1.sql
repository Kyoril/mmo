-- Add Message of the Day (MOTD) table
CREATE TABLE IF NOT EXISTS `realm_motd` (
  `id` INT NOT NULL PRIMARY KEY DEFAULT 1,
  `message` TEXT NOT NULL,
  `last_updated` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- Insert default MOTD message if it doesn't exist
INSERT IGNORE INTO `realm_motd` (`id`, `message`) VALUES (1, 'Welcome to the server!');