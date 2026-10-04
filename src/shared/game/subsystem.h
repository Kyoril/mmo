// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <array>
#include <cstring>

namespace mmo
{
	namespace game
	{
		/// Server subsystems whose availability is announced to clients. The values go on the wire
		/// (SubsystemStatus packets), so only ever append.
		namespace subsystem
		{
			enum Type : uint8
			{
				/// In-game bug reports. Owned by the world node that hosts the player.
				BugReport = 0,

				Count_
			};
		}

		typedef subsystem::Type Subsystem;

		/// Availability of a subsystem. A byte rather than a bool so states like "maintenance"
		/// can be added later without a wire change.
		namespace subsystem_status
		{
			enum Type : uint8
			{
				Unavailable = 0,
				Available = 1,

				Count_
			};
		}

		typedef subsystem_status::Type SubsystemStatus;

		/// Which server tier decides a subsystem's availability.
		namespace subsystem_owner
		{
			enum Type : uint8
			{
				Realm,
				World
			};
		}

		typedef subsystem_owner::Type SubsystemOwner;

		/// Static description of a subsystem: its script name and owning tier.
		struct SubsystemInfo
		{
			Subsystem id;
			const char* name;
			SubsystemOwner owner;
		};

		/// One entry per subsystem, indexed by its id. Adding a subsystem = one enum value + one row.
		inline constexpr std::array<SubsystemInfo, subsystem::Count_> SubsystemTable =
		{{
			{ subsystem::BugReport, "BUG_REPORT", subsystem_owner::World },
		}};

		/// Returns the script name of a subsystem ("BUG_REPORT"), or an empty string if unknown.
		inline const char* GetSubsystemName(const uint8 id)
		{
			return id < subsystem::Count_ ? SubsystemTable[id].name : "";
		}

		/// Looks up a subsystem by its script name. Returns false if the name is unknown.
		inline bool FindSubsystemByName(const char* name, Subsystem& out)
		{
			if (!name)
			{
				return false;
			}

			for (const auto& info : SubsystemTable)
			{
				if (std::strcmp(info.name, name) == 0)
				{
					out = info.id;
					return true;
				}
			}

			return false;
		}

		/// Returns the tier that owns the given subsystem.
		inline SubsystemOwner GetSubsystemOwner(const Subsystem id)
		{
			return SubsystemTable[id].owner;
		}
	}
}
