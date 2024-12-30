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

	namespace quest_status
	{
		enum Type
		{
			/// Used if the player has been rewarded for completing the quest. This means,
			/// that this quest is no longer available (does not appear in quest log) and that
			/// the next quest in the quest chain is available (if any, and all other requirements
			/// are fulfilled).
			Rewarded = 0,

			/// The quest is completed, but is still in the players quest log (has not yet been
			/// rewarded).
			Complete = 1,

			/// This quest is unavailable, because some requirements do not match.
			Unavailable = 2,

			/// This quest is in in the players quest log, but has not yet been completed.
			Incomplete = 3,

			/// This quest is available, but the player has not yet accepted it.
			Available = 4,

			/// This quest is in the players quest log, but the player failed, which prevents this quest
			/// from being completed (possible on time out or special requirements like "may not die").
			Failed = 5,

			/// Maximum number of quest status flags
			Count_ = 6
		};
	}

	typedef quest_status::Type QuestStatus;

	constexpr uint32 MaxQuestLogSize = 20;

	struct QuestField
	{
		uint32 questId = 0;
		uint32 status = quest_status::Rewarded;
		uint8 counters[4] = { 0, 0, 0, 0};
		uint32 questTimer = 0;
	};

	inline bool operator==(const QuestField& lhs, const QuestField& rhs)
	{
		return lhs.questId == rhs.questId && lhs.status == rhs.status && lhs.questTimer == rhs.questTimer;
	}
}