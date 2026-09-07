// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_object_manager.h"
#include "mmo_client/realm_data.h"

#include "game_protocol/game_connector.h"
#include "auth_protocol/auth_protocol.h"
#include "base/big_number.h"
#include "base/signal.h"
#include "game/character_view.h"
#include "game/movement_type.h"
#include "game/movement_info.h"
#include "game/chat_type.h"
#include "game/auto_attack.h"
#include "game/spell_target_map.h"

#include "binary_io/reader.h"

#include "asio/io_service.hpp"

#include <optional>

namespace mmo
{
	/// Represents a party member for the bot.
	struct BotPartyMember
	{
		uint64 guid { 0 };
		std::string name;
		uint8 group { 0 };
		bool assistant { false };
		uint32 status { 0 };
		uint16 health { 0 };
		uint16 maxHealth { 0 };
		uint8 powerType { 0 };
		uint16 power { 0 };
		uint16 maxPower { 0 };
		uint16 level { 0 };
		Vector3 position { Vector3::Zero };
		bool hasHealth { false };
		bool hasPower { false };
		bool hasLevel { false };
		bool hasPosition { false };
	};

	/// Minimal realm connector variant that tolerates unknown packets and exposes the hooks needed for the bot.
	class BotRealmConnector final
		: public game::Connector
		, public game::IConnectorListener
	{
	public:
		signal<void(uint8)> AuthenticationResult;
		signal<void()> CharListUpdated;
		signal<void()> Disconnected;
		signal<void(game::player_login_response::Type)> EnterWorldFailed;
		signal<void(uint32, Vector3, float)> VerifyNewWorld;
		signal<void(game::CharCreateResult)> CharacterCreated;
		signal<void(const std::string&)> PartyInvitationReceived;
		/// @brief Emitted when the bot joins a party or receives an updated party list.
		/// @param leaderGuid The GUID of the party leader.
		/// @param memberCount The number of members in the party.
		signal<void(uint64, uint32)> PartyJoined;
		/// @brief Emitted when the bot leaves or is removed from a party.
		signal<void()> PartyLeft;

		/// @brief Emitted when a new unit is spawned in the world.
		signal<void(const BotUnit&)> UnitSpawned;

		/// @brief Emitted when a unit is despawned from the world.
		signal<void(uint64)> UnitDespawned;

		/// @brief Emitted when a unit's data is updated.
		signal<void(const BotUnit&)> UnitUpdated;

		// ============================================================
		// Combat Signals
		// ============================================================

		/// @brief Emitted when auto-attack starts (confirmed by server).
		/// @param attackerGuid The GUID of the attacker.
		/// @param victimGuid The GUID of the victim.
		signal<void(uint64, uint64)> AttackStarted;

		/// @brief Emitted when auto-attack stops (confirmed by server).
		/// @param attackerGuid The GUID of the attacker.
		signal<void(uint64)> AttackStopped;

		/// @brief Emitted when an attack swing error occurs.
		/// @param error The error code (see attack_swing_event).
		signal<void(AttackSwingEvent)> AttackSwingError;

		/// @brief Emitted when an attack hit occurs (attacker state update).
		/// @param attackerGuid The GUID of the attacker.
		/// @param victimGuid The GUID of the victim.
		/// @param damage The total damage dealt.
		/// @param hitInfo Hit flags (miss, crit, etc.).
		/// @param victimState Victim state (dodge, parry, block, etc.).
		signal<void(uint64, uint64, uint32, uint32, uint32)> AttackHit;

		/// @brief Emitted when we take non-spell damage.
		/// @param targetGuid Our GUID (the damage target).
		/// @param damage The damage amount.
		/// @param flags Damage flags (crit, crushing, etc.).
		signal<void(uint64, uint32, uint8)> DamageReceived;

	private:
		asio::io_service& m_ioService;
		std::string m_realmAddress;
		uint16 m_realmPort { 0 };
		std::string m_realmName;
		std::string m_account;
		BigNumber m_sessionKey;
		uint32 m_serverSeed { 0 };
		uint32 m_clientSeed { 0 };
		uint32 m_realmId { 0 };

		uint64 m_selectedCharacterGuid { 0 };
		MovementInfo m_movementInfo;
		std::string m_lastSpellStateIssue;

		/// Reason the realm gave for terminating this session, if any.
		std::optional<auth::SessionKickReason> m_kickReason;

		/// Last spell visualization the server told us to play, if any.
		struct SpellVisual
		{
			uint64 targetGuid { 0 };
			uint32 visualizationId { 0 };
			uint8 visualEvent { 0 };
		};
		std::optional<SpellVisual> m_lastSpellVisual;

		// Party state
		std::vector<BotPartyMember> m_partyMembers;
		uint64 m_partyLeaderGuid { 0 };
		bool m_inParty { false };

		// Object management
		BotObjectManager m_objectManager;

		// Combat state
		bool m_isAutoAttacking { false };
		uint64 m_autoAttackTargetGuid { 0 };

		// Line of sight debug check state (dev command responses)
		uint32 m_losResultCounter { 0 };
		bool m_lastLosResult { false };

	public:
		/// A list of character views.
		std::vector<CharacterView> m_characterViews;

	public:
		explicit BotRealmConnector(asio::io_service& io);

		/// Accept unhandled packets without disconnecting.
		PacketParseResult HandleIncomingPacket(game::IncomingPacket& packet) override;

		/// Why the realm terminated this session, if it said so before closing the connection.
		[[nodiscard]] std::optional<auth::SessionKickReason> GetKickReason() const { return m_kickReason; }

		/// Visualization id of the most recent PlaySpellVisual the server sent, or 0 for none.
		/// A spell visual has no gameplay side effect to assert on, so a scenario observes the
		/// packet itself.
		[[nodiscard]] uint32 GetLastSpellVisualId() const { return m_lastSpellVisual ? m_lastSpellVisual->visualizationId : 0; }

		/// Guid the last observed spell visual played on, or 0 for none.
		[[nodiscard]] uint64 GetLastSpellVisualTarget() const { return m_lastSpellVisual ? m_lastSpellVisual->targetGuid : 0; }

		/// Visualization event of the last observed spell visual, or 255 for none.
		[[nodiscard]] uint8 GetLastSpellVisualEvent() const { return m_lastSpellVisual ? m_lastSpellVisual->visualEvent : 255; }

		/// Forgets the last observed spell visual, so a scenario can wait for the next one.
		void ClearLastSpellVisual() { m_lastSpellVisual.reset(); }

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(game::IncomingPacket& packet) override;
		// ~ End IConnectorListener

		void SetLoginData(const std::string& accountName, const BigNumber& sessionKey);

		void ConnectToRealm(const RealmData& data);

		void Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey);

		void EnterWorld(const CharacterView& character);

		void CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender, const AvatarConfiguration& customization);

		void RequestCharEnum();

		void SendChatMessage(const String& message, ChatType chatType, const String& target);

		void SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info);

		void SendTimeSyncResponse(uint32 syncIndex, GameTime clientTimestamp);

		void SendMoveWorldPortAck();

		void SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo);

		void SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo);

		/// Acknowledges a MoveCharge packet so the server can start the charge movement.
		void SendMoveChargeAck(uint32 ackId, const MovementInfo& movementInfo, float speed);

		const std::vector<CharacterView>& GetCharacterViews() const { return m_characterViews; }

		const MovementInfo& GetMovementInfo() const { return m_movementInfo; }

		uint64 GetSelectedGuid() const { return m_selectedCharacterGuid; }

		void AcceptPartyInvitation();

		void DeclinePartyInvitation();

		// ============================================================
		// Party Information Methods
		// ============================================================

		/// Checks if the bot is currently in a party.
		bool IsInParty() const { return m_inParty; }

		/// Gets the number of members in the party (including the bot).
		uint32 GetPartyMemberCount() const { return m_inParty ? static_cast<uint32>(m_partyMembers.size()) : 0; }

		/// Gets the GUID of the party leader.
		uint64 GetPartyLeaderGuid() const { return m_partyLeaderGuid; }

		/// Checks if the bot is the party leader.
		bool IsPartyLeader() const { return m_inParty && m_partyLeaderGuid == m_selectedCharacterGuid; }

		/// Gets a party member by index.
		const BotPartyMember* GetPartyMember(uint32 index) const;

		/// Gets a party member by GUID.
		const BotPartyMember* GetPartyMemberByGuid(uint64 guid) const;

		/// Gets all party member GUIDs.
		std::vector<uint64> GetPartyMemberGuids() const;

		// ============================================================
		// Party Action Methods
		// ============================================================

		/// Leaves the current party.
		void LeaveParty();

		/// Kicks a player from the party by name.
		void KickFromParty(const std::string& playerName);

		/// Invites a player to the party by name.
		void InviteToParty(const std::string& playerName);

		// ============================================================
		// Object Management Methods
		// ============================================================

		/// Gets the object manager containing all known units.
		BotObjectManager& GetObjectManager() { return m_objectManager; }

		/// Gets the object manager containing all known units (const).
		const BotObjectManager& GetObjectManager() const { return m_objectManager; }

		/// Gets the latest spell-state issue mirrored by packet handling or cast validation.
		const std::string& GetLastSpellStateIssue() const { return m_lastSpellStateIssue; }

		// ============================================================
		// Combat Methods
		// ============================================================

		/// Starts auto-attack against a target.
		/// @param targetGuid The GUID of the target to attack.
		void SendAttackStart(uint64 targetGuid);

		/// Stops auto-attack.
		void SendAttackStop();

		/// Releases the corpse and revives at the bind point, the same way a player does after
		/// dying. Deliberately not the GM revive cheat: the point of a bot is to walk the paths a
		/// player walks, and the release path is where the interesting server work happens.
		void SendReviveRequest();

		/// Sends a spell cast request using the standard spell target map contract.
		/// @return True if the request was queued locally, false if it was rejected before send.
		bool SendCastSpell(uint32 spellId, const SpellTargetMap& targetMap, bool autoFlush = true);

		/// Checks if the bot is currently auto-attacking.
		bool IsAutoAttacking() const { return m_isAutoAttacking; }

		/// Gets the GUID of the current auto-attack target.
		uint64 GetAutoAttackTarget() const { return m_autoAttackTargetGuid; }

		/// Registers the world/runtime packet handlers without going through live auth.
		/// Exposed to keep packet-level unit tests deterministic.
		void PrimeWorldSessionForTesting(uint64 selectedGuid = 0);

		// ============================================================
		// Target Selection & GM Commands (server built with MMO_WITH_DEV_COMMANDS)
		// ============================================================

		/// Sets the selected target object of the controlled player (0 to deselect).
		void SetSelection(uint64 guid);

		/// GAME MASTER only. Spawns a temporary monster near the player.
		void CheatCreateMonster(uint32 entry);

		/// GAME MASTER only. Destroys the monster with the given guid.
		void CheatDestroyMonster(uint64 guid);

		/// GAME MASTER only. Spawns a temporary world object (e.g. a door) at the player's
		/// position with the given initial state (doors: 0 = closed, 1 = open).
		void CheatCreateObject(uint32 entry, uint32 state);

		/// GAME MASTER only. Requests a server-side line of sight check from the player to the
		/// object with the given guid. The result arrives asynchronously — poll GetLosResultCounter
		/// for a change and then read GetLastLosResult.
		void CheatCheckLineOfSight(uint64 targetGuid);

		/// Returns how many DebugLineOfSightResult packets have been received so far.
		uint32 GetLosResultCounter() const { return m_losResultCounter; }

		/// Returns the result of the most recent line of sight check (true = clear).
		bool GetLastLosResult() const { return m_lastLosResult; }

		/// GAME MASTER only. Learns the given spell.
		void CheatLearnSpell(uint32 spellId);

		/// GAME MASTER only. Unlocks the given emote.
		void CheatLearnEmote(uint32 emoteId);

		/// Performs an animated emote (one-shot, pose or mood).
		void SendEmote(uint32 emoteId, uint64 targetGuid);

		/// Cycles the pose variant of the current stand-state context (/pose).
		void SendCyclePose();

		/// GAME MASTER only. Increases the player level by the given amount.
		void CheatLevelUp(uint8 levels);

		/// GAME MASTER only. Increases the active class level by the given amount.
		void CheatClassLevelUp(uint8 levels);

		/// GAME MASTER only. Gives money (in copper) to the player.
		void CheatGiveMoney(uint32 amount);

		/// GAME MASTER only. Adds an item to the player inventory.
		void CheatAddItem(uint32 itemId, uint8 count);

		/// GAME MASTER only. Teleports the player to the given map position.
		void CheatWorldPort(uint32 mapId, const Vector3& position, float facing);

		/// GAME MASTER only. Changes the player movement speed.
		void CheatSpeed(float speed);

		/// GAME MASTER only. Instantly kills the selected unit.
		void CheatKill();

		/// GAME MASTER only. Toggles damage immunity on the sender's character.
		void CheatGodmode(bool enable);

		/// GAME MASTER only. Sets an instance-scoped variable in the world instance the character is
		///	in, so a scenario can put an encounter into a given state without having to reach it
		///	through gameplay.
		///	@param key The variable key, matching the one an InstanceVariable condition reads.
		///	@param value The value to assign.
		void CheatSetInstanceVariable(uint32 key, int64 value);

		/// GAME MASTER only. Deals raw damage to the currently selected unit, through the normal
		///	damage path so health-threshold triggers fire. Lets a scenario walk a boss across its
		///	phase thresholds without depending on the test character's damage output.
		///	@param amount The amount of damage to deal.
		void CheatDamage(uint32 amount);

		/// GAME MASTER only. Accepts the given quest without a questgiver interaction.
		void CheatAcceptQuest(uint32 questId);

		/// GAME MASTER only. Turns in the given completed quest without a quest ender interaction.
		void CheatTurnInQuest(uint32 questId, uint8 rewardChoice);

		/// GAME MASTER only. Destroys every item in the characters backpack (equipment is kept).
		void CheatClearInventory();

	private:
		void RegisterWorldPacketHandlers();
		BotUnit* GetSelfMutable();
		const BotUnit* GetSelf() const;
		void UpdateSpellStateIssue(const std::string& issue);
		void ClearSpellStateIssue();

		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);

		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);

		PacketParseResult OnKickReason(game::IncomingPacket& packet);

		PacketParseResult OnPlaySpellVisual(game::IncomingPacket& packet);

		PacketParseResult OnCharEnum(game::IncomingPacket& packet);

		PacketParseResult OnLoginVerifyWorld(game::IncomingPacket& packet);

		PacketParseResult OnEnterWorldFailed(game::IncomingPacket& packet);

		PacketParseResult OnTimeSyncRequest(game::IncomingPacket& packet);

		PacketParseResult OnTransferPending(game::IncomingPacket& packet);

		PacketParseResult OnNewWorld(game::IncomingPacket& packet);

		PacketParseResult OnCharCreateResponse(game::IncomingPacket& packet);

		PacketParseResult OnMoveTeleport(game::IncomingPacket& packet);

		/// Handles the MoveCharge packet: acknowledges immediately so the server starts the
		/// charge movement (the bot does not simulate the path; the server moves the character).
		PacketParseResult OnMoveCharge(game::IncomingPacket& packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket& packet);

		PacketParseResult OnIgnoredPacket(game::IncomingPacket& packet);

		PacketParseResult OnGroupInvite(game::IncomingPacket& packet);

		PacketParseResult OnGroupList(game::IncomingPacket& packet);

		PacketParseResult OnGroupDestroyed(game::IncomingPacket& packet);

		PacketParseResult OnGroupSetLeader(game::IncomingPacket& packet);

		PacketParseResult OnPartyMemberStats(game::IncomingPacket& packet);

		PacketParseResult OnUpdateObject(game::IncomingPacket& packet);

		PacketParseResult OnCompressedUpdateObject(game::IncomingPacket& packet);

		PacketParseResult HandleObjectUpdate(io::Reader& reader);

		PacketParseResult OnDestroyObjects(game::IncomingPacket& packet);

		PacketParseResult OnDebugLineOfSightResult(game::IncomingPacket& packet);

		PacketParseResult OnNameQueryResult(game::IncomingPacket& packet);

		/// @brief Handles movement packets from other units.
		PacketParseResult OnMovementPacket(game::IncomingPacket& packet);

		/// @brief Handles AI-driven spline movement (CreatureMove) from NPCs. The bot does not
		/// interpolate the spline - it jumps the tracked unit straight to the announced
		/// destination so later position/facing queries (FaceUnit, GetPosX/Y/Z) reflect where a
		/// moving creature ends up instead of staying frozen at its spawn position.
		PacketParseResult OnCreatureMove(game::IncomingPacket& packet);

		PacketParseResult OnInitialSpells(game::IncomingPacket& packet);
		PacketParseResult OnLearnedSpell(game::IncomingPacket& packet);
		PacketParseResult OnUnlearnedSpell(game::IncomingPacket& packet);
		PacketParseResult OnSpellStart(game::IncomingPacket& packet);
		PacketParseResult OnSpellGo(game::IncomingPacket& packet);
		PacketParseResult OnSpellFailure(game::IncomingPacket& packet);
		PacketParseResult OnSpellCooldown(game::IncomingPacket& packet);
		PacketParseResult OnAuraUpdate(game::IncomingPacket& packet);

		// ============================================================
		// Combat Packet Handlers
		// ============================================================

		/// @brief Handles the AttackStart packet from server.
		PacketParseResult OnAttackStart(game::IncomingPacket& packet);

		/// @brief Handles the AttackStop packet from server.
		PacketParseResult OnAttackStop(game::IncomingPacket& packet);

		/// @brief Handles the AttackSwingError packet from server.
		PacketParseResult OnAttackSwingError(game::IncomingPacket& packet);

		/// @brief Handles the AttackerStateUpdate packet from server.
		PacketParseResult OnAttackerStateUpdate(game::IncomingPacket& packet);

		/// @brief Handles the NonSpellDamageLog packet from server.
		PacketParseResult OnNonSpellDamageLog(game::IncomingPacket& packet);

		/// @brief Parses a single object update from the packet stream.
		/// @param packet The packet to read from.
		/// @param creation Whether this is a creation (new object) or update.
		/// @param typeId The type ID of the object.
		/// @return True if parsing succeeded.
		bool ParseObjectUpdate(io::Reader& reader, bool creation, ObjectTypeId typeId);
	};
}
