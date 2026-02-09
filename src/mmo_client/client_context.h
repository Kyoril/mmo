#pragma once

#include <memory>

#include <asio/io_service.hpp>

namespace mmo
{
	class LoginConnector;
	class RealmConnector;
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

	struct ClientContext
	{
		ClientContext() = default;
		~ClientContext();

		ClientContext(const ClientContext&) = delete;
		ClientContext& operator=(const ClientContext&) = delete;

		std::unique_ptr<asio::io_service> networkIo;
		std::unique_ptr<asio::io_service::work> networkWork;
		std::shared_ptr<LoginConnector> loginConnector;
		std::shared_ptr<RealmConnector> realmConnector;

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
	};

	ClientContext& GetClientContext();
}
