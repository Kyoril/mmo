// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "frame_ui/color.h"
#include "math/vector3.h"
#include "net/realm_connector.h"
#include "ui/world_text_frame.h"

namespace mmo
{
	class CombatSoundPlayer;
	class PlayerController;

	/// @brief Shows the in-world feedback for combat log events.
	class ICombatLogListener
	{
	public:
		virtual ~ICombatLogListener() = default;

	public:
		/// @brief Shows floating combat text at a world position.
		virtual void AddWorldTextFrame(const Vector3 &position, const String &text, const Color &color, float duration, WorldTextAnimation animation = WorldTextAnimation::Normal) = 0;

		/// @brief Shakes the camera when the local player takes damage or lands a critical hit.
		virtual void TriggerCombatCameraShake(ObjectGuid victimGuid, ObjectGuid attackerGuid, uint32 amount, bool isCritical) = 0;
	};

	/// @brief Handles the server's combat log packets (melee swings, spell and periodic damage,
	///        heals, energizes, environmental damage and experience gains): floating combat text,
	///        swing and hit sounds, attack animations, camera shake and the DAMAGE_DONE Lua event.
	class CombatLogClient final : public NonCopyable
	{
	public:
		explicit CombatLogClient(RealmConnector &connector, const proto_client::Project &project, CombatSoundPlayer &combatSoundPlayer);

	public:
		/// @brief Registers the packet handlers. The player controller must outlive Shutdown.
		void Initialize(PlayerController &playerController, ICombatLogListener &listener);

		/// @brief Unregisters the packet handlers.
		void Shutdown();

	private:
		PacketParseResult OnXpLog(game::IncomingPacket &packet);

		PacketParseResult OnSpellDamageLog(game::IncomingPacket &packet);

		PacketParseResult OnNonSpellDamageLog(game::IncomingPacket &packet);

		PacketParseResult OnAttackerStateUpdate(game::IncomingPacket &packet);

		PacketParseResult OnLogEnvironmentalDamage(game::IncomingPacket &packet);

		PacketParseResult OnPeriodicAuraLog(game::IncomingPacket &packet);

		PacketParseResult OnSpellEnergizeLog(game::IncomingPacket &packet);

		PacketParseResult OnSpellHealLog(game::IncomingPacket &packet);

	private:
		RealmConnector &m_connector;
		const proto_client::Project &m_project;
		CombatSoundPlayer &m_combatSoundPlayer;
		RealmConnector::PacketHandlerHandleContainer m_handlers;
		PlayerController *m_playerController = nullptr;
		ICombatLogListener *m_listener = nullptr;
	};
}
