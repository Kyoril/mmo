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

			/// Quest is not auto completed but needs an event to complete.
			Exploration = 0x0100,

			/// This quest can be repeated without any time restriction (immediately available again
			/// after being rewarded). Daily and Weekly imply repeatability as well.
			Repeatable = 0x0200,
		};
	}

	typedef quest_flags::Type QuestFlags;

	/// Determines whether a quest with the given flags can be repeated at all
	/// (either without restriction or on a daily/weekly interval).
	/// @param flags The quest flags bitmask.
	/// @returns True if the quest is repeatable in any form.
	constexpr bool IsRepeatableQuest(const uint32 flags)
	{
		return (flags & (quest_flags::Repeatable | quest_flags::Daily | quest_flags::Weekly)) != 0;
	}

	/// Determines whether a quest with the given flags resets on a fixed interval (daily/weekly)
	/// rather than being immediately repeatable.
	/// @param flags The quest flags bitmask.
	/// @returns True if the quest resets on a daily or weekly interval.
	constexpr bool IsIntervalQuest(const uint32 flags)
	{
		return (flags & (quest_flags::Daily | quest_flags::Weekly)) != 0;
	}

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

			AvailableNextLevel,

			/// Maximum number of quest status flags
			Count_
		};
	}

	typedef quest_status::Type QuestStatus;

	/// Returns true if the given quest class mask permits the given (0-based) class id. A mask of 0
	/// means "no class restriction".
	/// NOTE: the bit convention is 1 << (classId - 1), matching the editor and all existing checks.
	/// Class ids are 0-based, so class id 0 (Mage) wraps to bit 31; the explicit `& 31` makes that
	/// wrap portable. Do not change the convention here without migrating all authored quest data.
	inline bool IsQuestClassAllowed(const uint32 requiredClasses, const uint32 classId)
	{
		return requiredClasses == 0 || (requiredClasses & (1u << ((classId - 1) & 31))) != 0;
	}

	constexpr uint32 MaxQuestLogSize = 20;

	struct QuestField
	{
		uint32 questId = 0;
		uint32 status = quest_status::Rewarded;
		union
		{
			uint32 counterField = 0;
			uint8 counters[4];
		};
		
		uint32 questTimer = 0;
	};

	inline bool operator==(const QuestField& lhs, const QuestField& rhs)
	{
		return lhs.questId == rhs.questId && lhs.status == rhs.status && lhs.questTimer == rhs.questTimer && lhs.counterField == rhs.counterField;
	}
}