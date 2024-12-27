#pragma once

namespace mmo
{
	namespace questgiver_status
	{
		enum Type
		{
			/// No status
			None = 0,

			/// NPC does not talk currently
			Unavailable = 1,

			/// Chat bubble: NPC wants to talk and has a custom menu.
			Chat = 2,

			/// Grey "?" above the head
			Incomplete = 3,

			/// Blue "?" above the head
			RewardRep = 4,

			/// Blue "!" above the head
			AvailableRep = 5,

			/// Yellow "!" above the head
			Available = 6,

			/// Yellow "?" above the head, but does not appear on mini map.
			RewardNoDot = 7,

			/// Yellow "?" above the head
			Reward = 8,

			Count_ = 9
		};
	}

	typedef questgiver_status::Type QuestgiverStatus;

	namespace quest_flags
	{
		enum Type
		{
			/// Player has to stay alive.
			StayAlive = 0x0001,

			/// All party members will be offered to accept this quest.
			PartyAccept = 0x0002,

			/// Indicates that a player can share this quest.
			Sharable = 0x0004,

			/// Raid quest.
			Raid = 0x0008,

			/// Quest rewards are hidden until the quest is completed and never appear in the clients quest log.
			HiddenRewards = 0x0010,

			/// Quest will be automatically rewarded on quest completion..
			AutoRewarded = 0x0020,

			/// This quest is repeatable once per day.
			Daily = 0x0040,

			/// This quest is repeatable once per week.
			Weekly = 0x0080,
		};
	}

	typedef quest_flags::Type QuestFlags;
}