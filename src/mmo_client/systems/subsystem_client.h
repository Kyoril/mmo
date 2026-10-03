// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "game/subsystem.h"
#include "net/realm_connector.h"

#include <array>

struct lua_State;

namespace mmo
{
	/// Tracks which server subsystems are available, as announced by the realm.
	///
	/// The realm sends the full list after entering the world and changed entries afterwards; both
	/// overwrite what is known. Anything not announced yet counts as unavailable (fail closed), so
	/// UI built on a subsystem stays hidden until the server confirms it.
	///
	/// Script API: IsSubsystemAvailable(name), GetSubsystemStatus(name) and the event
	/// SUBSYSTEM_STATUS_CHANGED(name, available), fired once per changed entry.
	class SubsystemClient final : public NonCopyable
	{
	public:
		explicit SubsystemClient(RealmConnector& connector);

	public:
		/// Registers the packet handler.
		void Initialize();

		/// Unregisters the packet handler and forgets every status.
		void Shutdown();

		/// Registers the script API.
		void RegisterScriptFunctions(lua_State* lua);

		/// Whether the subsystem is known to be available.
		[[nodiscard]] bool IsAvailable(game::Subsystem subsystem) const;

		/// Whether the subsystem with the given script name is known to be available.
		[[nodiscard]] bool IsAvailable(const String& name) const;

	private:
		PacketParseResult OnSubsystemStatus(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		RealmConnector::PacketHandlerHandleContainer m_handlers;
		std::array<game::SubsystemStatus, game::subsystem::Count_> m_status;
	};
}
