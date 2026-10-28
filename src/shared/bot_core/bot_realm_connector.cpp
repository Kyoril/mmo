// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_realm_connector.h"

#include "version.h"
#include "game/field_map.h"
#include "game/group.h"
#include "game/loot.h"
#include "game/movement_info.h"
#include "game/object_type_id.h"

#include "base/clock.h"
#include "base/constants.h"
#include "base/localization.h"
#include "base/random.h"
#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <vector>

#include <zlib.h>

namespace mmo
{
	namespace
	{
		MovementType MovementTypeFromForcePacket(const uint16 opCode)
		{
			switch (opCode)
			{
			case game::realm_client_packet::ForceMoveSetWalkSpeed:
				return movement_type::Walk;
			case game::realm_client_packet::ForceMoveSetRunSpeed:
				return movement_type::Run;
			case game::realm_client_packet::ForceMoveSetRunBackSpeed:
				return movement_type::Backwards;
			case game::realm_client_packet::ForceMoveSetSwimSpeed:
				return movement_type::Swim;
			case game::realm_client_packet::ForceMoveSetSwimBackSpeed:
				return movement_type::SwimBackwards;
			case game::realm_client_packet::ForceMoveSetTurnRate:
				return movement_type::Turn;
			case game::realm_client_packet::ForceSetFlightSpeed:
				return movement_type::Flight;
			case game::realm_client_packet::ForceSetFlightBackSpeed:
				return movement_type::FlightBackwards;
			default:
				return movement_type::Run;
			}
		}

		bool IsCastTargetLocallyValid(const SpellTargetMap& targetMap)
		{
			const uint32 targetFlags = targetMap.GetTargetMap();
			if ((targetFlags & spell_cast_target_flags::Unit) != 0 && targetMap.GetUnitTarget() == 0)
			{
				return false;
			}

			if ((targetFlags & spell_cast_target_flags::Object) != 0 && targetMap.GetGOTarget() == 0)
			{
				return false;
			}

			if ((targetFlags & spell_cast_target_flags::Item) != 0 && targetMap.GetItemTarget() == 0)
			{
				return false;
			}

			if ((targetFlags & spell_cast_target_flags::Corpse) != 0 && targetMap.GetCorpseTarget() == 0)
			{
				return false;
			}

			return true;
		}
	}

	BotRealmConnector::BotRealmConnector(asio::io_service& io)
		: game::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
	{
	}

	PacketParseResult BotRealmConnector::HandleIncomingPacket(game::IncomingPacket& packet)
	{
		game::EncryptedConnector<game::Protocol>::PacketHandler handler = nullptr;
		{
			std::scoped_lock lock{ m_packetHandlerMutex };

			const auto it = m_packetHandlers.find(packet.GetId());
			if (it != m_packetHandlers.end())
			{
				handler = it->second;
			}
		}

		if (!handler)
		{
			WLOG("[Realm] Unhandled packet 0x" << std::hex << std::setw(2) << std::setfill('0') << packet.GetId());
			return PacketParseResult::Pass;
		}

		return handler(packet);
	}

	bool BotRealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Reset server seed
			m_serverSeed = 0;

			// Generate a new client seed
			std::uniform_int_distribution<uint32> dist;
			m_clientSeed = dist(RandomGenerator);

			// A reason left over from a previous session must not be reported for this one, nor a
			// request the old connection never had answered.
			m_kickReason.reset();
			for (std::deque<GameTime>& pending : m_pendingRequests)
			{
				pending.clear();
			}
			m_pendingCasts.clear();
			m_lastSpellVisual.reset();
			m_heardSounds.clear();
			m_heardSoundCounts.clear();
			m_subsystemStatus.fill(game::subsystem_status::Unavailable);
			m_lastBugReportResult.clear();

			// Accept LogonChallenge packets from here on
			RegisterPacketHandler(game::realm_client_packet::AuthChallenge, *this, &BotRealmConnector::OnAuthChallenge);

			// The realm may terminate the session at any point from here on, including before we
			// finish authenticating.
			RegisterPacketHandler(game::realm_client_packet::KickReason, *this, &BotRealmConnector::OnKickReason);
			RegisterPacketHandler(game::realm_client_packet::PlaySpellVisual, *this, &BotRealmConnector::OnPlaySpellVisual);
			RegisterPacketHandler(game::realm_client_packet::PlaySoundById, *this, &BotRealmConnector::OnPlaySoundById);
			RegisterPacketHandler(game::realm_client_packet::SubsystemStatus, *this, &BotRealmConnector::OnSubsystemStatus);
			RegisterPacketHandler(game::realm_client_packet::BugReportResult, *this, &BotRealmConnector::OnBugReportResult);
		}
		else
		{
			ELOG("Could not connect to the realm server");
			ConnectFailed();
		}

		return true;
	}

	void BotRealmConnector::connectionLost()
	{
		ELOG("Lost connection to the realm server...");
		ClearPacketHandlers();

		// Questions asked on the old connection will never be answered, and the next session
		// announces its own channel memberships.
		m_pendingNameQueries.clear();
		m_chatChannels.clear();
		m_spellZones.clear();
		m_hasChatChannelList = false;

		Disconnected();
	}

	void BotRealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult BotRealmConnector::connectionPacketReceived(game::IncomingPacket& packet)
	{
		++m_packetsReceived;
		return HandleIncomingPacket(packet);
	}

	void BotRealmConnector::connectionDataSent(const size_t size)
	{
		m_bytesSent += size;
	}

	const char* GetBotRequestName(const bot_request::Type type)
	{
		switch (type)
		{
		case bot_request::Cast: return "cast";
		case bot_request::Loot: return "loot";
		case bot_request::NameQuery: return "name_query";
		case bot_request::QuestHello: return "quest_hello";
		case bot_request::QuestStatus: return "quest_status";
		default: return "unknown";
		}
	}

	namespace
	{
		/// An answer later than this is not paired with its request any more: it was lost, and
		/// pairing the next answer with it would report a latency nobody waited for.
		constexpr GameTime RequestGiveUpMs = 10000;

		/// Outstanding requests kept per kind; a bot never has more than a handful in flight.
		constexpr std::size_t MaxPendingRequests = 32;
	}

	void BotRealmConnector::BeginRequest(const bot_request::Type type)
	{
		std::deque<GameTime>& pending = m_pendingRequests[type];
		const GameTime nowMs = GetAsyncTimeMs();
		while (!pending.empty() && (nowMs - pending.front() > RequestGiveUpMs || pending.size() >= MaxPendingRequests))
		{
			pending.pop_front();
		}

		pending.push_back(nowMs);
	}

	void BotRealmConnector::BeginCastRequest(const uint32 spellId)
	{
		const GameTime nowMs = GetAsyncTimeMs();
		for (auto it = m_pendingCasts.begin(); it != m_pendingCasts.end();)
		{
			it = nowMs - it->second > RequestGiveUpMs ? m_pendingCasts.erase(it) : std::next(it);
		}

		m_pendingCasts[spellId] = nowMs;
	}

	void BotRealmConnector::EndCastRequest(const uint32 spellId)
	{
		const auto it = m_pendingCasts.find(spellId);
		if (it == m_pendingCasts.end())
		{
			return;
		}

		const GameTime latencyMs = GetAsyncTimeMs() - it->second;
		m_pendingCasts.erase(it);
		if (latencyMs <= RequestGiveUpMs)
		{
			RequestAnswered(bot_request::Cast, latencyMs);
		}
	}

	void BotRealmConnector::EndRequest(const bot_request::Type type)
	{
		std::deque<GameTime>& pending = m_pendingRequests[type];
		const GameTime nowMs = GetAsyncTimeMs();
		while (!pending.empty() && nowMs - pending.front() > RequestGiveUpMs)
		{
			pending.pop_front();
		}

		if (pending.empty())
		{
			return;
		}

		const GameTime sentMs = pending.front();
		pending.pop_front();
		RequestAnswered(type, nowMs - sentMs);
	}

	void BotRealmConnector::SetLoginData(const std::string& accountName, const BigNumber& sessionKey)
	{
		m_account = accountName;
		m_sessionKey = sessionKey;
	}

	void BotRealmConnector::ConnectToRealm(const RealmData& data)
	{
		m_realmId = data.id;
		m_realmAddress = data.address;
		m_realmPort = data.port;
		m_realmName = data.name;

		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void BotRealmConnector::Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey)
	{
		m_realmAddress = realmAddress;
		m_realmPort = realmPort;
		m_realmName = realmName;
		m_account = accountName;
		m_sessionKey = sessionKey;

		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void BotRealmConnector::EnterWorld(const CharacterView& character)
	{
		const uint64 guid = character.GetGuid();
		m_selectedCharacterGuid = guid;

		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::EnterWorld);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void BotRealmConnector::CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender, const AvatarConfiguration& customization)
	{
		sendSinglePacket([&name, race, characterClass, characterGender, &customization](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CreateChar);
			packet
				<< io::write_dynamic_range<uint8>(name)
				<< io::write<uint8>(race)
				<< io::write<uint8>(characterClass)
				<< io::write<uint8>(characterGender)
				<< customization;
			packet.Finish();
			});
	}

	void BotRealmConnector::RequestCharEnum()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::CharEnum);
				packet.Finish();
			});
	}

	void BotRealmConnector::DeleteCharacter(const uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::DeleteChar);
				packet << io::write<uint64>(guid);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendChatMessage(const String& message, ChatType chatType, const String& target)
	{
		sendSinglePacket([&message, chatType, &target](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ChatMessage);
				packet
					<< io::write<uint8>(chatType)
					<< io::write_range(message) << io::write<uint8>(0);
				if (chatType == ChatType::Whisper || chatType == ChatType::Channel)
				{
					packet << io::write_dynamic_range<uint8>(target);
				}
				packet.Finish();
			});
	}

	void BotRealmConnector::QueryName(const uint64 guid)
	{
		if (guid == 0 || m_nameCache.count(guid) != 0 || !m_pendingNameQueries.insert(guid).second)
		{
			return;
		}

		BeginRequest(bot_request::NameQuery);
		sendSinglePacket([guid](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::NameQuery);
				packet << io::write_packed_guid(guid);
				packet.Finish();
			});
	}

	const std::string* BotRealmConnector::GetCachedName(const uint64 guid) const
	{
		const auto it = m_nameCache.find(guid);
		return it != m_nameCache.end() ? &it->second : nullptr;
	}

	void BotRealmConnector::JoinChatChannel(const std::string& name)
	{
		sendSinglePacket([&name](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ChannelJoin);
				packet << io::write_dynamic_range<uint8>(name);
				packet.Finish();
			});
	}

	uint32 BotRealmConnector::FindChatChannelId(const std::string& name) const
	{
		const auto equalsIgnoreCase = [](const std::string& a, const std::string& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y)
				{
					return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				});
		};

		for (const auto& [id, channelName] : m_chatChannels)
		{
			if (equalsIgnoreCase(channelName, name))
			{
				return id;
			}
		}

		return 0;
	}

	void BotRealmConnector::SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info)
	{
		sendSinglePacket([characterId, opCode, &info](game::OutgoingPacket& packet) {
			packet.Start(opCode);
			packet << io::write<uint64>(characterId);
			packet << info;
			packet.Finish();
			});
	}

	void BotRealmConnector::SendTimeSyncResponse(uint32 syncIndex, GameTime clientTimestamp)
	{
		sendSinglePacket([syncIndex, clientTimestamp](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TimeSyncResponse);
				packet << io::write<uint32>(syncIndex) << io::write<uint64>(clientTimestamp);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMoveWorldPortAck()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveWorldPortAck);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo)
	{
		sendSinglePacket([type, ackId, speed, &movementInfo](game::OutgoingPacket& packet)
			{
				static const uint16 moveOpCodes[MovementType::Count] = {
					game::client_realm_packet::ForceMoveSetWalkSpeedAck,
					game::client_realm_packet::ForceMoveSetRunSpeedAck,
					game::client_realm_packet::ForceMoveSetRunBackSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimBackSpeedAck,
					game::client_realm_packet::ForceMoveSetTurnRateAck,
					game::client_realm_packet::ForceSetFlightSpeedAck,
					game::client_realm_packet::ForceSetFlightBackSpeedAck
				};

				packet.Start(moveOpCodes[type]);
				packet << io::write<uint32>(ackId) << movementInfo << io::write<float>(speed);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::MoveTeleportAck);
				packet << io::write<uint32>(ackId) << movementInfo;
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMoveChargeAck(uint32 ackId, const MovementInfo& movementInfo, float speed)
	{
		sendSinglePacket([ackId, &movementInfo, speed](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::MoveChargeAck);
				packet << io::write<uint32>(ackId) << movementInfo << io::write<float>(speed);
				packet.Finish();
			});
	}

	bool BotRealmConnector::SendCastSpell(const uint32 spellId, const SpellTargetMap& targetMap, const bool autoFlush)
	{
		if (spellId == 0)
		{
			UpdateSpellStateIssue("cast_spell_invalid_spell_id");
			if (BotUnit* self = GetSelfMutable())
			{
				self->SetLastCastFailed(0, spell_cast_result::FailedNotKnown, GetAsyncTimeMs());
			}
			return false;
		}

		if (!IsCastTargetLocallyValid(targetMap))
		{
			UpdateSpellStateIssue("cast_spell_invalid_target");
			if (BotUnit* self = GetSelfMutable())
			{
				self->SetLastCastFailed(spellId, spell_cast_result::FailedBadTargets, GetAsyncTimeMs());
			}
			return false;
		}

		const GameTime nowMs = GetAsyncTimeMs();
		if (BotUnit* self = GetSelfMutable())
		{
			if (!self->KnowsSpell(spellId))
			{
				UpdateSpellStateIssue("cast_spell_unknown_spell");
				self->SetLastCastFailed(spellId, spell_cast_result::FailedNotKnown, nowMs);
				return false;
			}

			self->SetLastCastPending(spellId, targetMap.GetTargetMap(), targetMap.GetUnitTarget(), nowMs);
		}
		ClearSpellStateIssue();

		BeginCastRequest(spellId);
		if (autoFlush)
		{
			sendSinglePacket([spellId, &targetMap](game::OutgoingPacket& packet)
				{
					packet.Start(game::client_realm_packet::CastSpell);
					packet << io::write<uint32>(spellId) << targetMap;
					packet.Finish();
				});
		}
		else
		{
			io::StringSink sink(getSendBuffer());
			game::OutgoingPacket packet(sink);
			packet.Start(game::client_realm_packet::CastSpell);
			packet << io::write<uint32>(spellId) << targetMap;
			packet.Finish();
		}
		return true;
	}

	void BotRealmConnector::PrimeWorldSessionForTesting(const uint64 selectedGuid)
	{
		if (selectedGuid != 0)
		{
			m_selectedCharacterGuid = selectedGuid;
			m_objectManager.SetSelfGuid(selectedGuid);
		}

		RegisterWorldPacketHandlers();
	}

	BotUnit* BotRealmConnector::GetSelfMutable()
	{
		return m_objectManager.GetSelfMutable();
	}

	const BotUnit* BotRealmConnector::GetSelf() const
	{
		return m_objectManager.GetSelf();
	}

	void BotRealmConnector::UpdateSpellStateIssue(const std::string& issue)
	{
		m_lastSpellStateIssue = issue;
		WLOG("bot spell-state issue=" << issue);
	}

	void BotRealmConnector::ClearSpellStateIssue()
	{
		m_lastSpellStateIssue.clear();
	}

	void BotRealmConnector::RegisterWorldPacketHandlers()
	{
		RegisterPacketHandler(game::realm_client_packet::CharEnum, *this, &BotRealmConnector::OnCharEnum);
		RegisterPacketHandler(game::realm_client_packet::LoginVerifyWorld, *this, &BotRealmConnector::OnLoginVerifyWorld);
		RegisterPacketHandler(game::realm_client_packet::EnterWorldFailed, *this, &BotRealmConnector::OnEnterWorldFailed);
		RegisterPacketHandler(game::realm_client_packet::TimeSyncRequest, *this, &BotRealmConnector::OnTimeSyncRequest);
		RegisterPacketHandler(game::realm_client_packet::TransferPending, *this, &BotRealmConnector::OnTransferPending);
		RegisterPacketHandler(game::realm_client_packet::NewWorld, *this, &BotRealmConnector::OnNewWorld);
		RegisterPacketHandler(game::realm_client_packet::CharCreateResponse, *this, &BotRealmConnector::OnCharCreateResponse);
		RegisterPacketHandler(game::realm_client_packet::MoveTeleportAck, *this, &BotRealmConnector::OnMoveTeleport);
		RegisterPacketHandler(game::realm_client_packet::MoveCharge, *this, &BotRealmConnector::OnMoveCharge);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetWalkSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetRunSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetRunBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetSwimSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetSwimBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceMoveSetTurnRate, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceSetFlightSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
		RegisterPacketHandler(game::realm_client_packet::ForceSetFlightBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);

		// Register party invitation handler
		RegisterPacketHandler(game::realm_client_packet::GroupInvite, *this, &BotRealmConnector::OnGroupInvite);

		// Register party state handlers
		RegisterPacketHandler(game::realm_client_packet::GroupList, *this, &BotRealmConnector::OnGroupList);
		RegisterPacketHandler(game::realm_client_packet::GroupDestroyed, *this, &BotRealmConnector::OnGroupDestroyed);
		RegisterPacketHandler(game::realm_client_packet::GroupSetLeader, *this, &BotRealmConnector::OnGroupSetLeader);
		RegisterPacketHandler(game::realm_client_packet::PartyMemberStats, *this, &BotRealmConnector::OnPartyMemberStats);

		// Register handlers for common packets that can be safely ignored by the bot
		RegisterPacketHandler(game::realm_client_packet::UpdateObject, *this, &BotRealmConnector::OnUpdateObject);
		RegisterPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &BotRealmConnector::OnCompressedUpdateObject);
		RegisterPacketHandler(game::realm_client_packet::DestroyObjects, *this, &BotRealmConnector::OnDestroyObjects);
		RegisterPacketHandler(game::realm_client_packet::DebugLineOfSightResult, *this, &BotRealmConnector::OnDebugLineOfSightResult);
		RegisterPacketHandler(game::realm_client_packet::NameQueryResult, *this, &BotRealmConnector::OnNameQueryResult);
		RegisterPacketHandler(game::realm_client_packet::ChatMessage, *this, &BotRealmConnector::OnChatMessage);
		RegisterPacketHandler(game::realm_client_packet::LootResponse, *this, &BotRealmConnector::OnLootResponse);
		RegisterPacketHandler(game::realm_client_packet::SpellZoneStart, *this, &BotRealmConnector::OnSpellZoneStart);
		RegisterPacketHandler(game::realm_client_packet::SpellZoneEnd, *this, &BotRealmConnector::OnSpellZoneEnd);
		RegisterPacketHandler(game::realm_client_packet::StartLootRoll, *this, &BotRealmConnector::OnStartLootRoll);
		RegisterPacketHandler(game::realm_client_packet::SetProficiency, *this, &BotRealmConnector::OnSetProficiency);
		RegisterPacketHandler(game::realm_client_packet::ReviveRequest, *this, &BotRealmConnector::OnReviveOffer);
		RegisterPacketHandler(game::realm_client_packet::MoveRoot, *this, &BotRealmConnector::OnMovementControl);
		RegisterPacketHandler(game::realm_client_packet::MoveStun, *this, &BotRealmConnector::OnMovementControl);
		RegisterPacketHandler(game::realm_client_packet::MoveSleep, *this, &BotRealmConnector::OnMovementControl);
		RegisterPacketHandler(game::realm_client_packet::MoveFear, *this, &BotRealmConnector::OnMovementControl);
		RegisterPacketHandler(game::realm_client_packet::MoveDisorient, *this, &BotRealmConnector::OnMovementControl);
		RegisterPacketHandler(game::realm_client_packet::InventoryError, *this, &BotRealmConnector::OnInventoryError);
		RegisterPacketHandler(game::realm_client_packet::QuestGiverStatus, *this, &BotRealmConnector::OnQuestGiverStatus);
		RegisterPacketHandler(game::realm_client_packet::QuestGiverQuestList, *this, &BotRealmConnector::OnQuestGiverQuestList);
		RegisterPacketHandler(game::realm_client_packet::QuestGiverQuestDetails, *this, &BotRealmConnector::OnQuestGiverQuestDetails);
		RegisterPacketHandler(game::realm_client_packet::QuestGiverOfferReward, *this, &BotRealmConnector::OnQuestGiverOfferReward);
		RegisterPacketHandler(game::realm_client_packet::QuestGiverQuestComplete, *this, &BotRealmConnector::OnQuestGiverQuestComplete);
		RegisterPacketHandler(game::realm_client_packet::QuestAccepted, *this, &BotRealmConnector::OnQuestAccepted);
		RegisterPacketHandler(game::realm_client_packet::QuestLogFull, *this, &BotRealmConnector::OnQuestLogFull);
		RegisterPacketHandler(game::realm_client_packet::TradeInvite, *this, &BotRealmConnector::OnTradeInvite);
		RegisterPacketHandler(game::realm_client_packet::TradeRequestResult, *this, &BotRealmConnector::OnTradeRequestResult);
		RegisterPacketHandler(game::realm_client_packet::TradeSessionOpened, *this, &BotRealmConnector::OnTradeSessionOpened);
		RegisterPacketHandler(game::realm_client_packet::TradeSessionClosed, *this, &BotRealmConnector::OnTradeSessionClosed);
		RegisterPacketHandler(game::realm_client_packet::ShowMailbox, *this, &BotRealmConnector::OnShowMailbox);
		RegisterPacketHandler(game::realm_client_packet::MailList, *this, &BotRealmConnector::OnMailList);
		RegisterPacketHandler(game::realm_client_packet::MailSendResult, *this, &BotRealmConnector::OnMailSendResult);
		RegisterPacketHandler(game::realm_client_packet::MailTakeResult, *this, &BotRealmConnector::OnMailTakeResult);
		RegisterPacketHandler(game::realm_client_packet::LogoutResponse, *this, &BotRealmConnector::OnLogoutResponse);
		RegisterPacketHandler(game::realm_client_packet::ChannelList, *this, &BotRealmConnector::OnChannelList);
		RegisterPacketHandler(game::realm_client_packet::ChannelNotify, *this, &BotRealmConnector::OnChannelNotify);

		// Movement packets - these contain position updates for other units
		RegisterPacketHandler(game::realm_client_packet::MoveStartForward, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStartBackward, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStop, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStartStrafeLeft, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStartStrafeRight, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStopStrafe, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStartTurnLeft, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStartTurnRight, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveStopTurn, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveHeartBeat, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetFacing, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveJump, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveFallLand, *this, &BotRealmConnector::OnMovementPacket);
		RegisterPacketHandler(game::realm_client_packet::CreatureMove, *this, &BotRealmConnector::OnCreatureMove);

		// Spell/state packets
		RegisterPacketHandler(game::realm_client_packet::InitialSpells, *this, &BotRealmConnector::OnInitialSpells);
		RegisterPacketHandler(game::realm_client_packet::LearnedSpell, *this, &BotRealmConnector::OnLearnedSpell);
		RegisterPacketHandler(game::realm_client_packet::TradeSkills, *this, &BotRealmConnector::OnTradeSkills);
		RegisterPacketHandler(game::realm_client_packet::TradeSkillUpdate, *this, &BotRealmConnector::OnTradeSkillUpdate);
		RegisterPacketHandler(game::realm_client_packet::UnlearnedSpell, *this, &BotRealmConnector::OnUnlearnedSpell);
		RegisterPacketHandler(game::realm_client_packet::SpellStart, *this, &BotRealmConnector::OnSpellStart);
		RegisterPacketHandler(game::realm_client_packet::SpellGo, *this, &BotRealmConnector::OnSpellGo);
		RegisterPacketHandler(game::realm_client_packet::SpellFailure, *this, &BotRealmConnector::OnSpellFailure);
		RegisterPacketHandler(game::realm_client_packet::SpellCooldown, *this, &BotRealmConnector::OnSpellCooldown);
		RegisterPacketHandler(game::realm_client_packet::AuraUpdate, *this, &BotRealmConnector::OnAuraUpdate);
		RegisterPacketHandler(game::realm_client_packet::ChannelStart, *this, &BotRealmConnector::OnChannelStart);
		RegisterPacketHandler(game::realm_client_packet::ChannelUpdate, *this, &BotRealmConnector::OnChannelUpdate);

		// These packets can be safely ignored
		RegisterPacketHandler(game::realm_client_packet::ActionButtons, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetWalkSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetRunSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetRunBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetSwimSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetSwimBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::MoveSetTurnRate, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::SetFlightSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::SetFlightBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
		RegisterPacketHandler(game::realm_client_packet::GameTimeInfo, *this, &BotRealmConnector::OnGameTimeInfo);
		RegisterPacketHandler(game::realm_client_packet::ShutdownCountdown, *this, &BotRealmConnector::OnShutdownCountdown);
		RegisterPacketHandler(game::realm_client_packet::CastFailed, *this, &BotRealmConnector::OnIgnoredPacket);

		// Combat packet handlers
		RegisterPacketHandler(game::realm_client_packet::AttackStart, *this, &BotRealmConnector::OnAttackStart);
		RegisterPacketHandler(game::realm_client_packet::AttackStop, *this, &BotRealmConnector::OnAttackStop);
		RegisterPacketHandler(game::realm_client_packet::AttackSwingError, *this, &BotRealmConnector::OnAttackSwingError);
		RegisterPacketHandler(game::realm_client_packet::AttackerStateUpdate, *this, &BotRealmConnector::OnAttackerStateUpdate);
		RegisterPacketHandler(game::realm_client_packet::NonSpellDamageLog, *this, &BotRealmConnector::OnNonSpellDamageLog);
	}

	PacketParseResult BotRealmConnector::OnAuthChallenge(game::IncomingPacket& packet)
	{
		// No longer handle LogonChallenge packets during this session
		ClearPacketHandler(game::realm_client_packet::AuthChallenge);

		// Try to read the packet data
		if (!(packet >> io::read<uint32>(m_serverSeed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Calculate a hash for verification
		HashGeneratorSha1 hashGen;
		hashGen.update(m_account.data(), m_account.length());
		hashGen.update(reinterpret_cast<const char*>(&m_clientSeed), sizeof(m_clientSeed));
		hashGen.update(reinterpret_cast<const char*>(&m_serverSeed), sizeof(m_serverSeed));
		Sha1_Add_BigNumbers(hashGen, { m_sessionKey });
		SHA1Hash hash = hashGen.finalize();

		// Listen for response packet
		RegisterPacketHandler(game::realm_client_packet::AuthSessionResponse, *this, &BotRealmConnector::OnAuthSessionResponse);

		// We have been challenged, respond with an answer
		sendSinglePacket([this, &hash](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AuthSession);
			packet
				<< io::write<uint32>(mmo::Revision)
				<< io::write_dynamic_range<uint8>(this->m_account)
				<< io::write<uint32>(m_clientSeed)
				<< io::write_range(hash)
				<< io::write<uint32>(LocaleFourCC(LocaleIndex::enUS));	// Locale (the harness always runs enUS)
			packet.Finish();
			});

		ILOG("[Realm] Handshaking...");
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnKickReason(game::IncomingPacket& packet)
	{
		uint8 reason = 0;
		if (!(packet >> io::read<uint8>(reason)))
		{
			return PacketParseResult::Disconnect;
		}

		if (reason >= auth::session_kick_reason::Count_)
		{
			WLOG("Realm sent unknown session kick reason " << static_cast<uint16>(reason));
			return PacketParseResult::Pass;
		}

		m_kickReason = static_cast<auth::SessionKickReason>(reason);
		ILOG("Realm terminated this session, reason code " << static_cast<uint16>(reason));

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnPlaySpellVisual(game::IncomingPacket& packet)
	{
		SpellVisual visual;
		if (!(packet
			>> io::read_packed_guid(visual.targetGuid)
			>> io::read<uint32>(visual.visualizationId)
			>> io::read<uint8>(visual.visualEvent)))
		{
			return PacketParseResult::Disconnect;
		}

		m_lastSpellVisual = visual;
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnPlaySoundById(game::IncomingPacket& packet)
	{
		uint32 soundId = 0;
		uint8 source = 0;
		if (!(packet >> io::read<uint32>(soundId) >> io::read<uint8>(source)) || source > game::play_sound_source::Unit)
		{
			return PacketParseResult::Disconnect;
		}

		uint64 sourceGuid = 0;
		if (source == game::play_sound_source::Unit && !(packet >> io::read_packed_guid(sourceGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		m_heardSounds[soundId] = sourceGuid;
		++m_heardSoundCounts[soundId];
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSubsystemStatus(game::IncomingPacket& packet)
	{
		uint8 count = 0;
		if (!(packet >> io::read<uint8>(count)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint8 i = 0; i < count; ++i)
		{
			uint8 id = 0;
			uint8 status = 0;
			if (!(packet >> io::read<uint8>(id) >> io::read<uint8>(status)))
			{
				return PacketParseResult::Disconnect;
			}

			if (id < game::subsystem::Count_)
			{
				m_subsystemStatus[id] = status == game::subsystem_status::Available ? game::subsystem_status::Available : game::subsystem_status::Unavailable;
			}
		}

		++m_subsystemStatusPackets;
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnBugReportResult(game::IncomingPacket& packet)
	{
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		switch (result)
		{
		case game::bug_report_result::Accepted: m_lastBugReportResult = "ACCEPTED"; break;
		case game::bug_report_result::RateLimited: m_lastBugReportResult = "RATE_LIMITED"; break;
		case game::bug_report_result::TooLarge: m_lastBugReportResult = "TOO_LARGE"; break;
		case game::bug_report_result::Disabled: m_lastBugReportResult = "DISABLED"; break;
		default: m_lastBugReportResult = "INVALID"; break;
		}

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::SubmitBugReport(const game::BugReportPayload& payload)
	{
		sendSinglePacket([&payload](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::BugReport);
			packet << payload;
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatSetSubsystem(const game::Subsystem subsystem, const bool enabled)
	{
		sendSinglePacket([subsystem, enabled](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSetSubsystem);
			packet << io::write<uint8>(subsystem) << io::write<uint8>(enabled ? 1 : 0);
			packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnAuthSessionResponse(game::IncomingPacket& packet)
	{
		// No longer accept these packets from here on!
		ClearPacketHandler(game::realm_client_packet::AuthSessionResponse);

		// Try to read packet data
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		AuthenticationResult(result);

		if (result == game::auth_result::Success)
		{
			HMACHash cryptKey;
			game::Crypt::GenerateKey(cryptKey, m_sessionKey);

			game::Crypt& crypt = GetCrypt();
			crypt.SetKey(cryptKey.data(), cryptKey.size());
			crypt.Init();

			RegisterWorldPacketHandlers();
			RequestCharEnum();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnCharEnum(game::IncomingPacket& packet)
	{
		m_characterViews.clear();

		if (!(packet >> io::read_container<uint8>(m_characterViews)))
		{
			return PacketParseResult::Disconnect;
		}

		CharListUpdated();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnLoginVerifyWorld(game::IncomingPacket& packet)
	{
		uint32 mapId;
		Vector3 position;
		float facing;

		if (!(packet >> io::read<uint32>(mapId)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(facing)))
		{
			return PacketParseResult::Disconnect;
		}

		m_movementInfo.position = position;
		m_movementInfo.facing = Radian(facing);
		m_movementInfo.timestamp = GetAsyncTimeMs();

		m_awaitingWorldSpawn = false;
		VerifyNewWorld(mapId, position, facing);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnEnterWorldFailed(game::IncomingPacket& packet)
	{
		game::player_login_response::Type response;
		if (!(packet >> io::read<uint8>(response)))
		{
			return PacketParseResult::Disconnect;
		}

		ELOG("Failed to enter world: " << response);
		EnterWorldFailed(response);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTimeSyncRequest(game::IncomingPacket& packet)
	{
		uint32 syncIndex;
		if (!(packet >> io::read<uint32>(syncIndex)))
		{
			ELOG("Failed to read TimeSyncRequest packet!");
			return PacketParseResult::Disconnect;
		}

		GameTime clientTimestamp = GetAsyncTimeMs();
		SendTimeSyncResponse(syncIndex, clientTimestamp);

		// A world node asks right after it spawned the bot, and takes movement once it has the
		// answer: after a transfer, this is the moment the bot is in the new world.
		if (m_awaitingWorldSpawn)
		{
			m_awaitingWorldSpawn = false;
			VerifyNewWorld(m_transferMapId, m_movementInfo.position, m_transferFacing);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTransferPending(game::IncomingPacket& packet)
	{
		uint32 mapId;
		if (!(packet >> io::read<uint32>(mapId)))
		{
			ELOG("Failed to read TransferPending packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer pending to map " << mapId << "...");
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnNewWorld(game::IncomingPacket& packet)
	{
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(packet >> io::read<uint32>(mapId) >> io::read<float>(position.x) >> io::read<float>(position.y) >> io::read<float>(position.z) >> io::read<float>(facingRadianVal)))
		{
			ELOG("Failed to read NewWorld packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer to map " << mapId << " completed.");
		m_movementInfo.position = position;
		m_movementInfo.facing = Radian(facingRadianVal);
		m_movementInfo.timestamp = GetAsyncTimeMs();

		// The ack is what makes the realm hand the bot to the new world node, and that node takes
		// no movement until it has spawned the bot and had its time sync request answered.
		// Anything sent before then is dropped - the landing that VerifyNewWorld sends among it -
		// and a bot whose landing was dropped is falling for the server for good, which refuses
		// every movement packet it sends from then on. So the world is verified once the new
		// node's time sync request is answered (OnTimeSyncRequest), not here.
		// Nothing the bot saw on the old map exists on the new one, and the new node spawns all of
		// it afresh. Kept, the old units are targets the bot goes on swinging at, and the server
		// answers each swing with "Failed to find target object".
		m_objectManager.Clear();
		m_spellZones.clear();
		m_isAutoAttacking = false;
		m_autoAttackTargetGuid = 0;

		m_awaitingWorldSpawn = true;
		m_transferMapId = mapId;
		m_transferPosition = position;
		m_transferFacing = facingRadianVal;
		SendMoveWorldPortAck();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnCharCreateResponse(game::IncomingPacket& packet)
	{
		game::CharCreateResult result;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		CharacterCreated(result);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMoveTeleport(game::IncomingPacket& packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			ELOG("Failed to read teleported mover guid!");
			return PacketParseResult::Disconnect;
		}

		uint32 ackId;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint32>(ackId) >> movementInfo))
		{
			ELOG("Failed to read move teleport packet");
			return PacketParseResult::Disconnect;
		}

		if (m_selectedCharacterGuid == guid)
		{
			// Whatever the bot was swinging at, it is not any more. The server does not always
			// say so - a revive teleports the bot without an AttackStop - and a flag left set
			// kept the bot "in combat" for good, where nothing walks it anywhere.
			m_isAutoAttacking = false;
			m_autoAttackTargetGuid = 0;

			m_movementInfo = movementInfo;
			SendMoveTeleportAck(ackId, m_movementInfo);

			// Keep the self unit's cached position in sync so distance/position
			// queries reflect the teleport immediately.
			if (BotUnit* selfUnit = m_objectManager.GetSelfMutable())
			{
				selfUnit->SetMovementInfo(m_movementInfo);
			}

			Teleported();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMoveCharge(game::IncomingPacket& packet)
	{
		uint32 ackId;
		float speed;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			ELOG("Failed to read move charge packet");
			return PacketParseResult::Disconnect;
		}

		// Stop local movement (the server takes over) and acknowledge so the charge starts.
		m_movementInfo.movementFlags &= ~(movement_flags::Moving | movement_flags::Strafing |
			movement_flags::Turning | movement_flags::PositionChanging);
		SendMoveChargeAck(ackId, m_movementInfo, speed);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnForceMovementSpeedChange(game::IncomingPacket& packet)
	{
		uint32 ackId;
		float speed;

		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			WLOG("Failed to read force movement speed change packet!");
			return PacketParseResult::Disconnect;
		}

		const MovementType type = MovementTypeFromForcePacket(packet.GetId());
		if (BotUnit* self = m_objectManager.GetSelfMutable())
		{
			self->SetSpeed(type, speed);
		}

		// A slow or a sprint applies to the bot's own unit, which is what the movement controller
		// takes its speed from: a bot that ran on at its old speed after a slow tripped the
		// server's speed check, again and again, until the server kicked it.
		MovementInfo info = m_liveMovement ? m_liveMovement() : m_movementInfo;
		info.timestamp = GetAsyncTimeMs();
		SendMovementSpeedAck(type, ackId, speed, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnIgnoredPacket(game::IncomingPacket& packet)
	{
		// Silently ignore this packet - it's not important for the bot's functionality
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnGroupInvite(game::IncomingPacket& packet)
	{
		String inviterName;
		if (!(packet >> io::read_container<uint8>(inviterName)))
		{
			ELOG("Failed to read GroupInvite packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Received party invitation from " << inviterName);
		m_hasPendingPartyInvitation = true;

		// Trigger the signal for the bot profile to handle
		PartyInvitationReceived(inviterName);

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::AcceptPartyInvitation()
	{
		m_hasPendingPartyInvitation = false;
		// Send GroupAccept packet to the server
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupAccept);
			packet.Finish();
		});

		ILOG("Accepted party invitation");
	}

	void BotRealmConnector::DeclinePartyInvitation()
	{
		m_hasPendingPartyInvitation = false;
		// Send GroupDecline packet to the server
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupDecline);
			packet.Finish();
		});

		ILOG("Declined party invitation");
	}

	const BotPartyMember* BotRealmConnector::GetPartyMember(uint32 index) const
	{
		if (!m_inParty || index >= m_partyMembers.size())
		{
			return nullptr;
		}

		return &m_partyMembers[index];
	}

	const BotPartyMember* BotRealmConnector::GetPartyMemberByGuid(const uint64 guid) const
	{
		if (!m_inParty || guid == 0)
		{
			return nullptr;
		}

		const auto it = std::find_if(m_partyMembers.begin(), m_partyMembers.end(), [guid](const BotPartyMember& member)
		{
			return member.guid == guid;
		});

		return it != m_partyMembers.end() ? &*it : nullptr;
	}

	std::vector<uint64> BotRealmConnector::GetPartyMemberGuids() const
	{
		std::vector<uint64> guids;
		if (m_inParty)
		{
			guids.reserve(m_partyMembers.size());
			for (const auto& member : m_partyMembers)
			{
				guids.push_back(member.guid);
			}
		}
		return guids;
	}

	void BotRealmConnector::ResetInstances()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::ResetInstances);
			packet.Finish();
		});
	}

	void BotRealmConnector::LeaveParty()
	{
		if (!m_inParty)
		{
			WLOG("Cannot leave party: Not in a party");
			return;
		}

		// GroupLeave, as the game client sends it. A GroupUninvite with an empty name is a kick
		// of nobody: the realm answers a member with YouNotLeader and a leader with
		// NotInYourParty, and the bot never left.
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupLeave);
			packet.Finish();
		});

		ILOG("Left party");
	}

	void BotRealmConnector::KickFromParty(const std::string& playerName)
	{
		if (!m_inParty)
		{
			WLOG("Cannot kick from party: Not in a party");
			return;
		}

		if (!IsPartyLeader())
		{
			WLOG("Cannot kick from party: Not the party leader");
			return;
		}

		sendSinglePacket([&playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupUninvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});

		ILOG("Kicked " << playerName << " from party");
	}

	void BotRealmConnector::InviteToParty(const std::string& playerName)
	{
		sendSinglePacket([&playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupInvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});

		ILOG("Invited " << playerName << " to party");
	}

	void BotRealmConnector::AutoEquipItem(const uint8 bag, const uint8 slot)
	{
		sendSinglePacket([bag, slot](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::AutoEquipItem);
			packet
				<< io::write<uint8>(bag)
				<< io::write<uint8>(slot);
			packet.Finish();
		});
	}

	void BotRealmConnector::UseItem(const uint8 bag, const uint8 slot, const uint64 itemGuid, const SpellTargetMap& targetMap)
	{
		sendSinglePacket([bag, slot, itemGuid, &targetMap](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::UseItem);
			packet
				<< io::write<uint8>(bag)
				<< io::write<uint8>(slot)
				<< io::write<uint64>(itemGuid)
				<< targetMap;
			packet.Finish();
		});
	}

	void BotRealmConnector::DestroyItem(const uint8 bag, const uint8 slot, const uint8 count)
	{
		sendSinglePacket([bag, slot, count](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::DestroyItem);
			packet
				<< io::write<uint8>(bag)
				<< io::write<uint8>(slot)
				<< io::write<uint8>(count);
			packet.Finish();
		});
	}

	void BotRealmConnector::SwapInventoryItems(const uint8 srcSlot, const uint8 dstSlot)
	{
		sendSinglePacket([srcSlot, dstSlot](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::SwapInvItem);
			packet
				<< io::write<uint8>(srcSlot)
				<< io::write<uint8>(dstSlot);
			packet.Finish();
		});
	}

	PacketParseResult BotRealmConnector::OnGroupList(game::IncomingPacket& packet)
	{
		uint8 groupType = 0;
		uint8 isAssistant = 0;
		uint8 memberCount = 0;

		if (!(packet >> io::read<uint8>(groupType) >> io::read<uint8>(isAssistant) >> io::read<uint8>(memberCount)))
		{
			ELOG("Failed to read GroupList packet header!");
			return PacketParseResult::Disconnect;
		}

		const bool wasInParty = m_inParty;

		const std::vector<BotPartyMember> previousMembers = m_partyMembers;

		// Read party members
		m_partyMembers.clear();
		m_partyMembers.resize(memberCount);

		for (uint8 i = 0; i < memberCount; ++i)
		{
			String memberName;
			if (!(packet 
				>> io::read_container<uint8>(memberName)
				>> io::read<uint64>(m_partyMembers[i].guid)
				>> io::read<uint8>(m_partyMembers[i].status)
				>> io::read<uint8>(m_partyMembers[i].group)
				>> io::read<uint8>(m_partyMembers[i].assistant)))
			{
				ELOG("Failed to read GroupList member data!");
				return PacketParseResult::Disconnect;
			}
			m_partyMembers[i].name = memberName;

			const auto previous = std::find_if(previousMembers.begin(), previousMembers.end(), [guid = m_partyMembers[i].guid](const BotPartyMember& member)
			{
				return member.guid == guid;
			});
			if (previous != previousMembers.end())
			{
				m_partyMembers[i].health = previous->health;
				m_partyMembers[i].maxHealth = previous->maxHealth;
				m_partyMembers[i].powerType = previous->powerType;
				m_partyMembers[i].power = previous->power;
				m_partyMembers[i].maxPower = previous->maxPower;
				m_partyMembers[i].level = previous->level;
				m_partyMembers[i].position = previous->position;
				m_partyMembers[i].hasHealth = previous->hasHealth;
				m_partyMembers[i].hasPower = previous->hasPower;
				m_partyMembers[i].hasLevel = previous->hasLevel;
				m_partyMembers[i].hasPosition = previous->hasPosition;
			}
		}

		// Read leader guid
		if (!(packet >> io::read<uint64>(m_partyLeaderGuid)))
		{
			ELOG("Failed to read GroupList leader guid!");
			return PacketParseResult::Disconnect;
		}

		// We're in a party if group type is not 0 (none)
		m_inParty = (groupType != 0);

		ILOG("GroupList received: " << static_cast<int>(memberCount) << " members, leader: " << m_partyLeaderGuid);

		// Fire signal if we just joined a party
		if (m_inParty && !wasInParty)
		{
			PartyJoined(m_partyLeaderGuid, static_cast<uint32>(memberCount));
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnGroupDestroyed(game::IncomingPacket& packet)
	{
		ILOG("Party has been disbanded");

		m_partyMembers.clear();
		m_partyLeaderGuid = 0;
		m_inParty = false;

		PartyLeft();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnGroupSetLeader(game::IncomingPacket& packet)
	{
		uint64 newLeaderGuid = 0;
		if (!(packet >> io::read<uint64>(newLeaderGuid)))
		{
			ELOG("Failed to read GroupSetLeader packet!");
			return PacketParseResult::Disconnect;
		}

		m_partyLeaderGuid = newLeaderGuid;
		ILOG("Party leader changed to " << newLeaderGuid);
		PartyJoined(m_partyLeaderGuid, static_cast<uint32>(m_partyMembers.size()));
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnPartyMemberStats(game::IncomingPacket& packet)
	{
		uint64 playerGuid = 0;
		uint32 updateFlags = 0;
		if (!(packet >> io::read_packed_guid(playerGuid) >> io::read<uint32>(updateFlags)))
		{
			ELOG("Failed to read PartyMemberStats header!");
			return PacketParseResult::Disconnect;
		}

		const auto it = std::find_if(m_partyMembers.begin(), m_partyMembers.end(), [playerGuid](const BotPartyMember& member)
		{
			return member.guid == playerGuid;
		});
		if (it == m_partyMembers.end())
		{
			WLOG("Unable to find party member " << playerGuid << " for party member stats update");
			return PacketParseResult::Pass;
		}

		BotPartyMember& member = *it;
		if (updateFlags & group_update_flags::Status)
		{
			uint16 status = 0;
			if (!(packet >> io::read<uint16>(status)))
			{
				ELOG("Failed to read PartyMemberStats status");
				return PacketParseResult::Disconnect;
			}
			member.status = status;
		}

		if (updateFlags & group_update_flags::CurrentHP)
		{
			if (!(packet >> io::read<uint16>(member.health)))
			{
				ELOG("Failed to read PartyMemberStats health");
				return PacketParseResult::Disconnect;
			}
			member.hasHealth = true;
		}

		if (updateFlags & group_update_flags::MaxHP)
		{
			if (!(packet >> io::read<uint16>(member.maxHealth)))
			{
				ELOG("Failed to read PartyMemberStats max health");
				return PacketParseResult::Disconnect;
			}
			member.hasHealth = true;
		}

		if (updateFlags & group_update_flags::PowerType)
		{
			if (!(packet >> io::read<uint8>(member.powerType)))
			{
				ELOG("Failed to read PartyMemberStats power type");
				return PacketParseResult::Disconnect;
			}
			member.hasPower = true;
		}

		if (updateFlags & group_update_flags::CurrentPower)
		{
			if (!(packet >> io::read<uint16>(member.power)))
			{
				ELOG("Failed to read PartyMemberStats power");
				return PacketParseResult::Disconnect;
			}
			member.hasPower = true;
		}

		if (updateFlags & group_update_flags::MaxPower)
		{
			if (!(packet >> io::read<uint16>(member.maxPower)))
			{
				ELOG("Failed to read PartyMemberStats max power");
				return PacketParseResult::Disconnect;
			}
			member.hasPower = true;
		}

		if (updateFlags & group_update_flags::Level)
		{
			if (!(packet >> io::read<uint16>(member.level)))
			{
				ELOG("Failed to read PartyMemberStats level");
				return PacketParseResult::Disconnect;
			}
			member.hasLevel = true;
		}

		if (updateFlags & group_update_flags::Zone)
		{
			if (!(packet >> io::skip<uint16>()))
			{
				ELOG("Failed to read PartyMemberStats zone");
				return PacketParseResult::Disconnect;
			}
		}

		if (updateFlags & group_update_flags::Position)
		{
			if (!(packet
				>> io::read<float>(member.position.x)
				>> io::read<float>(member.position.y)
				>> io::read<float>(member.position.z)))
			{
				ELOG("Failed to read PartyMemberStats position");
				return PacketParseResult::Disconnect;
			}
			member.hasPosition = true;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnUpdateObject(game::IncomingPacket& packet)
	{
		return HandleObjectUpdate(packet);
	}

	PacketParseResult BotRealmConnector::OnCompressedUpdateObject(game::IncomingPacket& packet)
	{
		uint32 uncompressedSize = 0;
		if (!(packet >> io::read<uint32>(uncompressedSize)))
		{
			ELOG("Failed to read uncompressed size of compressed update object packet!");
			return PacketParseResult::Disconnect;
		}

		if (uncompressedSize == 0)
		{
			ELOG("Compressed update object packet declares an empty payload!");
			return PacketParseResult::Disconnect;
		}

		const uint32 compressedSize = packet.GetSize() - sizeof(uint32);
		if (compressedSize == 0)
		{
			ELOG("Compressed update object packet has no payload!");
			return PacketParseResult::Disconnect;
		}

		std::vector<char> compressed(compressedSize);
		if (packet.getSource()->read(compressed.data(), compressedSize) != compressedSize)
		{
			ELOG("Failed to read compressed update object payload!");
			return PacketParseResult::Disconnect;
		}

		std::vector<char> decompressed(uncompressedSize);
		uLongf destLen = uncompressedSize;
		const int result = uncompress(
			reinterpret_cast<Bytef*>(decompressed.data()),
			&destLen,
			reinterpret_cast<const Bytef*>(compressed.data()),
			static_cast<uLong>(compressedSize));
		if (result != Z_OK || destLen != uncompressedSize)
		{
			ELOG("Failed to decompress update object packet (zlib error " << result << ")");
			return PacketParseResult::Disconnect;
		}

		io::MemorySource decompressedSource(decompressed.data(), decompressed.data() + decompressed.size());
		io::Reader reader(decompressedSource);
		return HandleObjectUpdate(reader);
	}

	PacketParseResult BotRealmConnector::HandleObjectUpdate(io::Reader& reader)
	{
		uint16 numObjectUpdates;
		if (!(reader >> io::read<uint16>(numObjectUpdates)))
		{
			ELOG("Failed to read UpdateObject count");
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < numObjectUpdates; ++i)
		{
			ObjectTypeId typeId;
			uint8 creation;
			if (!(reader >> io::read<uint8>(typeId) >> io::read<uint8>(creation)))
			{
				ELOG("Failed to read object update header #" << i);
				return PacketParseResult::Disconnect;
			}

			// Only process units and players - skip items, containers, world objects
			if (typeId != ObjectTypeId::Unit && typeId != ObjectTypeId::Player)
			{
				// Skip this object - we need to read the data to advance the stream
				// but we don't store non-unit types
				if (!ParseObjectUpdate(reader, creation != 0, typeId))
				{
					ELOG("Failed to skip non-unit object #" << i << " (type: " << static_cast<int>(typeId) << ")");
					return PacketParseResult::Disconnect;
				}
				continue;
			}

			if (!ParseObjectUpdate(reader, creation != 0, typeId))
			{
				ELOG("Failed to parse object update #" << i << " (type: " << static_cast<int>(typeId) << ")");
				return PacketParseResult::Disconnect;
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnDestroyObjects(game::IncomingPacket& packet)
	{
		uint16 objectCount;
		if (!(packet >> io::read<uint16>(objectCount)))
		{
			ELOG("Failed to read DestroyObjects count");
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < objectCount; ++i)
		{
			uint64 guid;
			if (!(packet >> io::read_packed_guid(guid)))
			{
				ELOG("Failed to read destroyed object GUID #" << i);
				return PacketParseResult::Disconnect;
			}

			if (const BotUnit* gone = m_objectManager.GetUnit(guid); gone && gone->IsPlayer())
			{
				DLOG("Player " << guid << " left the view of " << m_selectedCharacterGuid);
			}

			// Remove from object manager if it exists
			if (m_objectManager.RemoveUnit(guid))
			{
				UnitDespawned(guid);
			}
			else if (!m_objectManager.RemoveItem(guid))
			{
				m_objectManager.RemoveWorldObject(guid);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnNameQueryResult(game::IncomingPacket& packet)
	{
		EndRequest(bot_request::NameQuery);
		// Same layout the game client reads: packed guid, success flag, then a
		// zero-terminated name.
		uint64 guid;
		uint8 found;
		if (!(packet >> io::read_packed_guid(guid) >> io::read<uint8>(found)))
		{
			ELOG("Failed to read NameQueryResult header");
			return PacketParseResult::Disconnect;
		}

		m_pendingNameQueries.erase(guid);

		if (found == 0)
		{
			// Player not found
			return PacketParseResult::Pass;
		}

		String name;
		if (!(packet >> io::read_string(name)))
		{
			ELOG("Failed to read player name from NameQueryResult");
			return PacketParseResult::Disconnect;
		}

		m_nameCache[guid] = name;

		// Update the unit's name if we have it
		if (BotUnit* unit = m_objectManager.GetUnitMutable(guid))
		{
			unit->SetName(name);
			UnitUpdated(*unit);
		}

		NameResolved(guid, name);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnChatMessage(game::IncomingPacket& packet)
	{
		BotChatMessage chat;
		uint8 flags = 0;
		if (!(packet >> io::read_packed_guid(chat.senderGuid) >> io::read<uint8>(chat.type)
			>> io::read_limited_string<512>(chat.message) >> io::read<uint8>(flags)))
		{
			ELOG("Failed to read ChatMessage");
			return PacketParseResult::Disconnect;
		}

		// NPC speech carries the speaker's name after the flags and is never anyone talking to
		// the bot; nothing listening for chat wants it.
		if (chat.type == ChatType::UnitSay || chat.type == ChatType::UnitYell || chat.type == ChatType::UnitEmote)
		{
			return PacketParseResult::Pass;
		}

		if (chat.type == ChatType::Channel && !(packet >> io::read<uint32>(chat.channelId)))
		{
			ELOG("Failed to read channel id of ChatMessage");
			return PacketParseResult::Disconnect;
		}

		ChatMessageReceived(chat);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnChannelList(game::IncomingPacket& packet)
	{
		uint8 count = 0;
		if (!(packet >> io::read<uint8>(count)))
		{
			return PacketParseResult::Disconnect;
		}

		m_chatChannels.clear();
		for (uint8 i = 0; i < count; ++i)
		{
			uint32 id = 0;
			std::string name;
			uint32 flags = 0;
			if (!(packet >> io::read<uint32>(id) >> io::read_container<uint8>(name) >> io::read<uint32>(flags)))
			{
				return PacketParseResult::Disconnect;
			}

			m_chatChannels.emplace_back(id, std::move(name));
		}

		m_hasChatChannelList = true;
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnChannelNotify(game::IncomingPacket& packet)
	{
		uint8 notifyType = 0;
		uint32 channelId = 0;
		std::string channelName;
		if (!(packet >> io::read<uint8>(notifyType) >> io::read<uint32>(channelId) >> io::read_container<uint8>(channelName)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto existing = std::find_if(m_chatChannels.begin(), m_chatChannels.end(),
			[channelId](const auto& entry) { return entry.first == channelId; });

		if (notifyType == game::channel_notify::Joined && existing == m_chatChannels.end())
		{
			m_chatChannels.emplace_back(channelId, std::move(channelName));
		}
		else if (notifyType == game::channel_notify::Left && existing != m_chatChannels.end())
		{
			m_chatChannels.erase(existing);
		}

		return PacketParseResult::Pass;
	}

	bool BotRealmConnector::ParseObjectUpdate(io::Reader& reader, const bool creation, const ObjectTypeId typeId)
	{
		uint64 guid = 0;

		// For updates (not creation), read the GUID first
		if (!creation)
		{
			if (!(reader >> io::read_packed_guid(guid)))
			{
				ELOG("Failed to read object GUID for update");
				return false;
			}
		}

		// Read update flags
		uint32 updateFlags;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			ELOG("Failed to read update flags");
			return false;
		}

		// Read movement info if present
		MovementInfo movementInfo;
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			if (!(reader >> movementInfo))
			{
				ELOG("Failed to read movement info");
				return false;
			}
		}

		// Determine field count based on type
		size_t fieldCount = 0;
		switch (typeId)
		{
		case ObjectTypeId::Object:
			fieldCount = object_fields::WorldObjectFieldCount;
			break;
		case ObjectTypeId::Unit:
			fieldCount = object_fields::UnitFieldCount;
			break;
		case ObjectTypeId::Player:
			fieldCount = object_fields::PlayerFieldCount;
			break;
		case ObjectTypeId::Item:
			fieldCount = object_fields::ItemFieldCount;
			break;
		case ObjectTypeId::Container:
			fieldCount = object_fields::BagFieldCount;
			break;
		default:
			fieldCount = object_fields::ObjectFieldCount;
			break;
		}

		// Create a temporary field map for reading
		FieldMap<uint32> fieldMap;
		fieldMap.Initialize(fieldCount);

		// Read field data
		if (creation)
		{
			if (!(fieldMap.DeserializeComplete(reader)))
			{
				ELOG("Failed to deserialize complete field map");
				return false;
			}
		}
		else
		{
			if (!(fieldMap.DeserializeChanges(reader)))
			{
				ELOG("Failed to deserialize field map changes");
				return false;
			}
		}

		// For creation, the GUID is in the field map
		if (creation)
		{
			guid = fieldMap.GetFieldValue<uint64>(object_fields::Guid);
		}

		// Read unit speeds (only for units and players)
		std::array<float, static_cast<size_t>(MovementType::Count)> speeds = {};
		if (typeId == ObjectTypeId::Unit || typeId == ObjectTypeId::Player)
		{
			if (!(reader 
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Walk)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Run)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Backwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Swim)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::SwimBackwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Flight)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::FlightBackwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Turn)])))
			{
				ELOG("Failed to read unit speeds");
				return false;
			}
		}

		// A player's creation block ends with its appearance. Nothing here needs it, but it has
		// to be read: left in the stream, every object after the first player of a batch - the
		// group already standing in an instance a bot enters - is read from the wrong offset.
		if (typeId == ObjectTypeId::Player && creation)
		{
			AvatarConfiguration configuration;
			if (!(reader >> configuration))
			{
				ELOG("Failed to read player appearance");
				return false;
			}
		}

		// Track item and container objects so scenarios can assert on inventory contents.
		if (typeId == ObjectTypeId::Item || typeId == ObjectTypeId::Container)
		{
			// Field by field: an update carries only what changed, and zero is a real value for
			// most of these - an emptied bag slot, a broken item.
			const BotItemState* known = m_objectManager.GetItem(guid);
			BotItemState item = known ? *known : BotItemState{};
			item.guid = guid;

			const auto changed = [&fieldMap, creation](const uint32 field, const bool is64Bit = false)
			{
				const auto index = static_cast<FieldMap<uint32>::FieldIndexType>(field);
				return creation || fieldMap.IsFieldMarkedAsChanged(index)
					|| (is64Bit && fieldMap.IsFieldMarkedAsChanged(static_cast<FieldMap<uint32>::FieldIndexType>(field + 1)));
			};

			if (changed(object_fields::Entry))
			{
				item.entry = fieldMap.GetFieldValue<uint32>(object_fields::Entry);
			}
			if (changed(object_fields::StackCount))
			{
				item.stackCount = fieldMap.GetFieldValue<uint32>(object_fields::StackCount);
			}
			if (changed(object_fields::ItemOwner, true))
			{
				item.ownerGuid = fieldMap.GetFieldValue<uint64>(object_fields::ItemOwner);
			}
			if (changed(object_fields::Contained, true))
			{
				item.containerGuid = fieldMap.GetFieldValue<uint64>(object_fields::Contained);
			}
			if (changed(object_fields::Durability))
			{
				item.durability = fieldMap.GetFieldValue<uint32>(object_fields::Durability);
			}
			if (changed(object_fields::MaxDurability))
			{
				item.maxDurability = fieldMap.GetFieldValue<uint32>(object_fields::MaxDurability);
			}

			if (typeId == ObjectTypeId::Container)
			{
				constexpr uint32 MaxBagSlots = 36;
				item.slots.resize(MaxBagSlots, 0);

				if (changed(object_fields::NumSlots))
				{
					item.numSlots = std::min(fieldMap.GetFieldValue<uint32>(object_fields::NumSlots), MaxBagSlots);
				}

				for (uint32 slot = 0; slot < MaxBagSlots; ++slot)
				{
					const uint32 field = object_fields::Slot_1 + slot * 2;
					if (changed(field, true))
					{
						item.slots[slot] = fieldMap.GetFieldValue<uint64>(static_cast<FieldMap<uint32>::FieldIndexType>(field));
					}
				}
			}

			m_objectManager.SetItem(item);
			return true;
		}

		// Track world objects (chests, doors, ...) so scenarios can find and interact with them.
		if (typeId == ObjectTypeId::Object)
		{
			const BotWorldObjectState* existing = m_objectManager.GetWorldObject(guid);
			if (!creation && !existing)
			{
				// A values-update for an object we never saw created would produce a
				// phantom entry with entry id 0 — ignore it.
				return true;
			}

			BotWorldObjectState objectState;
			if (existing)
			{
				objectState = *existing;
			}
			objectState.guid = guid;

			if (creation)
			{
				objectState.entry = fieldMap.GetFieldValue<uint32>(object_fields::Entry);
				objectState.type = fieldMap.GetFieldValue<uint32>(object_fields::ObjectTypeId);
				objectState.state = fieldMap.GetFieldValue<uint32>(object_fields::State);
				objectState.lockEntry = fieldMap.GetFieldValue<uint32>(object_fields::LockEntry);
				objectState.position = movementInfo.position;
			}
			else
			{
				// Only merge fields that actually changed — State 0 (closed) is meaningful, so
				// the zero-ignore merge used for items would lose door closings.
				if (fieldMap.IsFieldMarkedAsChanged(object_fields::Entry))
				{
					objectState.entry = fieldMap.GetFieldValue<uint32>(object_fields::Entry);
				}
				if (fieldMap.IsFieldMarkedAsChanged(object_fields::State))
				{
					objectState.state = fieldMap.GetFieldValue<uint32>(object_fields::State);
				}
				if (fieldMap.IsFieldMarkedAsChanged(object_fields::LockEntry))
				{
					objectState.lockEntry = fieldMap.GetFieldValue<uint32>(object_fields::LockEntry);
				}
			}

			m_objectManager.AddOrUpdateWorldObject(objectState);
			return true;
		}

		// Only store units and players
		if (typeId != ObjectTypeId::Unit && typeId != ObjectTypeId::Player)
		{
			return true;  // Successfully parsed but not stored
		}

		// Check if unit already exists
		const bool isNewUnit = !m_objectManager.HasUnit(guid);

		// Create or get the bot unit
		BotUnit unit(guid, typeId);

		// If updating existing unit, start with existing data to preserve unchanged fields
		if (!isNewUnit)
		{
			if (const BotUnit* existingUnit = m_objectManager.GetUnit(guid))
			{
				unit = *existingUnit;  // Copy existing unit data
			}
		}

		// Extract relevant fields from field map
		// For creation, all fields are present; for updates, only changed fields are in the map
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Entry))
		{
			unit.SetEntry(fieldMap.GetFieldValue<uint32>(object_fields::Entry));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Level))
		{
			unit.SetLevel(fieldMap.GetFieldValue<uint32>(object_fields::Level));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Health))
		{
			unit.SetHealth(fieldMap.GetFieldValue<uint32>(object_fields::Health));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::MaxHealth))
		{
			unit.SetMaxHealth(fieldMap.GetFieldValue<uint32>(object_fields::MaxHealth));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::FactionTemplate))
		{
			unit.SetFactionTemplate(fieldMap.GetFieldValue<uint32>(object_fields::FactionTemplate));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::DisplayId))
		{
			unit.SetDisplayId(fieldMap.GetFieldValue<uint32>(object_fields::DisplayId));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Flags))
		{
			unit.SetUnitFlags(fieldMap.GetFieldValue<uint32>(object_fields::Flags));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::NpcFlags))
		{
			unit.SetNpcFlags(fieldMap.GetFieldValue<uint32>(object_fields::NpcFlags));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::TargetUnit))
		{
			unit.SetTargetGuid(fieldMap.GetFieldValue<uint64>(object_fields::TargetUnit));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::StandState))
		{
			unit.SetStandState(fieldMap.GetFieldValue<uint32>(object_fields::StandState));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::MoodEmote))
		{
			unit.SetMoodEmote(fieldMap.GetFieldValue<uint32>(object_fields::MoodEmote));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::IdlePoseEmote))
		{
			unit.SetIdlePoseEmote(fieldMap.GetFieldValue<uint32>(object_fields::IdlePoseEmote));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::SitPoseEmote))
		{
			unit.SetSitPoseEmote(fieldMap.GetFieldValue<uint32>(object_fields::SitPoseEmote));
		}
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::SleepPoseEmote))
		{
			unit.SetSleepPoseEmote(fieldMap.GetFieldValue<uint32>(object_fields::SleepPoseEmote));
		}

		PowerType powerType = unit.GetPowerType();
		if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::PowerType))
		{
			const uint32 rawPowerType = fieldMap.GetFieldValue<uint32>(object_fields::PowerType);
			powerType = rawPowerType < power_type::Count_ ? static_cast<PowerType>(rawPowerType) : power_type::Invalid_;
		}

		if (powerType != power_type::Invalid_)
		{
			const auto currentPowerField = static_cast<FieldMap<uint32>::FieldIndexType>(object_fields::Mana + static_cast<uint32>(powerType));
			const auto maxPowerField = static_cast<FieldMap<uint32>::FieldIndexType>(object_fields::MaxMana + static_cast<uint32>(powerType));
			uint32 power = unit.GetPower();
			uint32 maxPower = unit.GetMaxPower();
			if (creation || fieldMap.IsFieldMarkedAsChanged(currentPowerField))
			{
				power = fieldMap.GetFieldValue<uint32>(currentPowerField);
			}
			if (creation || fieldMap.IsFieldMarkedAsChanged(maxPowerField))
			{
				maxPower = fieldMap.GetFieldValue<uint32>(maxPowerField);
			}
			unit.SetPower(powerType, power, maxPower);
		}

		// The bot's own equipment, bag and backpack slots. Only its own player object carries
		// them; for everyone else the server leaves them out.
		if (typeId == ObjectTypeId::Player && guid == m_selectedCharacterGuid)
		{
			for (uint32 slot = 0; slot < player_inventory_pack_slots::End; ++slot)
			{
				const uint32 field = object_fields::InvSlotHead + slot * 2;
				const auto low = static_cast<FieldMap<uint32>::FieldIndexType>(field);
				const auto high = static_cast<FieldMap<uint32>::FieldIndexType>(field + 1);
				if (creation || fieldMap.IsFieldMarkedAsChanged(low) || fieldMap.IsFieldMarkedAsChanged(high))
				{
					m_objectManager.SetSelfInventorySlot(static_cast<uint8>(slot), fieldMap.GetFieldValue<uint64>(low));
				}
			}
		}

		// The bot's own quest log, which like the inventory slots only its own player object carries.
		if (typeId == ObjectTypeId::Player && guid == m_selectedCharacterGuid)
		{
			constexpr uint32 fieldsPerSlot = sizeof(QuestField) / sizeof(uint32);
			for (uint32 slot = 0; slot < MaxQuestLogSize; ++slot)
			{
				const uint32 first = object_fields::QuestLogSlot_1 + slot * fieldsPerSlot;
				bool slotChanged = creation;
				for (uint32 offset = 0; offset < fieldsPerSlot && !slotChanged; ++offset)
				{
					slotChanged = fieldMap.IsFieldMarkedAsChanged(static_cast<FieldMap<uint32>::FieldIndexType>(first + offset));
				}

				if (slotChanged)
				{
					QuestField field;
					field.questId = fieldMap.GetFieldValue<uint32>(static_cast<FieldMap<uint32>::FieldIndexType>(first));
					field.status = fieldMap.GetFieldValue<uint32>(static_cast<FieldMap<uint32>::FieldIndexType>(first + 1));
					field.counterField = fieldMap.GetFieldValue<uint32>(static_cast<FieldMap<uint32>::FieldIndexType>(first + 2));
					field.questTimer = fieldMap.GetFieldValue<uint32>(static_cast<FieldMap<uint32>::FieldIndexType>(first + 3));
					m_objectManager.SetSelfQuestLogSlot(static_cast<uint8>(slot), field);
				}
			}
		}

		// Money is a player-only field used by scenario assertions.
		if (typeId == ObjectTypeId::Player &&
			(creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Money)))
		{
			unit.SetMoney(fieldMap.GetFieldValue<uint32>(object_fields::Money));
		}

		// Experience fields are player-only and used by scenario assertions.
		if (typeId == ObjectTypeId::Player)
		{
			if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::TalentPoints))
			{
				unit.SetTalentPoints(fieldMap.GetFieldValue<uint32>(object_fields::TalentPoints));
			}
			if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Class))
			{
				unit.SetClassId(fieldMap.GetFieldValue<uint32>(object_fields::Class));
			}
			if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::Xp))
			{
				unit.SetXp(fieldMap.GetFieldValue<uint32>(object_fields::Xp));
			}
			if (creation || fieldMap.IsFieldMarkedAsChanged(object_fields::NextLevelXp))
			{
				unit.SetNextLevelXp(fieldMap.GetFieldValue<uint32>(object_fields::NextLevelXp));
			}
		}

		// Set position/movement - this should always be present for creation
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			unit.SetMovementInfo(movementInfo);
		}

		// Set speeds (always present for units in the packet)
		unit.SetSpeeds(speeds);

		// Add or update in object manager
		m_objectManager.AddOrUpdateUnit(unit);

		// Set self guid if this is our character
		if (guid == m_selectedCharacterGuid)
		{
			m_objectManager.SetSelfGuid(guid);

			// The new world node has spawned the bot after a transfer: take its movement state,
			// falling as every spawn is.
			if (m_awaitingWorldSpawn && (updateFlags & object_update_flags::HasMovementInfo))
			{
				m_movementInfo = movementInfo;
			}
		}

		// Emit signals
		if (isNewUnit)
		{
			if (typeId == ObjectTypeId::Player && guid != m_selectedCharacterGuid)
			{
				DLOG("Player " << guid << " came into the view of " << m_selectedCharacterGuid);
			}
			UnitSpawned(unit);
		}
		else
		{
			UnitUpdated(unit);
		}

		return true;
	}

	PacketParseResult BotRealmConnector::OnMovementPacket(game::IncomingPacket& packet)
	{
		uint64 guid;
		MovementInfo movementInfo;

		if (!(packet >> io::read<uint64>(guid) >> movementInfo))
		{
			ELOG("Failed to parse movement packet");
			return PacketParseResult::Disconnect;
		}

		// Get the unit from object manager
		BotUnit* unit = m_objectManager.GetUnitMutable(guid);
		if (!unit)
		{
			// Unit not yet known - this can happen if movement arrives before spawn
			return PacketParseResult::Pass;
		}

		// Update the unit's movement info (which includes position)
		unit->SetMovementInfo(movementInfo);

		// Emit update signal
		UnitUpdated(*unit);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnCreatureMove(game::IncomingPacket& packet)
	{
		// Wire format written by unit_mover.cpp's WriteCreatureMove: guid, old position (unused
		// here), start/end time (unused - no interpolation), a point count, the destination,
		// an optional facing, the movement mode, then any intermediate waypoints (skipped - we
		// only care about where the unit ends up).
		//
		// The destination is written unconditionally, and pointCount is path.size() - 1, so a
		// single-point path arrives as pointCount == 0 *with* a destination. That is exactly
		// what UnitMover::StopMovement sends to announce where a creature actually stopped
		// after being interrupted mid-path, so returning early on pointCount == 0 would skip
		// the one update that matters most and leave the cached position at a destination the
		// creature never reached.
		uint64 guid = 0;
		float oldX = 0.0f, oldY = 0.0f, oldZ = 0.0f;
		GameTime startTime = 0, endTime = 0;
		uint32 pointCount = 0;

		if (!(packet
			>> io::read_packed_guid(guid)
			>> io::read<float>(oldX) >> io::read<float>(oldY) >> io::read<float>(oldZ)
			>> io::read<GameTime>(startTime) >> io::read<GameTime>(endTime)
			>> io::read<uint32>(pointCount)))
		{
			ELOG("Failed to parse CreatureMove header");
			return PacketParseResult::Disconnect;
		}

		float destX = 0.0f, destY = 0.0f, destZ = 0.0f;
		if (!(packet >> io::read<float>(destX) >> io::read<float>(destY) >> io::read<float>(destZ)))
		{
			ELOG("Failed to parse CreatureMove destination");
			return PacketParseResult::Disconnect;
		}

		uint8 hasFacing = 0;
		if (!(packet >> io::read<uint8>(hasFacing)))
		{
			ELOG("Failed to parse CreatureMove facing flag");
			return PacketParseResult::Disconnect;
		}

		float facing = 0.0f;
		if (hasFacing != 0 && !(packet >> io::read<float>(facing)))
		{
			ELOG("Failed to parse CreatureMove facing value");
			return PacketParseResult::Disconnect;
		}

		uint8 movementMode = 0;
		if (!(packet >> io::read<uint8>(movementMode)))
		{
			ELOG("Failed to parse CreatureMove movement mode");
			return PacketParseResult::Disconnect;
		}

		// The intermediate waypoints (pointCount - 1 of them, 3 floats each), between the start
		// and the destination. With them and the move's duration the bot knows where the unit is
		// on its way, not only where it ends up - which is what a cast at it has to face.
		std::vector<Vector3> path { Vector3(oldX, oldY, oldZ) };
		for (uint32 i = 0; i + 1 < pointCount; ++i)
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
			if (!(packet >> io::read<float>(x) >> io::read<float>(y) >> io::read<float>(z)))
			{
				ELOG("Failed to parse CreatureMove intermediate waypoint");
				return PacketParseResult::Disconnect;
			}
			path.emplace_back(x, y, z);
		}
		path.emplace_back(destX, destY, destZ);

		BotUnit* unit = m_objectManager.GetUnitMutable(guid);
		if (!unit)
		{
			// Unit not yet known - this can happen if movement arrives before spawn.
			return PacketParseResult::Pass;
		}

		MovementInfo info = unit->GetMovementInfo();
		info.position = Vector3(destX, destY, destZ);
		if (hasFacing != 0)
		{
			info.facing = Radian(facing);
		}
		unit->SetMovementInfo(info);

		// The server's start and end are on its clock; the move starts about now on the bot's.
		unit->SetMovePath(std::move(path), GetAsyncTimeMs(), endTime > startTime ? endTime - startTime : 0);

		UnitUpdated(*unit);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnInitialSpells(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			UpdateSpellStateIssue("initial_spells_self_unavailable");
			return PacketParseResult::Pass;
		}

		uint16 spellCount = 0;
		if (!(packet >> io::read<uint16>(spellCount)))
		{
			UpdateSpellStateIssue("initial_spells_parse_failed");
			return PacketParseResult::Pass;
		}

		std::vector<uint32> spellIds;
		spellIds.reserve(spellCount);
		for (uint16 i = 0; i < spellCount; ++i)
		{
			uint32 spellId = 0;
			if (!(packet >> io::read<uint32>(spellId)))
			{
				UpdateSpellStateIssue("initial_spells_parse_failed");
				return PacketParseResult::Pass;
			}

			spellIds.push_back(spellId);
		}

		self->SetKnownSpells(spellIds);
		ClearSpellStateIssue();
		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnLearnedSpell(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			UpdateSpellStateIssue("learned_spell_self_unavailable");
			return PacketParseResult::Pass;
		}

		uint32 spellId = 0;
		if (!(packet >> io::read<uint32>(spellId)) || spellId == 0)
		{
			UpdateSpellStateIssue("learned_spell_parse_failed");
			return PacketParseResult::Pass;
		}

		self->LearnSpell(spellId);
		ClearSpellStateIssue();
		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeSkills(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			return PacketParseResult::Pass;
		}

		uint8 count = 0;
		if (!(packet >> io::read<uint8>(count)))
		{
			return PacketParseResult::Pass;
		}

		std::vector<TradeSkillData> skills;
		skills.reserve(count);
		for (uint8 i = 0; i < count; ++i)
		{
			TradeSkillData data;
			if (!(packet >> data))
			{
				return PacketParseResult::Pass;
			}

			skills.push_back(data);
		}

		self->ClearTradeSkills();
		for (const TradeSkillData& data : skills)
		{
			self->SetTradeSkill(data.skillId, data.value, data.max);
		}

		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeSkillUpdate(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			return PacketParseResult::Pass;
		}

		TradeSkillData data;
		if (!(packet >> data))
		{
			return PacketParseResult::Pass;
		}

		self->SetTradeSkill(data.skillId, data.value, data.max);
		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnUnlearnedSpell(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			UpdateSpellStateIssue("unlearned_spell_self_unavailable");
			return PacketParseResult::Pass;
		}

		uint32 spellId = 0;
		if (!(packet >> io::read<uint32>(spellId)) || spellId == 0)
		{
			UpdateSpellStateIssue("unlearned_spell_parse_failed");
			return PacketParseResult::Pass;
		}

		self->UnlearnSpell(spellId);
		ClearSpellStateIssue();
		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSpellStart(game::IncomingPacket& packet)
	{
		uint64 casterGuid = 0;
		uint32 spellId = 0;
		GameTime castTime = 0;
		SpellTargetMap targetMap;
		uint32 cooldownMs = 0;
		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<GameTime>(castTime) >> targetMap >> io::read<uint32>(cooldownMs)))
		{
			UpdateSpellStateIssue("spell_start_parse_failed");
			return PacketParseResult::Pass;
		}

		if (casterGuid == m_selectedCharacterGuid)
		{
			EndCastRequest(spellId);
		}

		if (casterGuid != m_selectedCharacterGuid)
		{
			// Someone else's cast: remembered so that a bot can see a boss start a spell worth
			// interrupting, and how long it has left to do it.
			if (BotUnit* caster = m_objectManager.GetUnitMutable(casterGuid))
			{
				caster->SetCurrentCast(spellId, GetAsyncTimeMs() + castTime);
			}
			UnitCastStarted(casterGuid, spellId, castTime);

			return PacketParseResult::Pass;
		}

		if (BotUnit* self = GetSelfMutable())
		{
			const GameTime nowMs = GetAsyncTimeMs();
			self->SetLastCastStarted(spellId, targetMap.GetTargetMap(), targetMap.GetUnitTarget(), nowMs);
			if (cooldownMs > 0)
			{
				self->SetSpellCooldown(spellId, nowMs, cooldownMs);
			}
			ClearSpellStateIssue();
			UnitUpdated(*self);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSpellGo(game::IncomingPacket& packet)
	{
		uint64 casterGuid = 0;
		uint32 spellId = 0;
		GameTime gameTime = 0;
		SpellTargetMap targetMap;
		uint32 cooldownMs = 0;
		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<GameTime>(gameTime) >> targetMap >> io::read<uint32>(cooldownMs)))
		{
			UpdateSpellStateIssue("spell_go_parse_failed");
			return PacketParseResult::Pass;
		}

		SpellWentOff(casterGuid, spellId);

		if (casterGuid != m_selectedCharacterGuid)
		{
			return PacketParseResult::Pass;
		}

		EndCastRequest(spellId);

		if (BotUnit* self = GetSelfMutable())
		{
			const GameTime nowMs = GetAsyncTimeMs();
			self->SetLastCastSucceeded(spellId, targetMap.GetTargetMap(), targetMap.GetUnitTarget(), nowMs);
			if (cooldownMs > 0)
			{
				self->SetSpellCooldown(spellId, nowMs, cooldownMs);
			}
			ClearSpellStateIssue();
			UnitUpdated(*self);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSpellFailure(game::IncomingPacket& packet)
	{
		uint64 casterGuid = 0;
		uint32 spellId = 0;
		GameTime gameTime = 0;
		SpellCastResult result = spell_cast_result::CastOkay;
		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<GameTime>(gameTime) >> io::read<uint8>(result)))
		{
			UpdateSpellStateIssue("spell_failure_parse_failed");
			return PacketParseResult::Pass;
		}

		if (casterGuid == m_selectedCharacterGuid)
		{
			EndCastRequest(spellId);
		}

		if (casterGuid != m_selectedCharacterGuid)
		{
			// Interrupted or failed: nothing left to interrupt.
			if (BotUnit* caster = m_objectManager.GetUnitMutable(casterGuid))
			{
				if (caster->GetCurrentCast(GetAsyncTimeMs()) == spellId)
				{
					caster->SetCurrentCast(0, 0);
				}
			}
			UnitCastFailed(casterGuid, spellId, static_cast<uint8>(result));
			return PacketParseResult::Pass;
		}

		if (BotUnit* self = GetSelfMutable())
		{
			// A cast that fails validation gets two failures: the specific reason (not in front, say)
			// from the cast state, then a generic FailedError from the cast request handler. Keep the
			// specific one, or every validation failure reads as "failed:5".
			const BotUnit::CastState& last = self->GetLastCastState();
			const bool genericAfterSpecific = result == spell_cast_result::FailedError
				&& last.status == BotUnit::CastState::Status::Failed
				&& last.spellId == spellId
				&& last.failureReason != spell_cast_result::FailedError;
			if (!genericAfterSpecific)
			{
				self->SetLastCastFailed(spellId, result, GetAsyncTimeMs());
			}
			ClearSpellStateIssue();
			UnitUpdated(*self);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSpellCooldown(game::IncomingPacket& packet)
	{
		BotUnit* self = GetSelfMutable();
		if (!self)
		{
			UpdateSpellStateIssue("spell_cooldown_self_unavailable");
			return PacketParseResult::Pass;
		}

		// uint16 count, then (uint32 spellId, uint32 remainingMs) per entry. A zero remaining time
		// clears the cooldown; spell 0 with zero clears all of them (GM cooldown reset).
		uint16 count = 0;
		if (!(packet >> io::read<uint16>(count)))
		{
			UpdateSpellStateIssue("spell_cooldown_parse_failed");
			return PacketParseResult::Pass;
		}

		const GameTime nowMs = GetAsyncTimeMs();
		for (uint16 i = 0; i < count; ++i)
		{
			uint32 spellId = 0;
			uint32 cooldownMs = 0;
			if (!(packet >> io::read<uint32>(spellId) >> io::read<uint32>(cooldownMs)))
			{
				UpdateSpellStateIssue("spell_cooldown_parse_failed");
				return PacketParseResult::Pass;
			}

			// Spell 0 with no time left: every cooldown was reset (GM command).
			if (spellId == 0)
			{
				if (cooldownMs == 0)
				{
					self->ClearAllSpellCooldowns();
				}
				continue;
			}

			if (!self->KnowsSpell(spellId))
			{
				UpdateSpellStateIssue("spell_cooldown_unknown_spell");
				continue;
			}

			self->SetSpellCooldown(spellId, nowMs, cooldownMs);
		}

		ClearSpellStateIssue();
		UnitUpdated(*self);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnAuraUpdate(game::IncomingPacket& packet)
	{
		uint64 unitGuid = 0;
		if (!(packet >> io::read_packed_guid(unitGuid)))
		{
			UpdateSpellStateIssue("aura_update_parse_failed");
			return PacketParseResult::Pass;
		}

		BotUnit* unit = m_objectManager.GetUnitMutable(unitGuid);
		if (!unit)
		{
			if (unitGuid == m_selectedCharacterGuid)
			{
				UpdateSpellStateIssue("aura_update_self_unavailable");
			}
			else
			{
				WLOG("Unable to find unit " << unitGuid << " for aura update!");
			}
			return PacketParseResult::Pass;
		}

		uint32 visibleAuraCount = 0;
		if (!(packet >> io::read<uint32>(visibleAuraCount)))
		{
			UpdateSpellStateIssue("aura_update_parse_failed");
			return PacketParseResult::Pass;
		}

		std::vector<BotUnit::AuraState> auras;
		auras.reserve(visibleAuraCount);
		for (uint32 i = 0; i < visibleAuraCount; ++i)
		{
			BotUnit::AuraState aura;
			uint8 auraTypeCount = 0;
			if (!(packet >> io::read<uint32>(aura.spellId)
				>> io::read<uint32>(aura.remainingMs)
				>> io::read_packed_guid(aura.casterGuid)
				>> io::read<uint8>(auraTypeCount)))
			{
				UpdateSpellStateIssue("aura_update_parse_failed");
				return PacketParseResult::Pass;
			}

			if (aura.spellId == 0)
			{
				UpdateSpellStateIssue("aura_update_invalid_spell_id");
				return PacketParseResult::Pass;
			}

			aura.basePoints.resize(auraTypeCount);
			if (!(packet >> io::read_range(aura.basePoints)))
			{
				UpdateSpellStateIssue("aura_update_parse_failed");
				return PacketParseResult::Pass;
			}

			if (!(packet >> io::read<uint8>(aura.stackCount)))
			{
				UpdateSpellStateIssue("aura_update_parse_failed");
				return PacketParseResult::Pass;
			}

			auras.push_back(std::move(aura));
		}

		unit->SetVisibleAuras(std::move(auras));
		if (unitGuid == m_selectedCharacterGuid)
		{
			ClearSpellStateIssue();
		}
		UnitUpdated(*unit);
		return PacketParseResult::Pass;
	}

	// ============================================================
	// Combat Methods
	// ============================================================

	void BotRealmConnector::SendAttackStart(uint64 targetGuid)
	{
		const GameTime timestamp = GetAsyncTimeMs();

		sendSinglePacket([targetGuid, timestamp](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::AttackSwing);
				packet << io::write_packed_guid(targetGuid) << io::write<GameTime>(timestamp);
				packet.Finish();
			});

		m_autoAttackTargetGuid = targetGuid;
		// Note: m_isAutoAttacking will be set to true when we receive AttackStart from server
	}

	void BotRealmConnector::SendAttackStop()
	{
		const GameTime timestamp = GetAsyncTimeMs();

		sendSinglePacket([timestamp](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::AttackStop);
				packet << io::write<GameTime>(timestamp);
				packet.Finish();
			});

		// Note: m_isAutoAttacking will be set to false when we receive AttackStop from server
	}

	PacketParseResult BotRealmConnector::OnReviveOffer(game::IncomingPacket& packet)
	{
		uint64 casterGuid = 0;
		uint32 spellId = 0;
		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId)))
		{
			ELOG("Failed to read ReviveRequest packet!");
			return PacketParseResult::Disconnect;
		}

		ReviveOffered(casterGuid);
		return PacketParseResult::Pass;
	}

	void BotRealmConnector::SendReviveResponse(const bool accept)
	{
		sendSinglePacket([accept](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ReviveResponse);
				packet << io::write<uint8>(accept ? 1 : 0);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendReviveRequest()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ReviveRequest);
				packet.Finish();
			});
	}

	// ============================================================
	// Combat Packet Handlers
	// ============================================================

	PacketParseResult BotRealmConnector::OnAttackStart(game::IncomingPacket& packet)
	{
		uint64 attackerGuid, victimGuid;
		GameTime attackTime;

		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read_packed_guid(victimGuid) >> io::read<GameTime>(attackTime)))
		{
			return PacketParseResult::Disconnect;
		}

		// Check if this is our attack starting
		if (attackerGuid == m_selectedCharacterGuid)
		{
			m_isAutoAttacking = true;
			m_autoAttackTargetGuid = victimGuid;
		}

		AttackStarted(attackerGuid, victimGuid);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnAttackStop(game::IncomingPacket& packet)
	{
		uint64 attackerGuid;
		GameTime attackTime;

		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read<GameTime>(attackTime)))
		{
			return PacketParseResult::Disconnect;
		}

		// Check if this is our attack stopping
		if (attackerGuid == m_selectedCharacterGuid)
		{
			m_isAutoAttacking = false;
			m_autoAttackTargetGuid = 0;
		}

		AttackStopped(attackerGuid);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnChannelStart(game::IncomingPacket& packet)
	{
		uint64 casterGuid;
		uint32 spellId;
		int32 durationMs;

		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<int32>(durationMs)))
		{
			return PacketParseResult::Disconnect;
		}

		// Someone else's channel counts as a cast to interrupt: Cantor Veyr's Dirge of the Grave is
		// channelled, and only a spell start ever set a unit's current cast.
		if (casterGuid != m_selectedCharacterGuid)
		{
			if (BotUnit* caster = m_objectManager.GetUnitMutable(casterGuid))
			{
				caster->SetCurrentCast(spellId, GetAsyncTimeMs() + static_cast<GameTime>(std::max(durationMs, 0)));
			}
		}

		ChannelStarted(casterGuid, spellId, durationMs);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnChannelUpdate(game::IncomingPacket& packet)
	{
		uint64 casterGuid;
		GameTime remainingMs;

		if (!(packet >> io::read_packed_guid(casterGuid) >> io::read<GameTime>(remainingMs)))
		{
			return PacketParseResult::Disconnect;
		}

		if (casterGuid != m_selectedCharacterGuid && remainingMs == 0)
		{
			if (BotUnit* caster = m_objectManager.GetUnitMutable(casterGuid))
			{
				caster->SetCurrentCast(0, 0);
			}
		}

		ChannelUpdated(casterGuid, remainingMs);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnAttackSwingError(game::IncomingPacket& packet)
	{
		AttackSwingEvent error;

		if (!(packet >> io::read<uint32>(error)))
		{
			return PacketParseResult::Disconnect;
		}

		// Success is the server saying the previous error no longer applies, not another error.
		if (error == attack_swing_event::Success)
		{
			AttackSwingRecovered();
		}
		else
		{
			AttackSwingError(error);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnAttackerStateUpdate(game::IncomingPacket& packet)
	{
		uint64 attackerGuid, victimGuid;
		uint32 hitInfo, victimState, totalDamage, school, absorbedDamage, resistedDamage, blockedDamage;

		if (!(packet >> io::read_packed_guid(attackerGuid) 
			>> io::read_packed_guid(victimGuid) 
			>> io::read<uint32>(hitInfo) 
			>> io::read<uint32>(victimState) 
			>> io::read<uint32>(totalDamage) 
			>> io::read<uint32>(school) 
			>> io::read<uint32>(absorbedDamage) 
			>> io::read<uint32>(resistedDamage) 
			>> io::read<uint32>(blockedDamage)))
		{
			return PacketParseResult::Disconnect;
		}

		AttackHit(attackerGuid, victimGuid, totalDamage, hitInfo, victimState);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnNonSpellDamageLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid;
		uint32 amount;
		uint8 flags;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(amount) >> io::read<uint8>(flags)))
		{
			return PacketParseResult::Disconnect;
		}

		DamageReceived(targetGuid, amount, flags);

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::SetSelection(const uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SetSelection);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatCreateMonster(const uint32 entry)
	{
		sendSinglePacket([entry](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatCreateMonster);
			packet << io::write<uint32>(entry);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatDestroyMonster(const uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatDestroyMonster);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatCreateObject(const uint32 entry, const uint32 state)
	{
		sendSinglePacket([entry, state](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatCreateObject);
			packet << io::write<uint32>(entry) << io::write<uint32>(state);
			packet.Finish();
			});
	}

	void BotRealmConnector::CancelCast()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CancelCast);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatCheckLineOfSight(const uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatCheckLineOfSight);
			packet << io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnDebugLineOfSightResult(game::IncomingPacket& packet)
	{
		uint8 hasLos = 0;
		if (!(packet >> io::read<uint8>(hasLos)))
		{
			ELOG("Failed to read DebugLineOfSightResult packet");
			return PacketParseResult::Disconnect;
		}

		// The tested ray and its hit point, for diagnostics.
		Vector3 from, to, hit;
		if (!(packet >> io::read<float>(from.x) >> io::read<float>(from.y) >> io::read<float>(from.z)
			>> io::read<float>(to.x) >> io::read<float>(to.y) >> io::read<float>(to.z)
			>> io::read<float>(hit.x) >> io::read<float>(hit.y) >> io::read<float>(hit.z)))
		{
			from = to = hit = Vector3::Zero;
		}

		m_lastLosFrom = from;
		m_lastLosTo = to;
		m_lastLosHit = hit;
		m_lastLosResult = (hasLos != 0);
		++m_losResultCounter;

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::CheatSetTimeOfDay(const GameTime timeOfDay, const uint32 transitionMs)
	{
		sendSinglePacket([timeOfDay, transitionMs](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSetTimeOfDay);
			packet
				<< io::write<uint8>(0)
				<< io::write<uint32>(static_cast<uint32>(timeOfDay))
				<< io::write<uint32>(transitionMs);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatResetTimeOfDay(const uint32 transitionMs)
	{
		sendSinglePacket([transitionMs](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSetTimeOfDay);
			packet
				<< io::write<uint8>(1)
				<< io::write<uint32>(0)
				<< io::write<uint32>(transitionMs);
			packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnGameTimeInfo(game::IncomingPacket& packet)
	{
		uint64 gameTime = 0;
		float timeSpeed = 0.0f;
		uint32 transitionMs = 0;
		if (!(packet >> io::read<uint64>(gameTime) >> io::read<float>(timeSpeed) >> io::read<uint32>(transitionMs)))
		{
			ELOG("Failed to read GameTimeInfo packet");
			return PacketParseResult::Disconnect;
		}

		m_lastGameTime = gameTime;
		m_lastGameTimeTransitionMs = transitionMs;
		++m_gameTimeInfoCounter;

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::GmShutdown(const uint8 action, const uint32 delaySeconds)
	{
		sendSinglePacket([action, delaySeconds](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GmShutdown);
			packet << io::write<uint8>(action) << io::write<uint32>(delaySeconds);
			packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnShutdownCountdown(game::IncomingPacket& packet)
	{
		uint32 seconds = 0;
		if (!(packet >> io::read<uint32>(seconds)))
		{
			ELOG("Failed to read ShutdownCountdown packet");
			return PacketParseResult::Disconnect;
		}

		m_lastShutdownCountdown = seconds;
		++m_shutdownCountdownCounter;
		return PacketParseResult::Pass;
	}

	void BotRealmConnector::CheatLearnSpell(const uint32 spellId)
	{
		sendSinglePacket([spellId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLearnSpell);
			packet << io::write<uint32>(spellId);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatLearnEmote(const uint32 emoteId)
	{
		sendSinglePacket([emoteId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLearnEmote);
			packet << io::write<uint32>(emoteId);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendEmote(const uint32 emoteId, const uint64 targetGuid)
	{
		sendSinglePacket([emoteId, targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::Emote);
			packet << io::write<uint32>(emoteId) << io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendCyclePose()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CyclePose);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatLevelUp(const uint8 levels)
	{
		sendSinglePacket([levels](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLevelUp);
			packet << io::write<uint8>(levels);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatClassLevelUp(const uint8 levels)
	{
		sendSinglePacket([levels](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatClassLevelUp);
			packet << io::write<uint8>(levels);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatGiveMoney(const uint32 amount)
	{
		sendSinglePacket([amount](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatGiveMoney);
			packet << io::write<uint32>(amount);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatAddItem(const uint32 itemId, const uint8 count)
	{
		sendSinglePacket([itemId, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatAddItem);
			packet << io::write<uint32>(itemId) << io::write<uint8>(count);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatSummon(const std::string& playerName)
	{
		sendSinglePacket([&playerName](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSummon);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatWorldPort(const uint32 mapId, const Vector3& position, const float facing)
	{
		sendSinglePacket([mapId, &position, facing](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatWorldPort);
			packet << io::write<uint32>(mapId)
				<< io::write<float>(position.x)
				<< io::write<float>(position.y)
				<< io::write<float>(position.z)
				<< io::write<float>(facing);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatSpeed(const float speed)
	{
		sendSinglePacket([speed](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSpeed);
			packet << io::write<float>(speed);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatKill()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatKill);
			packet.Finish();
			});
	}

	void BotRealmConnector::Loot(const uint64 lootObjectGuid)
	{
		BeginRequest(bot_request::Loot);
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::Loot);
			packet << io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::LootRelease(const uint64 lootObjectGuid)
	{
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootRelease);
			packet << io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::AutoStoreLootItem(const uint8 lootSlot)
	{
		sendSinglePacket([lootSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoStoreLootItem);
			packet << io::write<uint8>(lootSlot);
			packet.Finish();
			});
	}

	void BotRealmConnector::LootMoney()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootMoney);
			packet.Finish();
			});
	}

	void BotRealmConnector::SellItem(const uint64 vendorGuid, const uint64 itemGuid)
	{
		sendSinglePacket([vendorGuid, itemGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SellItem);
			packet << io::write<uint64>(vendorGuid) << io::write<uint64>(itemGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::BuyItem(const uint64 vendorGuid, const uint32 itemId, const uint8 count)
	{
		sendSinglePacket([vendorGuid, itemId, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::BuyItem);
			packet << io::write<uint64>(vendorGuid) << io::write<uint32>(itemId) << io::write<uint8>(count);
			packet.Finish();
			});
	}

	void BotRealmConnector::TrainerBuySpell(const uint64 trainerGuid, const uint32 spellId)
	{
		sendSinglePacket([trainerGuid, spellId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TrainerBuySpell);
			packet << io::write<uint64>(trainerGuid) << io::write<uint32>(spellId);
			packet.Finish();
			});
	}

	void BotRealmConnector::LearnTalent(const uint32 talentId, const uint8 rank)
	{
		sendSinglePacket([talentId, rank](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LearnTalent);
			packet << io::write<uint32>(talentId) << io::write<uint8>(rank);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendAreaTriggerTriggered(const uint32 triggerId)
	{
		sendSinglePacket([triggerId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AreaTriggerTriggered);
			packet << io::write<uint32>(triggerId);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendLootRoll(const uint64 lootGuid, const uint8 slot, const uint8 vote)
	{
		sendSinglePacket([lootGuid, slot, vote](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootRoll);
			packet << io::write<uint64>(lootGuid) << io::write<uint8>(slot) << io::write<uint8>(vote);
			packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnSpellZoneStart(game::IncomingPacket& packet)
	{
		BotSpellZone zone;
		uint32 remainingMs = 0;
		if (!(packet >> io::read<uint32>(zone.zoneId) >> io::read_packed_guid(zone.casterGuid) >> io::read<uint32>(zone.spellId)
			>> io::read<float>(zone.position.x) >> io::read<float>(zone.position.y) >> io::read<float>(zone.position.z)
			>> io::read<float>(zone.radius) >> io::read<uint32>(remainingMs)))
		{
			return PacketParseResult::Disconnect;
		}

		zone.endMs = GetAsyncTimeMs() + remainingMs;
		m_spellZones[zone.zoneId] = zone;
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnSpellZoneEnd(game::IncomingPacket& packet)
	{
		uint32 zoneId = 0;
		uint8 expired = 0;
		if (!(packet >> io::read<uint32>(zoneId) >> io::read<uint8>(expired)))
		{
			return PacketParseResult::Disconnect;
		}

		m_spellZones.erase(zoneId);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMovementControl(game::IncomingPacket& packet)
	{
		uint32 ackId = 0;
		uint8 applied = 0;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read movement control packet " << packet.GetId());
			return PacketParseResult::Disconnect;
		}

		uint16 ackOpCode = game::client_realm_packet::MoveRootAck;
		switch (packet.GetId())
		{
		case game::realm_client_packet::MoveStun: ackOpCode = game::client_realm_packet::MoveStunAck; break;
		case game::realm_client_packet::MoveSleep: ackOpCode = game::client_realm_packet::MoveSleepAck; break;
		case game::realm_client_packet::MoveFear: ackOpCode = game::client_realm_packet::MoveFearAck; break;
		case game::realm_client_packet::MoveDisorient: ackOpCode = game::client_realm_packet::MoveDisorientAck; break;
		default: break;
		}

		MovementControlChanged(ackOpCode, ackId, applied != 0);
		return PacketParseResult::Pass;
	}

	void BotRealmConnector::SendMovementControlAck(const uint16 ackOpCode, const uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackOpCode, ackId, &movementInfo](game::OutgoingPacket& packet)
			{
				packet.Start(ackOpCode);
				packet << io::write<uint32>(ackId) << movementInfo;
				packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnSetProficiency(game::IncomingPacket& packet)
	{
		uint32 proficiencyId = 0;
		uint8 added = 0;
		if (!(packet >> io::read<uint32>(proficiencyId) >> io::read<uint8>(added)))
		{
			return PacketParseResult::Disconnect;
		}

		if (added != 0)
		{
			m_proficiencies.insert(proficiencyId);
		}
		else
		{
			m_proficiencies.erase(proficiencyId);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnStartLootRoll(game::IncomingPacket& packet)
	{
		BotLootRoll roll;
		uint32 rollTimeMs = 0;
		if (!(packet >> io::read<uint64>(roll.lootGuid) >> io::read<uint8>(roll.slot) >> io::read<uint32>(roll.itemId) >> io::read<uint32>(rollTimeMs)))
		{
			return PacketParseResult::Disconnect;
		}

		roll.endMs = GetAsyncTimeMs() + rollTimeMs;
		LootRollStarted(roll);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnLootResponse(game::IncomingPacket& packet)
	{
		EndRequest(bot_request::Loot);
		BotLootWindow window;
		uint8 lootType = 0;
		if (!(packet >> io::read<uint64>(window.lootGuid) >> io::read<uint8>(lootType)))
		{
			return PacketParseResult::Disconnect;
		}

		if (lootType == loot_type::None)
		{
			uint8 error = 0;
			if (!(packet >> io::read<uint8>(error)))
			{
				return PacketParseResult::Disconnect;
			}

			LootFailed(window.lootGuid, error);
			return PacketParseResult::Pass;
		}

		uint8 itemCount = 0;
		if (!(packet >> io::read<uint32>(window.money) >> io::read<uint8>(itemCount)))
		{
			return PacketParseResult::Disconnect;
		}

		window.items.resize(itemCount);
		for (BotLootItem& item : window.items)
		{
			if (!(packet >> io::read<uint8>(item.slot) >> io::read<uint32>(item.itemId) >> io::read<uint32>(item.count)
				>> io::skip<uint32>() >> io::skip<uint32>() >> io::skip<uint32>() >> io::read<uint8>(item.slotType)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		LootOpened(window);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestGiverStatus(game::IncomingPacket& packet)
	{
		EndRequest(bot_request::QuestStatus);
		uint64 questGiverGuid = 0;
		uint8 status = 0;
		if (!(packet >> io::read<uint64>(questGiverGuid) >> io::read<uint8>(status)))
		{
			return PacketParseResult::Disconnect;
		}

		QuestGiverStatusReceived(questGiverGuid, status);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestGiverQuestList(game::IncomingPacket& packet)
	{
		EndRequest(bot_request::QuestHello);
		uint64 questGiverGuid = 0;
		std::string greeting;
		uint8 questCount = 0;
		if (!(packet >> io::read<uint64>(questGiverGuid) >> io::read_container<uint16>(greeting, 512) >> io::read<uint8>(questCount)))
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<BotQuestMenuEntry> entries(questCount);
		for (BotQuestMenuEntry& entry : entries)
		{
			if (!(packet >> io::read<uint32>(entry.questId) >> io::read<uint32>(entry.menuIcon)
				>> io::read<int32>(entry.level) >> io::read_container<uint8>(entry.title)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		QuestListReceived(questGiverGuid, entries);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestGiverQuestDetails(game::IncomingPacket& packet)
	{
		// Only who offers what; the texts and rewards that follow are for a player to read.
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		if (!(packet >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId)))
		{
			return PacketParseResult::Disconnect;
		}

		QuestDetailsReceived(questGiverGuid, questId);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestGiverOfferReward(game::IncomingPacket& packet)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		std::string title;
		std::string text;
		uint32 choiceCount = 0;
		if (!(packet >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId)
			>> io::read_container<uint8>(title) >> io::read_container<uint16>(text, 512)
			>> io::read<uint32>(choiceCount)))
		{
			return PacketParseResult::Disconnect;
		}

		// The choices are the quest data's, a handful at most; anything larger is a misread.
		if (choiceCount > 16)
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<uint32> choiceItems(choiceCount);
		for (uint32& itemId : choiceItems)
		{
			if (!(packet >> io::read<uint32>(itemId) >> io::skip<uint32>() >> io::skip<uint32>()))
			{
				return PacketParseResult::Disconnect;
			}
		}

		QuestRewardOffered(questGiverGuid, questId, choiceItems);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestGiverQuestComplete(game::IncomingPacket& packet)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		uint32 xp = 0;
		uint32 money = 0;
		if (!(packet >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId) >> io::read<uint32>(xp) >> io::read<uint32>(money)))
		{
			return PacketParseResult::Disconnect;
		}

		QuestRewarded(questGiverGuid, questId, xp, money);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestAccepted(game::IncomingPacket& packet)
	{
		std::string name;
		uint32 questId = 0;
		if (!(packet >> io::read_container<uint8>(name) >> io::read<uint32>(questId)))
		{
			return PacketParseResult::Disconnect;
		}

		QuestAccepted(questId);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnQuestLogFull(game::IncomingPacket& packet)
	{
		QuestLogFull();
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeInvite(game::IncomingPacket& packet)
	{
		std::string inviterName;
		uint64 inviterGuid = 0;
		if (!(packet >> io::read_string(inviterName) >> io::read<uint64>(inviterGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		TradeInviteReceived(inviterName, inviterGuid);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeRequestResult(game::IncomingPacket& packet)
	{
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		TradeRequestAnswered(result);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeSessionOpened(game::IncomingPacket& packet)
	{
		std::string otherName;
		uint64 otherGuid = 0;
		if (!(packet >> io::read_string(otherName) >> io::read<uint64>(otherGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		TradeOpened(otherName, otherGuid);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTradeSessionClosed(game::IncomingPacket& packet)
	{
		uint8 reason = 0;
		if (!(packet >> io::read<uint8>(reason)))
		{
			return PacketParseResult::Disconnect;
		}

		TradeClosed(reason);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnShowMailbox(game::IncomingPacket& packet)
	{
		uint64 mailboxGuid = 0;
		if (!(packet >> io::read<uint64>(mailboxGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		MailboxOpened(mailboxGuid);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMailList(game::IncomingPacket& packet)
	{
		uint16 count = 0;
		if (!(packet >> io::read<uint16>(count)))
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<MailInfo> mails(count);
		for (MailInfo& mail : mails)
		{
			if (!(packet >> mail))
			{
				return PacketParseResult::Disconnect;
			}
		}

		MailListReceived(mails);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMailSendResult(game::IncomingPacket& packet)
	{
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		MailSendAnswered(result);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMailTakeResult(game::IncomingPacket& packet)
	{
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		MailTakeAnswered(result);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnLogoutResponse(game::IncomingPacket& packet)
	{
		LoggedOut();
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnInventoryError(game::IncomingPacket& packet)
	{
		uint8 error = 0;
		if (!(packet >> io::read<uint8>(error)))
		{
			return PacketParseResult::Disconnect;
		}

		InventoryErrorReceived(error);
		return PacketParseResult::Pass;
	}

	void BotRealmConnector::CheatGodmode(const bool enable)
	{
		sendSinglePacket([enable](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatGodmode);
			packet << io::write<uint8>(enable ? 1 : 0);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatInvisible(const bool enable)
	{
		sendSinglePacket([enable](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatInvisible);
			packet << io::write<uint8>(enable ? 1 : 0);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatSetInstanceVariable(const uint32 key, const int64 value)
	{
		sendSinglePacket([key, value](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSetInstanceVariable);
			// int64 to match WorldInstance's storage; a 32-bit field here would silently truncate
			// any value a trigger condition could legitimately hold.
			packet << io::write<uint32>(key) << io::write<int64>(value);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatDamage(const uint32 amount)
	{
		sendSinglePacket([amount](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatDamage);
			packet << io::write<uint32>(amount);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatResetCooldowns()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatResetCooldowns);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatRevive()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatRevive);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatHeal()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatHeal);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatRestorePower()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatRestorePower);
			packet.Finish();
			});
	}

	void BotRealmConnector::UseObject(const uint64 objectGuid)
	{
		sendSinglePacket([objectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::UseObject);
			packet << io::write<uint64>(objectGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::InitiateTrade(const uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeInitiate);
			packet << io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::AcceptTradeInvite()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeInviteAccept);
			packet.Finish();
			});
	}

	void BotRealmConnector::DeclineTradeInvite()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeInviteDecline);
			packet.Finish();
			});
	}

	void BotRealmConnector::TradeSetMoney(const uint32 amount)
	{
		sendSinglePacket([amount](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeSetMoney);
			packet << io::write<uint32>(amount);
			packet.Finish();
			});
	}

	void BotRealmConnector::AcceptTrade()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeAccept);
			packet.Finish();
			});
	}

	void BotRealmConnector::CancelTrade()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TradeCancelRequest);
			packet.Finish();
			});
	}

	void BotRealmConnector::RequestMailList()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailListRequest);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendMail(const std::string& recipient, const std::string& subject, const std::string& body, const uint32 money)
	{
		sendSinglePacket([&recipient, &subject, &body, money](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SendMail);
			packet
				<< io::write_dynamic_range<uint8>(recipient)
				<< io::write_dynamic_range<uint8>(subject)
				<< io::write_dynamic_range<uint16>(body)
				<< io::write<uint32>(money)
				<< io::write<uint8>(0);
			packet.Finish();
			});
	}

	void BotRealmConnector::MailTakeMoney(const uint64 mailId)
	{
		sendSinglePacket([mailId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailTakeMoney);
			packet << io::write<uint64>(mailId);
			packet.Finish();
			});
	}

	void BotRealmConnector::Logout()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LogoutRequest);
			packet.Finish();
			});
	}

	void BotRealmConnector::SetLootMethod(const uint8 method, const uint64 masterGuid, const uint8 threshold)
	{
		sendSinglePacket([method, masterGuid, threshold](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SetLootMethod);
			packet << io::write<uint8>(method) << io::write<uint64>(masterGuid) << io::write<uint8>(threshold);
			packet.Finish();
			});
	}

	void BotRealmConnector::QuestGiverStatusQuery(const uint64 questGiverGuid)
	{
		BeginRequest(bot_request::QuestStatus);
		sendSinglePacket([questGiverGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverStatusQuery);
			packet << io::write<uint64>(questGiverGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::QuestGiverHello(const uint64 questGiverGuid)
	{
		BeginRequest(bot_request::QuestHello);
		sendSinglePacket([questGiverGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverHello);
			packet << io::write<uint64>(questGiverGuid);
			packet.Finish();
			});
	}

	void BotRealmConnector::QuestGiverQueryQuest(const uint64 questGiverGuid, const uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverQueryQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void BotRealmConnector::AcceptQuest(const uint64 questGiverGuid, const uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AcceptQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void BotRealmConnector::QuestGiverCompleteQuest(const uint64 questGiverGuid, const uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverCompleteQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void BotRealmConnector::QuestGiverChooseQuestReward(const uint64 questGiverGuid, const uint32 questId, const uint32 rewardChoice)
	{
		sendSinglePacket([questGiverGuid, questId, rewardChoice](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverChooseQuestReward);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId) << io::write<uint32>(rewardChoice);
			packet.Finish();
			});
	}

	void BotRealmConnector::AbandonQuest(const uint32 questId)
	{
		sendSinglePacket([questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AbandonQuest);
			packet << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatAcceptQuest(const uint32 questId)
	{
		sendSinglePacket([questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatAcceptQuest);
			packet << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatTurnInQuest(const uint32 questId, const uint8 rewardChoice)
	{
		sendSinglePacket([questId, rewardChoice](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatTurnInQuest);
			packet << io::write<uint32>(questId) << io::write<uint8>(rewardChoice);
			packet.Finish();
			});
	}

	void BotRealmConnector::CheatClearInventory()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatClearInventory);
			packet.Finish();
			});
	}
}
