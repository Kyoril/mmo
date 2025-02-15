#pragma once

namespace mmo
{

	namespace roll_vote
	{
		enum Type
		{
			Pass,
			Need,
			Greed,

			NotEmittedYet,
			Invalid,
		};
	}

	typedef roll_vote::Type RollVote;

	namespace group_type
	{
		enum Type
		{
			Normal,
			Raid,
		};
	}

	typedef group_type::Type GroupType;

	namespace loot_method
	{
		enum Type
		{
			FreeForAll,

			RoundRobin,

			MasterLoot,

			GroupLoot
		};
	}

	typedef loot_method::Type LootMethod;

	namespace party_operation
	{
		enum Type
		{
			Invite,
			Leave
		};
	}

	typedef party_operation::Type PartyOperation;

	namespace party_result
	{
		enum Type
		{
			/// Silent, no client output.
			Ok,

			/// You cannot invite yourself.
			CantInviteYourself,

			/// Cannot find player "%s".
			CantFindTarget,

			/// %s is not in your party.
			NotInYourParty,

			/// %s is not in your instance.
			NotInYourInstance,

			/// Your party is full.
			PartyFull,

			/// %s is already in a group.
			AlreadyInGroup,

			/// You aren't in a party.
			YouNotInGroup,

			/// You are not the party leader.
			YouNotLeader,

			/// Target is unfriendly.
			TargetUnfriendly,

			/// %s is ignoring you.
			TargetIgnoreYou
		};
	}

	typedef party_result::Type PartyResult;

	/// Stores data of a group member.
	struct GroupMemberSlot final
	{
		/// Group member name.
		String name;

		/// Group id (0-7)
		uint8 group;

		/// Is assistant?
		bool assistant;

		/// Status
		uint32 status;

		///
		GroupMemberSlot()
			: group(0)
			, assistant(false)
			, status(0)
		{
		}
	};

	namespace group_update_flags
	{
		enum Type
		{
			None = 0,

			Status = 1 << 0,
			CurrentHP = 1 << 1,
			MaxHP = 1 << 2,
			PowerType = 1 << 3,
			CurrentPower = 1 << 4,
			MaxPower = 1 << 5,
			Level = 1 << 6,
			Zone = 1 << 7,
			Position = 1 << 8,
			Auras = 1 << 9,

			Full = (Status | CurrentHP | MaxHP | PowerType | CurrentPower | MaxPower | Level | Zone | Position | Auras)
		};
	}

	namespace group_member_status
	{
		enum Type
		{
			Offline = 0,
			Online = 1 << 1,
			Dead = 1 << 2
		};
	}

}