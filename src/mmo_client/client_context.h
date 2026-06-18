#pragma once

#include "base/signal.h"

#include <fstream>
#include <memory>

#include <asio/io_service.hpp>

namespace mmo
{
	namespace proto_client
	{
		class Project;
	}

	class GameScript;
	class Minimap;
	class TimerQueue;
	class IAudio;
	class LootClient;
	class VendorClient;
	class TrainerClient;
	class InventoryClient;
	class ClientCache;
	class ActionBar;
	class SpellCast;
	class CooldownManager;
	class QuestClient;
	class PartyInfo;
	class GuildClient;
	class FriendClient;
	class ChannelClient;
	class CharCreateInfo;
	class CharSelect;
	class TalentClient;
	class TradeClient;
	class Discord;
	class GameTimeComponent;
	class ClientRuntime;
	class ClientUiRuntime;

	/// @brief Shared application context for client-owned runtime services.
	///
	/// The context acts as a composition root storage for long-lived systems used
	/// by the client bootstrap and shutdown flow.
	struct ClientContext
	{
		ClientContext() = default;
		/// @brief Releases owned client systems and runtime services.
		~ClientContext();

		ClientContext(const ClientContext&) = delete;
		ClientContext& operator=(const ClientContext&) = delete;

		/// @brief Aggregated network runtime (io service + connectors).
		std::unique_ptr<ClientRuntime> runtime;
		/// @brief Main-thread task service used for dispatching game-thread work.
		asio::io_service timerService;

		/// @brief Frame-ui runtime (localization, UI binding, frame factories).
		std::unique_ptr<ClientUiRuntime> uiRuntime;

		/// @brief File stream for `Logs/Client.log`.
		std::ofstream logFile;
		/// @brief Subscription for forwarding log entries to the log file.
		scoped_connection logConnection;
		/// @brief Subscription for idle-tick runtime processing.
		scoped_connection timerConnection;

		/// @brief Scripting entry point used by frame-ui and client scripts.
		std::unique_ptr<GameScript> gameScript;
		/// @brief Shared minimap subsystem used by world UI and script bindings.
		std::unique_ptr<Minimap> minimap;

		/// @brief Timer queue tied to the client main loop.
		std::unique_ptr<TimerQueue> timerQueue;
		std::unique_ptr<IAudio> audio;
		std::unique_ptr<LootClient> lootClient;
		std::unique_ptr<VendorClient> vendorClient;
		std::unique_ptr<TrainerClient> trainerClient;
		std::unique_ptr<InventoryClient> inventoryClient;
		std::unique_ptr<ClientCache> clientCache;
		std::unique_ptr<ActionBar> actionBar;
		std::unique_ptr<SpellCast> spellCast;
		std::unique_ptr<CooldownManager> cooldownManager;
		std::unique_ptr<QuestClient> questClient;
		std::unique_ptr<PartyInfo> partyInfo;
		std::unique_ptr<GuildClient> guildClient;
		std::unique_ptr<FriendClient> friendClient;
		std::unique_ptr<ChannelClient> channelClient;
		std::unique_ptr<CharCreateInfo> charCreateInfo;
		std::unique_ptr<CharSelect> charSelect;
		std::unique_ptr<TalentClient> talentClient;
		std::unique_ptr<TradeClient> tradeClient;
		std::unique_ptr<Discord> discord;
		/// @brief Loaded client project data (DB/content metadata).
		std::unique_ptr<proto_client::Project> project;
		/// @brief Shared game-time state for scripts and world systems.
		std::unique_ptr<GameTimeComponent> gameTime;
	};

	/// @brief Returns the singleton client context.
	/// @return Process-global client context instance.
	ClientContext& GetClientContext();
}
