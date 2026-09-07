// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <string>
#include <vector>

namespace mmo
{
	/// One bot's identity and the parts of its personality that have to survive a restart.
	///
	/// The point of persisting this is reproducibility: a bot that comes back as the same
	/// character, at the same level, with the same seed behaves the same way, so two swarm runs
	/// can be compared. A roster regenerated on every run would make every run its own
	/// experiment.
	struct BotRosterEntry final
	{
		uint32 index { 0 };
		std::string account;
		std::string password;
		std::string characterName;
		uint8 race { 0 };
		uint8 characterClass { 1 };
		uint8 gender { 0 };

		/// Level the bot is cheated to on entering the world. Rolled once, then kept.
		uint32 targetLevel { 1 };

		/// Seed for anything else this bot randomises.
		uint32 seed { 0 };
	};

	/// Builds a deterministic roster of the given size.
	///
	/// Deterministic means index N always produces the same account and character name, so
	/// provisioning is idempotent: running it again over an existing set of accounts is a no-op
	/// rather than a second population.
	///
	/// @param count Number of bots.
	/// @param accountPrefix Account name prefix, e.g. "swarm".
	/// @param password Shared account password. These are throwaway test accounts.
	/// @param minLevel Lowest level a bot may be rolled at.
	/// @param maxLevel Highest level a bot may be rolled at.
	/// @param classCount Number of playable classes to spread bots across.
	[[nodiscard]] std::vector<BotRosterEntry> BuildRoster(
		uint32 count,
		const std::string& accountPrefix,
		const std::string& password,
		uint32 minLevel,
		uint32 maxLevel,
		uint32 classCount);

	/// Generates a pronounceable name from a seed. Letters only, 3 to 12 characters, which is
	/// what the realm accepts.
	[[nodiscard]] std::string GenerateBotName(uint32 seed);

	/// Reads a roster written by SaveRoster. Returns false if the file does not exist or cannot
	/// be parsed; the caller then builds a fresh one.
	[[nodiscard]] bool LoadRoster(const std::string& path, std::vector<BotRosterEntry>& outRoster);

	[[nodiscard]] bool SaveRoster(const std::string& path, const std::vector<BotRosterEntry>& roster);
}
