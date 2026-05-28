// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_incoming_packet.h"
#include "game_outgoing_packet.h"

namespace mmo
{
	namespace game
	{
		struct Protocol
		{
			typedef game::IncomingPacket IncomingPacket;
			typedef game::OutgoingPacket OutgoingPacket;
		};

		namespace guild_command
		{
			enum Type
			{
				Invite,
				Uninvite,
				Promote,
				Demote,
				SetLeader,
				SetMOTD,
				Leave,
				Disband,
			};
		}

		namespace guild_command_result
		{
			enum Type
			{
				Ok,

				NotInGuild,

				AlreadyInGuild,

				NotAllowed,

				PlayerNotFound,

				AlreadyInOtherGuild,

				InvitePending
			};
		}

		namespace friend_command_result
		{
			enum Type
			{
				Success,
				AlreadyFriends,
				FriendListFull,
				TargetFriendListFull,
				PlayerNotFound,
				CannotAddSelf,
				InvitePending,
				NotFriends
			};
		}

		constexpr uint32 MAX_FRIENDS = 50;

		constexpr uint32 ProtocolVersion = 0x00000005;

		////////////////////////////////////////////////////////////////////////////////
		// BEGIN: Client <-> Realm section

		/// Enumerates possible op codes sent by the client to a realm server.
		namespace client_realm_packet
		{
			enum Type
			{
				/// Sent by the client to respond to a AuthChallenge from a realm server.
				AuthSession = 0x00,
				/// Sent by the client to request the character list.
				CharEnum = 0x01,
				/// Creates a new character.
				CreateChar = 0x02,
				/// Deletes an existing character.
				DeleteChar = 0x03,
				/// Enters the world with a selected character.
				EnterWorld = 0x04,

				// Movement Codes
				MoveStartForward = 0x05,
				MoveStartBackward = 0x06,
				MoveStop = 0x07,
				MoveStartStrafeLeft = 0x08,
				MoveStartStrafeRight = 0x09,
				MoveStopStrafe = 0x0A,
				MoveStartTurnLeft = 0x0B,
				MoveStartTurnRight = 0x0C,
				MoveStopTurn = 0x0D,
				MoveHeartBeat = 0x0E,
				MoveSetFacing = 0x0F,

				NameQuery = 0x10,

				ChatMessage = 0x11,

				SetSelection = 0x12,

				CheatCreateMonster = 0x13,	// GAME MASTER
				CheatDestroyMonster = 0x14, // GAME MASTER

				CastSpell = 0x15,
				CancelCast = 0x16,
				CancelAura = 0x17,
				ChannelStart = 0x18,
				ChannelUpdate = 0x19,
				CancelChanneling = 0x1A,
				AttackSwing = 0x1B,
				AttackStop = 0x1C,

				CheatLearnSpell = 0x1D, // GAME MASTER
				CheatRecharge = 0x1E,	// GAME MASTER

				UseItem = 0x1F,

				CheatFollowMe = 0x20, // GAME MASTER
				CheatFaceMe = 0x21,	  // GAME MASTER

				CreatureQuery,
				ItemQuery,
				QuestQuery,

				ReviveRequest,

				ForceMoveSetWalkSpeedAck,
				ForceMoveSetRunSpeedAck,
				ForceMoveSetRunBackSpeedAck,
				ForceMoveSetSwimSpeedAck,
				ForceMoveSetSwimBackSpeedAck,
				ForceMoveSetTurnRateAck,
				ForceSetFlightSpeedAck,
				ForceSetFlightBackSpeedAck,
				MoveTeleportAck,

				AutoStoreLootItem,
				AutoEquipItem,
				SwapItem,
				SwapInvItem,
				SplitItem,
				DestroyItem,

				Loot,
				LootMoney,
				LootRelease,
				LootRoll,

				MoveJump,
				MoveFallLand,

				CheatLevelUp,	// GAME MASTER
				CheatGiveMoney, // GAME MASTER

				GossipHello,

				CheatAddItem, // GAME MASTER

				SellItem,
				BuyItem,

				AttributePoint,
				SetActionBarButton,
				TrainerBuySpell,

				QuestGiverStatusQuery,

				QuestGiverHello,
				TrainerMenu,
				ListInventory,

				AcceptQuest,
				QuestGiverQueryQuest,
				AbandonQuest,
				QuestGiverCompleteQuest,
				QuestGiverChooseQuestReward,

				MoveEnded,

				CheatWorldPort, // GAME MASTER

				MoveWorldPortAck,

				CheatSpeed,			   // GAME MASTER
				CheatSummon,		   // GAME MASTER
				CheatTeleportToPlayer, // GAME MASTER

				GroupInvite,
				GroupUninvite,
				GroupAccept,
				GroupDecline,

				RandomRoll,

				MoveSplineDone,

				GossipAction,

				GuildCreate,
				GuildInvite,
				GuildAccept,
				GuildDecline,
				GuildRemove,
				GuildLeave,
				GuildDisband,
				GuildPromote,
				GuildDemote,
				GuildMotd,

				GuildQuery,
				ObjectQuery,

				LogoutRequest,

				GuildRoster,

				MoveRootAck,

				MoveStunAck,
				MoveSleepAck,
				MoveFearAck,
				MoveDisorientAck,

				LearnTalent,

				TimePlayedRequest,

				TimeSyncResponse,

				FriendInvite,
				FriendAccept,
				FriendDecline,
				FriendRemove,
				FriendListRequest,

				AreaTriggerTriggered,

				GroupLeave,
				GroupDisband,

				/// Sent by the group leader to change the group's loot method.
				SetLootMethod,

				UseObject,

				/// Sent by the client to place a ping at their current world position, broadcast to party.
				PartyPing,

			/// Sent by the client to initiate a trade with the current target.
			TradeInitiate,

			/// Sent by the client to cancel the active trade session.
			TradeCancelRequest,

			/// Sent by the client to add an inventory item to a trade slot.
			TradeAddItem,

			/// Sent by the client to remove an item from a trade slot.
			TradeRemoveItem,

			/// Sent by the client to set the money amount offered in the trade.
			TradeSetMoney,

			/// Sent by the client to accept the current trade terms.
			TradeAccept,

			/// Sent by the target player to accept a trade invitation.
			TradeInviteAccept,

			/// Sent by the target player to decline a trade invitation.
			TradeInviteDecline,

				/// Counter constant
				Count_,
			};
		}

		/// Enumerates possible op codes sent by the client to a realm server.
		namespace realm_client_packet
		{
			enum Type
			{
				/// Sent by the client immediately after connecting to challenge it for authentication.
				AuthChallenge = 0x00,
				/// Send by the realm as response.
				AuthSessionResponse = 0x01,
				/// Sent by the realm as response to the CharEnum packet.
				CharEnum = 0x02,
				/// Resonse of the server for a character creation packet.
				CharCreateResponse = 0x03,
				/// Response of the server for a character deletion packet.
				CharDeleteResponse = 0x04,
				/// Response of the server for a enter world packet.
				LoginVerifyWorld = 0x05,
				/// Entering the world failed.
				EnterWorldFailed = 0x06,

				/// Sent right after successful auth; contains disabled race and class IDs.
				RealmConfig = 0x07,

				/// [PROXY] Update game objects.
				UpdateObject,
				/// [PROXY] Compressed update game objects.
				CompressedUpdateObject,
				/// [PROXY] Destroy game objects.
				DestroyObjects,

				// Movement Codes

				/// [PROXY]
				MoveStartForward,
				/// [PROXY]
				MoveStartBackward,
				/// [PROXY]
				MoveStop,
				/// [PROXY]
				MoveStartStrafeLeft,
				/// [PROXY]
				MoveStartStrafeRight,
				/// [PROXY]
				MoveStopStrafe,
				/// [PROXY]
				MoveStartTurnLeft,
				/// [PROXY]
				MoveStartTurnRight,
				/// [PROXY]
				MoveStopTurn,
				/// [PROXY]
				MoveHeartBeat,
				/// [PROXY]
				MoveSetFacing,

				NameQueryResult,

				ChatMessage,

				InitialSpells,
				LearnedSpell,
				UnlearnedSpell,
				AttackStart,
				AttackStop,
				AttackSwingError,

				CastFailed,
				SpellStart,
				SpellGo,
				SpellFailure,
				SpellCooldown,
				UpdateAuraDuration,
				ChannelStart,
				ChannelUpdate,

				CreatureMove,

				SpellDamageLog,
				NonSpellDamageLog,
				XpLog,

				CreatureQueryResult,
				ItemQueryResult,
				QuestQueryResult,

				ForceMoveSetWalkSpeed,
				ForceMoveSetRunSpeed,
				ForceMoveSetRunBackSpeed,
				ForceMoveSetSwimSpeed,
				ForceMoveSetSwimBackSpeed,
				ForceMoveSetTurnRate,
				ForceSetFlightSpeed,
				ForceSetFlightBackSpeed,

				MoveSetWalkSpeed,
				MoveSetRunSpeed,
				MoveSetRunBackSpeed,
				MoveSetSwimSpeed,
				MoveSetSwimBackSpeed,
				MoveSetTurnRate,
				SetFlightSpeed,
				SetFlightBackSpeed,

				LootResponse,
				LootReleaseResponse,
				LootRemoved,
				LootMoneyNotify,
				LootItemNotify,
				LootClearMoney,
				StartLootRoll,
				LootRollWon,
				LootAllPassed,
				LootRollResult,

				MoveTeleportAck,

				MoveJump,
				MoveFallLand,

				ListInventory,

				LevelUp,
				AuraUpdate,

				PeriodicAuraLog,
				ActionButtons,

				TrainerList,
				TrainerBuyError,
				TrainerBuySucceeded,

				QuestGiverStatus,
				QuestGiverQuestList,
				QuestGiverQuestDetails,
				QuestGiverRequestItems,
				QuestGiverOfferReward,
				QuestGiverQuestComplete,
				QuestLogFull,
				QuestUpdateComplete,
				QuestUpdateAddKill,
				QuestUpdateAddItem,

				GossipComplete,

				SpellEnergizeLog,
				MoveEnded,
				
				QuestAccepted,
				QuestAbandoned,

				TransferPending,

				NewWorld,
				TransferAborted,

				PartyCommandResult,
				PartyMemberStats,

				GroupInvite,
				GroupCancel,
				GroupDecline,
				GroupUninvite,
				GroupSetLeader,
				GroupDestroyed,
				GroupList,

				RandomRollResult,

				AttackerStateUpdate,

				SpellHealLog,

				MoveSplineDone,
				GossipMenu,

				GuildInvite,
				GuildDecline,
				GuildCommandResult,
				GuildQueryResponse,
				GuildUninvite,
				GuildEvent,

				ObjectQueryResult,

				ItemPushResult,

				/// Inventory error message sent to the client
				InventoryError,

				LogoutResponse,
				GuildRoster,
				/// Message of the day packet sent to the client
				MessageOfTheDay,

				MoveRoot,

				MoveStun,
				MoveSleep,
				MoveFear,
				MoveDisorient,

				/// Game time notification packet sent to the client
				GameTimeInfo,

				SpellSuperceeded,
				SetProficiency,

				/// Sent to the client when a spell modifier changes on the player's character.
				/// Contains: uint8 modType (Flat=0/Pct=1), uint8 effectIndex, uint8 modOp, int32 totalValue
				SpellModChanged,

				/// Environmental damage log packet (fall damage, drowning, lava, etc.)
				EnvironmentalDamageLog,

				/// Time played response packet sent to the client
				TimePlayedResponse,

				/// Time sync request packet sent to the client
				TimeSyncRequest,

				FriendInvite,
				FriendListUpdate,
				FriendStatusChange,
				FriendCommandResult,

				/// Broadcast ping from a party member (sender guid, x, z world position).
				PartyPing,

			/// Sent to the target player when someone requests a trade.
			TradeInvite,

			/// Sent to the initiating player with the result of the trade request.
			TradeRequestResult,

			/// Sent to both players when a trade session is successfully opened.
			TradeSessionOpened,

			/// Sent to both players when a trade session ends for any reason.
			TradeSessionClosed,

			/// Sent to a player when the other player's trade offer changes.
			TradeUpdate,

			/// Sent to both players when the acceptance state changes.
			TradeAcceptUpdate,

				/// Counter constant
				Count_,
			};
		}

		/// Result codes sent in TradeRequestResult.
		namespace trade_result
		{
			enum Type
			{
				Success = 0,
				TargetBusy = 1,
				TooFarAway = 2,
				PlayerNotFound = 3,
				YouAreDead = 4,
				TargetIsDead = 5,
				AlreadyTrading = 6,
				TargetAlreadyTrading = 7,
				Hostile = 8,
				TargetIsLoggingOut = 9,
				YouAreInCombat = 10,
				TargetIsInCombat = 11,
				Declined = 12,
			};
		}

		/// Reason codes sent in TradeSessionClosed.
		namespace trade_close_reason
		{
			enum Type
			{
				Completed = 0,
				Cancelled = 1,
				TooFarAway = 2,
				Death = 3,
				Hostile = 4,
				Disconnected = 5,
			};
		}

		namespace player_login_response
		{
			enum Type
			{
				NoWorldServer,
				NoCharacter,
				RaceOrClassDisabled,
			};
		};

		////////////////////////////////////////////////////////////////////////////////
		// Typedefs

		/// Enumerates possible authentification result codes.
		namespace auth_result
		{
			enum Type
			{
				/// Success.
				Success,
				/// Could not log in at this time. Please try again later.
				FailDbBusy,
				/// Unable to validate game version. This may be caused by file corruption or interference of another program. Please visit <site> for more information and possible solutions to this issue.
				FailVersionInvalid,
				/// Downloading...
				FailVersionUpdate,
				/// Unable to connect.
				FailInvalidServer,
				/// Unable to connect.
				FailNoAccess,
				/// Internal error.
				FailInternalError,

				Count_
			};
		}

		typedef auth_result::Type AuthResult;

		/// Enumerates possible character creation results.
		namespace char_create_result
		{
			enum Type
			{
				Unknown,

				/// Something went wrong like unknown class, race etc.
				Error,

				/// The character name is in use.
				NameInUse,

				/// The character class / race is disabled.
				Disabled,

				/// Number of characters on the realm has been reached.
				ServerLimit,

				/// Number of characters on the account has been reached.
				AccountLimit,

				/// The server currently only allows creating characters for players who already have an existing character on the server.
				OnlyExisting,
			};
		}

		typedef char_create_result::Type CharCreateResult;

		namespace transfer_abort_reason
		{
			enum Type
			{
				None,

				/// Transfer Aborted: instance is full
				MaxPlayers,

				/// Transfer aborted: instance not found
				NotFound,

				/// You have entered too many instances recently.
				TooManyInstances,

				/// Unable to zone in while an encounter is in progress.
				ZoneInCombat
			};
		}

		typedef transfer_abort_reason::Type TransferAbortReason;
	}
}
