#pragma once

#include "base/signal.h"
#include "frame_ui/localization.h"

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
	class CharCreateInfo;
	class CharSelect;
	class TalentClient;
	class Discord;
	class GameTimeComponent;
	class ClientRuntime;

	struct ClientContext
	{
		ClientContext() = default;
		~ClientContext();

		ClientContext(const ClientContext&) = delete;
		ClientContext& operator=(const ClientContext&) = delete;

		std::unique_ptr<ClientRuntime> runtime;
		asio::io_service timerService;

		scoped_connection_container frameUiConnections;
		Localization localization;

		std::ofstream logFile;
		scoped_connection logConnection;
		scoped_connection timerConnection;

		std::unique_ptr<GameScript> gameScript;
		std::unique_ptr<Minimap> minimap;

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
		std::unique_ptr<CharCreateInfo> charCreateInfo;
		std::unique_ptr<CharSelect> charSelect;
		std::unique_ptr<TalentClient> talentClient;
		std::unique_ptr<Discord> discord;
		std::unique_ptr<proto_client::Project> project;
		std::unique_ptr<GameTimeComponent> gameTime;
	};

	ClientContext& GetClientContext();
}
