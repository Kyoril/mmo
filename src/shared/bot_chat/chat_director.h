// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "chat_budget.h"
#include "chat_model.h"
#include "chat_persona.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "game/chat_type.h"

#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace mmo
{
	/// What the chat director needs from one bot. Implemented by the swarm host on top of a bot
	/// session, and by fakes in tests.
	class IChatParticipant
	{
	public:
		virtual ~IChatParticipant() = default;

		[[nodiscard]] virtual const std::string& GetCharacterName() const = 0;

		[[nodiscard]] virtual bool IsInWorld() const = 0;

		/// Who and where the character is right now, as a phrase for the prompt, e.g.
		/// "a level 7 human warrior in Elwynn Forest, currently fighting a Kobold Vermin".
		[[nodiscard]] virtual std::string DescribeSelf() const = 0;

		/// Names of the other party members; empty when not in a party.
		[[nodiscard]] virtual std::vector<std::string> GetPartyMemberNames() const = 0;

		/// Global id of the General channel, or 0 if the character is not in it.
		[[nodiscard]] virtual uint32 GetGeneralChannelId() const = 0;

		/// Sends a chat line. For ChatType::Whisper the target is a character name, for
		/// ChatType::Channel the global channel id as a string.
		virtual void SendChat(ChatType type, const std::string& text, const std::string& target) = 0;

		/// Whether the character may invite someone into its party right now: not in one, or
		/// leading one with room left.
		[[nodiscard]] virtual bool CanInvite() const = 0;

		/// Invites a character into the party by name.
		virtual void InviteToParty(const std::string& name) = 0;
	};

	struct ChatDirectorSettings final
	{
		/// The model's max_tokens. A chat line needs a few dozen at most.
		uint32 maxOutputTokens { 40 };

		/// How long after the last sign of a real player bots keep posting unprompted. Without
		/// a human around, unprompted chat is money spent on an audience of bots.
		GameTime humanActivityWindowMs { 30 * 60 * 1000 };

		/// Interval between one bot's unprompted posts, rolled between these.
		GameTime proactiveMinIntervalMs { 25 * 60 * 1000 };
		GameTime proactiveMaxIntervalMs { 90 * 60 * 1000 };

		/// Minimum gap between any two unprompted posts in General, across the whole swarm.
		GameTime generalProactiveGapMs { 4 * 60 * 1000 };

		/// Minimum gap between any two bot replies in General, across the whole swarm.
		GameTime generalReplyGapMs { 45 * 1000 };

		/// Whisper replies one bot sends one person per window, before it goes quiet on them.
		/// Keeps a player who spams a bot from spending the budget.
		uint32 whisperRepliesPerWindow { 8 };
		GameTime whisperWindowMs { 10 * 60 * 1000 };

		/// Scales the chance that a bot answers another bot. Bots never answer a bot that was
		/// itself answering, so bot conversations stop after one exchange regardless.
		float botToBotScale { 1.0f };

		/// Interval between one bot's unprompted /say lines while a real player stands near it,
		/// rolled between these, and the minimum gap between any two across the swarm.
		GameTime sayMinIntervalMs { 4 * 60 * 1000 };
		GameTime sayMaxIntervalMs { 15 * 60 * 1000 };
		GameTime sayGapMs { 90 * 1000 };

		/// How long after a real player was last seen near a bot it still counts as nearby.
		GameTime humanNearbyWindowMs { 60 * 1000 };

		/// A bot invites the same person at most once per this long.
		GameTime inviteCooldownMs { 10 * 60 * 1000 };

		/// Extra facts about the game world for the system prompt: zone names, classes, notable
		/// features. Optional.
		std::string gameFacts;

		/// Seed for the director's own randomness (who answers, delays, topics).
		uint32 seed { 0 };
	};

	struct ChatDirectorStats final
	{
		uint64 requestsSubmitted { 0 };
		uint64 budgetDenied { 0 };
		uint64 modelErrors { 0 };
		uint64 repliesSkipped { 0 };
		uint64 linesSent { 0 };
		uint64 linesDropped { 0 };
		uint64 whispersThrottled { 0 };
		uint64 invitesSent { 0 };
	};

	/// Decides which bot says what in chat, and when, for a whole swarm.
	///
	/// One director per process, so that swarm-wide limits hold: one bot - not every bot in the
	/// channel - answers a line in General, General is not flooded with unprompted posts, and
	/// every request goes through the one budget. Single-threaded; everything is called from the
	/// host frame.
	class ChatDirector final : public NonCopyable
	{
	public:
		/// A bot posted a line. For telemetry.
		signal<void(uint32 botIndex, ChatType type, const std::string& target, const std::string& text)> ChatSent;

		/// The model answered, but with nothing to send: it chose to stay silent, or the answer
		/// was rejected by SanitizeChatReply. For telemetry, with the raw answer.
		signal<void(uint32 botIndex, const std::string& rawText)> ReplyDiscarded;

		/// A request failed or was refused. For telemetry; the reason never contains the API key.
		signal<void(uint32 botIndex, const std::string& reason)> RequestFailed;

		/// A bot invited someone into its party because the conversation called for it.
		signal<void(uint32 botIndex, const std::string& name)> InviteSent;

		/// @param wallClock Source of wall clock time for the budget's day and hour boundaries.
		ChatDirector(ChatDirectorSettings settings, IChatModel& model, ChatBudget& budget,
			std::function<std::time_t()> wallClock);

		/// Registers a bot. The participant must outlive the director.
		void AddBot(uint32 index, IChatParticipant& participant, uint32 seed);

		/// Reports a chat line a bot received, with the sender's name already resolved.
		void OnChatReceived(uint32 botIndex, ChatType type, const std::string& senderName,
			const std::string& message, uint32 channelId, GameTime nowMs);

		/// Reports that a real player was seen, e.g. a player unit near a bot that is not a bot.
		void NoteHumanSeen(GameTime nowMs);

		/// Reports that a real player is standing near this bot, which lets it talk in /say.
		void NoteHumanNearby(uint32 botIndex, GameTime nowMs);

		/// Reports that the bot killed something. Counted as a shared fight with every real
		/// player in its party at the time, so that the bot knows what it actually did with them.
		void NoteKill(uint32 botIndex, const std::string& victimName, GameTime nowMs);

		/// Remembers something that happened to a bot ("died to a Forest Wolf"), for it to talk about.
		void NoteEvent(uint32 botIndex, const std::string& event, GameTime nowMs);

		/// Advances everything: model results, pending decisions, whisper replies, unprompted
		/// posts and lines waiting to be "typed".
		void Update(GameTime nowMs);

		/// Whether a character name belongs to a bot of this swarm (ignoring case).
		[[nodiscard]] bool IsBotName(const std::string& name) const;

		[[nodiscard]] const ChatDirectorStats& GetStats() const { return m_stats; }

	private:
		struct Line
		{
			/// Speaker's name, or "you" for the bot itself.
			std::string speaker;
			std::string text;
		};

		struct Conversation
		{
			ChatType replyType { ChatType::Unknown };
			std::string target;
			std::string label;
			std::deque<Line> lines;
			bool inFlight { false };
			bool unanswered { false };
			GameTime lastHeardMs { 0 };
			GameTime windowStartMs { 0 };
			uint32 repliesInWindow { 0 };
		};

		struct Event
		{
			std::string text;
			GameTime atMs { 0 };
		};

		/// What a bot knows about one real player. The model invents shared history ("you were a
		/// beast that last pull") unless it is told exactly what the history is.
		struct Acquaintance
		{
			/// Name as the player spells it.
			std::string name;
			GameTime firstMetMs { 0 };
			GameTime lastTalkedMs { 0 };

			/// Since when they share a party; zero when they do not.
			GameTime groupedSinceMs { 0 };

			/// Times they were in a party together before the current one.
			uint32 pastGroups { 0 };

			/// Kills the bot made while grouped with them, over all groups.
			uint32 killsTogether { 0 };
		};

		struct BotState
		{
			uint32 index { 0 };
			IChatParticipant* participant { nullptr };
			ChatPersona persona;
			std::mt19937 rng;
			std::map<std::string, Conversation> conversations;
			std::deque<Event> events;
			GameTime nextProactiveMs { 0 };
			GameTime lastHumanGuildMs { 0 };
			GameTime lastHumanNearbyMs { 0 };
			GameTime nextSayMs { 0 };

			/// When this bot last invited someone, by lower-case name.
			std::map<std::string, GameTime> lastInviteMs;

			/// Real players this bot has dealt with, by lower-case name.
			std::map<std::string, Acquaintance> acquaintances;
			bool lastPostWasReply { false };
		};

		struct PendingDecision
		{
			ChatType type { ChatType::Unknown };
			std::string senderName;
			std::string message;
			std::string conversationKey;
			std::vector<uint32> receivers;
			GameTime createdMs { 0 };
		};

		struct InFlightRequest
		{
			uint32 botIndex { 0 };
			std::string conversationKey;
			double reservedUsd { 0.0 };
			bool isReply { true };

			/// Whom the model was told it may invite; empty if nobody.
			std::string inviteCandidate;
		};

		struct OutgoingLine
		{
			GameTime sendAtMs { 0 };
			uint32 botIndex { 0 };
			std::string conversationKey;

			/// Empty for an invite without words.
			std::string text;
			bool isReply { true };
			bool record { true };

			/// Invited right after the line is sent; empty for none.
			std::string inviteTarget;
		};

	private:
		[[nodiscard]] BotState* FindBot(uint32 index);
		[[nodiscard]] const BotState* FindBotByName(const std::string& name) const;

		/// Finds or creates the conversation a chat line belongs to. Returns null for chat types
		/// bots never answer.
		Conversation* GetConversation(BotState& bot, ChatType type, const std::string& senderName,
			uint32 channelId, std::string& outKey);

		void HandleResults(GameTime nowMs);
		void ResolveDecisions(GameTime nowMs);
		void ResolveDecision(const PendingDecision& decision, GameTime nowMs);
		void ServiceWhispers(GameTime nowMs);
		void ServiceProactive(BotState& bot, GameTime nowMs);

		/// Unprompted /say lines while a real player stands nearby.
		void ServiceProactiveSay(BotState& bot, GameTime nowMs);
		void FlushOutbox(GameTime nowMs);

		/// The person the bot may invite from this conversation, or empty: the partner of a
		/// whisper, the last player to speak in /say or a channel, if the bot is free to invite
		/// and has not invited them recently.
		[[nodiscard]] std::string FindInviteCandidate(const BotState& bot, const Conversation& conversation, GameTime nowMs) const;

		/// Finds or creates what the bot knows about a real player.
		Acquaintance& Acquaint(BotState& bot, const std::string& name, GameTime nowMs);

		/// Brings group membership in the acquaintances up to date with the bot's party.
		void TrackParty(BotState& bot, GameTime nowMs);

		/// "What you know about the people here" for the prompt: every real player in the
		/// conversation or the party, and nothing the bot did not actually do with them.
		[[nodiscard]] std::string DescribeAcquaintances(const BotState& bot, const Conversation& conversation, GameTime nowMs) const;

		/// Asks the model for the bot's next line in a conversation.
		/// @param topic For an unprompted post, the idea to build it on; empty for a reply.
		bool Request(BotState& bot, const std::string& conversationKey, const std::string& topic, GameTime nowMs);

		[[nodiscard]] std::string BuildSystemPrompt(const BotState& bot) const;
		[[nodiscard]] std::string BuildPrompt(const BotState& bot, const Conversation& conversation,
			const std::string& topic, const std::string& inviteCandidate, GameTime nowMs) const;

		[[nodiscard]] bool HumanActive(GameTime nowMs) const;
		[[nodiscard]] float Roll();

	private:
		ChatDirectorSettings m_settings;
		IChatModel& m_model;
		ChatBudget& m_budget;
		std::function<std::time_t()> m_wallClock;
		std::mt19937 m_rng;

		std::map<uint32, BotState> m_bots;
		std::set<std::string> m_botNamesLower;

		std::map<std::string, PendingDecision> m_pending;
		std::map<uint64, InFlightRequest> m_inFlight;
		std::vector<OutgoingLine> m_outbox;
		uint64 m_nextTicket { 0 };

		GameTime m_lastHumanSeenMs { 0 };
		bool m_humanEverSeen { false };
		GameTime m_lastGeneralReplyMs { 0 };
		GameTime m_lastGeneralProactiveMs { 0 };
		bool m_generalProactiveEver { false };
		GameTime m_lastProactiveSayMs { 0 };
		bool m_proactiveSayEver { false };

		/// After the budget says no, it is not asked again before this. Asking re-reads the
		/// ledger file, and a capped swarm would otherwise do that every frame.
		GameTime m_budgetRetryAtMs { 0 };

		ChatDirectorStats m_stats;
	};
}
