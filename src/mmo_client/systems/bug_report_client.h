// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "client_log_tail.h"
#include "subsystem_client.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "game/bug_report.h"
#include "net/realm_connector.h"

struct lua_State;

namespace mmo
{
	/// Files in-game bug reports.
	///
	/// The UI decides what a report is about; this class adds client diagnostics (version, OS,
	/// GPU, graphics settings, frame rate and the recent client log), compresses them and sends
	/// everything to the realm. The world node attaches the authoritative server state.
	///
	/// Script API: SubmitBugReport(subjectType, subjectId, subjectGuid, subjectName, comment) and
	/// the event BUG_REPORT_RESULT(result) with result ACCEPTED, RATE_LIMITED, TOO_LARGE, DISABLED
	/// or INVALID.
	class BugReportClient final : public NonCopyable
	{
	public:
		BugReportClient(RealmConnector& connector, const SubsystemClient& subsystems);

	public:
		/// Registers the packet handler.
		void Initialize();

		/// Unregisters the packet handler.
		void Shutdown();

		/// Registers the script API.
		void RegisterScriptFunctions(lua_State* lua);

		/// Files a report.
		/// @param subjectType "item", "spell", "creature", "quest", "aura", "object" or "generic".
		/// @param subjectId Entry id of the subject (0 for generic reports).
		/// @param subjectGuid Decimal guid of the subject instance, "0" if none.
		/// @param subjectName Display name of the subject.
		/// @param comment The player's description, 1 to 1000 characters.
		/// @returns false if the report was not sent (unavailable, invalid input).
		bool Submit(const String& subjectType, uint32 subjectId, const String& subjectGuid, const String& subjectName, const String& comment);

		/// Builds the client diagnostics JSON sent with every report.
		[[nodiscard]] String BuildClientJson() const;

	private:
		PacketParseResult OnBugReportResult(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		const SubsystemClient& m_subsystems;
		RealmConnector::PacketHandlerHandleContainer m_handlers;
		ClientLogTail m_logTail;
		scoped_connection m_logConnection;
	};
}
