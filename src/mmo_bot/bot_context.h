// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/chat_type.h"
#include "game/movement_info.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace mmo
{
	// Forward declarations
	class BotRealmConnector;
	struct BotConfig;

	/// Provides context and capabilities to bot actions.
	/// This class acts as a facade, exposing only the necessary operations
	/// to actions while encapsulating the underlying implementation details.
	/// 
	/// Following Clean Architecture principles, this creates a boundary between
	/// the bot's domain logic (actions) and the infrastructure (network connectors).
	class BotContext
	{
	public:
		explicit BotContext(
			std::shared_ptr<BotRealmConnector> realmConnector,
			const BotConfig& config);

		/// Gets the bot's configuration.
		const BotConfig& GetConfig() const { return m_config; }

		/// Checks if the bot is connected to the world and ready to perform actions.
		bool IsWorldReady() const { return m_worldReady; }

		/// Sets whether the bot is ready in the world.
		void SetWorldReady(bool ready) { m_worldReady = ready; }

		/// Gets the GUID of the bot's selected character.
		uint64 GetSelectedCharacterGuid() const;

		/// Gets the current movement information of the bot.
		const MovementInfo& GetMovementInfo() const;

		/// Sends a chat message.
		/// @param message The message text to send.
		/// @param chatType The type of chat (say, yell, whisper, etc.).
		/// @param target The target player name (for whispers/channels).
		void SendChatMessage(const std::string& message, ChatType chatType, const std::string& target = "");

		/// Sends a movement update to the server.
		/// @param opCode The movement opcode.
		/// @param info The movement information.
		void SendMovementUpdate(uint16 opCode, const MovementInfo& info);

		/// Sends a landed packet to clear the FALLING flag after spawn.
		/// Characters spawn with the FALLING flag set and must send MoveFallLand
		/// to properly remove it before other movement can occur.
		void SendLandedPacket();

		/// Updates the bot's local movement information.
		/// @param info The new movement information.
		void UpdateMovementInfo(const MovementInfo& info);

		/// Gets the current server time in milliseconds.
		GameTime GetServerTime() const;

		/// Stores a custom state value (for use by actions/profiles).
		/// @param key The state key.
		/// @param value The state value.
		void SetState(const std::string& key, const std::string& value);

		/// Retrieves a custom state value.
		/// @param key The state key.
		/// @param defaultValue The value to return if the key doesn't exist.
		/// @return The state value or the default value.
		std::string GetState(const std::string& key, const std::string& defaultValue = "") const;

		/// Checks if a custom state key exists.
		/// @param key The state key to check.
		/// @return True if the key exists, false otherwise.
		bool HasState(const std::string& key) const;

		/// Removes a custom state value.
		/// @param key The state key to remove.
		void ClearState(const std::string& key);

		/// Gets the realm connector for advanced operations.
		/// Use with caution - prefer using the facade methods when possible.
		std::shared_ptr<BotRealmConnector> GetRealmConnector() const { return m_realmConnector; }

		/// Accepts a pending party invitation.
		void AcceptPartyInvitation();

		/// Declines a pending party invitation.
		void DeclinePartyInvitation();

	private:
		std::shared_ptr<BotRealmConnector> m_realmConnector;
		const BotConfig& m_config;
		bool m_worldReady { false };
		MovementInfo m_cachedMovementInfo;
		
		/// Custom state storage for actions and profiles
		std::map<std::string, std::string> m_customState;
	};
}
