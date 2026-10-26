// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// Exit codes of a bot session, kept stable so that agents and scripts can rely on them.
	namespace bot_exit_code
	{
		enum Type
		{
			Success = 0,
			ScenarioFailed = 1,
			SetupFailed = 2,
			Timeout = 3,
			Disconnected = 4,
		};
	}

	/// Where a session failed: which server, and at which step. A swarm tells a crashed login
	/// server from a crashed realm or world server by this, not by the exit code.
	namespace bot_failure_stage
	{
		enum Type
		{
			None,

			/// The login server did not accept the connection.
			LoginConnect,

			/// The login server refused the account or the client version.
			LoginAuth,

			/// No realm in the list, or not the configured one.
			RealmList,

			/// The realm server did not accept the connection.
			RealmConnect,

			/// The realm server refused the session.
			RealmAuth,

			/// Character list, creation or deletion went wrong.
			Character,

			/// The realm refused to put the character into the world.
			EnterWorld,

			/// The connection to the realm server was lost.
			RealmLost,

			/// The realm took the character out of the world because its world node went away,
			/// keeping the realm connection.
			WorldLost
		};
	}

	/// A short name for a failure stage, for logs and telemetry.
	inline const char* GetFailureStageName(const bot_failure_stage::Type stage)
	{
		switch (stage)
		{
		case bot_failure_stage::None: return "none";
		case bot_failure_stage::LoginConnect: return "login_connect";
		case bot_failure_stage::LoginAuth: return "login_auth";
		case bot_failure_stage::RealmList: return "realm_list";
		case bot_failure_stage::RealmConnect: return "realm_connect";
		case bot_failure_stage::RealmAuth: return "realm_auth";
		case bot_failure_stage::Character: return "character";
		case bot_failure_stage::EnterWorld: return "enter_world";
		case bot_failure_stage::RealmLost: return "realm_lost";
		case bot_failure_stage::WorldLost: return "world_lost";
		}

		return "unknown";
	}
}
