#pragma once

#include "simple_file_format/sff_write_table.h"
#include "base/typedefs.h"

namespace mmo
{
	/// Manages the login server configuration.
	struct Configuration
	{
		/// Config file version: used to detect new configuration files
		static const uint32 LoginConfigVersion;

		/// The port to be used by game clients to log in.
		uint16 playerPort;
		/// The port to be used by realms to log in.
		uint16 realmPort;
		/// Maximum number of player connections.
		size_t maxPlayers;
		/// Maximum number of realm connections.
		size_t maxRealms;

		/// The port to be used for a mysql connection.
		uint16 mysqlPort;
		/// The mysql server host address (ip or dns).
		String mysqlHost;
		/// The mysql user to be used.
		String mysqlUser;
		/// The mysql user password to be used.
		String mysqlPassword;
		/// The mysql database to be used.
		String mysqlDatabase;

		/// Indicates whether or not file logging is enabled.
		bool isLogActive;
		/// File name of the log file.
		String logFileName;
		/// If enabled, the log contents will be buffered before they are written to
		/// the file, which could be more efficient..
		bool isLogFileBuffering;

		/// The port to be used for a web connection.
		uint16 webPort;
		/// The port to be used for an ssl web connection.
		uint16 webSSLPort;
		/// The user name of the web user.
		String webUser;
		/// The password for the web user.
		String webPassword;


		explicit Configuration();
		bool load(const String &fileName);
		bool save(const String &fileName);
	};
}
