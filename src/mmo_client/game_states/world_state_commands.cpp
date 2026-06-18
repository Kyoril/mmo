// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_state.h"

#include "net/realm_connector.h"

#include "game/object_type_id.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mmo
{
#ifdef MMO_WITH_DEV_COMMANDS
	namespace
	{
		// Safe parse helpers - use std::from_chars (no exceptions, no locale overhead).
		template<typename T>
		bool ParseUInt(const std::string &s, T &out)
		{
			const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
			return result.ec == std::errc{} && result.ptr == s.data() + s.size();
		}

		bool ParseFloat(const std::string &s, float &out)
		{
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
			const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
			return result.ec == std::errc{} && result.ptr == s.data() + s.size();
#else
			// MSVC older than VS 2019 16.4 may lack floating-point from_chars.
			// Fall back to strtof (sets errno, no exceptions).
			char *end = nullptr;
			errno = 0;
			out = std::strtof(s.c_str(), &end);
			return errno == 0 && end == s.c_str() + s.size();
#endif
		}

		std::vector<std::string> ParseCommandArgs(const std::string &args)
		{
			std::istringstream iss(args);
			std::vector<std::string> tokens;
			std::string token;
			while (iss >> token)
			{
				tokens.push_back(std::move(token));
			}
			return tokens;
		}
	}

	void WorldState::Command_LearnSpell(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.size() != 1)
		{
			ELOG("Usage: learnspell <entry>");
			return;
		}

		uint32 entry = 0;
		if (!ParseUInt(tokens[0], entry))
		{
			ELOG("Invalid entry id: " + tokens[0]);
			return;
		}

		m_realmConnector.LearnSpell(entry);
	}

	void WorldState::Command_CreateMonster(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.size() != 1)
		{
			ELOG("Usage: createmonster <entry>");
			return;
		}

		uint32 entry = 0;
		if (!ParseUInt(tokens[0], entry))
		{
			ELOG("Invalid entry id: " + tokens[0]);
			return;
		}
		m_realmConnector.CreateMonster(entry);
	}

	void WorldState::Command_DestroyMonster(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.size() > 1)
		{
			ELOG("Usage: destroymonster <entry>");
			return;
		}

		uint64 guid = 0;
		if (tokens.empty())
		{
			guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		}
		else
		{
			if (!ParseUInt(tokens[0], guid))
			{
				ELOG("Invalid guid: " + tokens[0]);
				return;
			}
		}

		if (guid == 0)
		{
			ELOG("No target selected and no target guid provided to destroy!");
			return;
		}

		m_realmConnector.DestroyMonster(guid);
	}

	void WorldState::Command_FaceMe(const std::string &cmd, const std::string &args) const
	{
		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected!");
			return;
		}

		m_realmConnector.FaceMe(guid);
	}

	void WorldState::Command_FollowMe(const std::string &cmd, const std::string &args) const
	{
		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected!");
			return;
		}

		m_realmConnector.FollowMe(guid);
	}

	void WorldState::Command_LevelUp(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);

		uint32 level = 1;
		if (!tokens.empty())
		{
			if (!ParseUInt(tokens[0], level))
			{
				ELOG("Invalid level value: " + tokens[0]);
				return;
			}
		}

		m_realmConnector.LevelUp(level);
	}

	void WorldState::Command_GiveMoney(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: money <amount>");
			return;
		}

		uint32 money = 0;
		if (!ParseUInt(tokens[0], money))
		{
			ELOG("Invalid money amount: " + tokens[0]);
			return;
		}
		m_realmConnector.GiveMoney(money);
	}

	void WorldState::Command_AddItem(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: additem <item_id> [<count>]");
			return;
		}

		uint32 itemId = 0;
		if (!ParseUInt(tokens[0], itemId))
		{
			ELOG("Invalid item id: " + tokens[0]);
			return;
		}
		uint32 count = 1;

		if (tokens.size() > 1)
		{
			if (!ParseUInt(tokens[1], count))
			{
				ELOG("Invalid count: " + tokens[1]);
				return;
			}
		}

		m_realmConnector.AddItem(itemId, count);
	}

	void WorldState::Command_WorldPort(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: worldport <map_id> [<x>] [<y>] [<z>] [<facing degree>]");
			return;
		}

		Vector3 position = m_playerController->GetControlledUnit()->GetPosition();
		Radian facing = m_playerController->GetControlledUnit()->GetFacing();

		uint32 mapId = 0;
		if (!ParseUInt(tokens[0], mapId))
		{
			ELOG("Invalid map id: " + tokens[0]);
			return;
		}

		if (tokens.size() > 1)
		{
			if (!ParseFloat(tokens[1], position.x))
			{
				ELOG("Invalid x position: " + tokens[1]);
				return;
			}
		}
		if (tokens.size() > 2)
		{
			if (!ParseFloat(tokens[2], position.y))
			{
				ELOG("Invalid y position: " + tokens[2]);
				return;
			}
		}
		if (tokens.size() > 3)
		{
			if (!ParseFloat(tokens[3], position.z))
			{
				ELOG("Invalid z position: " + tokens[3]);
				return;
			}
		}
		if (tokens.size() > 4)
		{
			float facingDeg = 0.0f;
			if (!ParseFloat(tokens[4], facingDeg))
			{
				ELOG("Invalid facing degree: " + tokens[4]);
				return;
			}
			facing = Degree(facingDeg);
		}

		m_realmConnector.WorldPort(mapId, position, facing);
	}

	void WorldState::Command_Speed(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: speed <speed>");
			return;
		}

		float speed = 0.0f;
		if (!ParseFloat(tokens[0], speed))
		{
			ELOG("Invalid speed value: " + tokens[0]);
			return;
		}
		speed = std::min(50.0f, speed);
		if (speed <= 0.0f)
		{
			ELOG("Speed must be greater than 0!");
			return;
		}

		m_realmConnector.SetSpeed(speed);
	}

	void WorldState::Command_Summon(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: summon <playername>");
			return;
		}

		m_realmConnector.SummonPlayer(tokens[0]);
	}

	void WorldState::Command_Port(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: port <playername>");
			return;
		}

		m_realmConnector.TeleportToPlayer(tokens[0]);
	}

	void WorldState::Command_CheckLineOfSight(const std::string &cmd, const std::string &args) const
	{
		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			ELOG("checklos: no active player");
			return;
		}

		const uint64 targetGuid = player->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			ELOG("checklos: no target selected - select a unit first");
			return;
		}

		m_realmConnector.CheckLineOfSight(targetGuid);
	}
#endif
}
