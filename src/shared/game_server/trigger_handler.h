// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

#include "objects/game_unit_s.h"

namespace mmo
{
	namespace proto
	{
		class TriggerEntry;
	}

	class WorldInstance;

	struct TriggerContext
	{
		/// Owner of the trigger or nullptr if none (e.g. for map-instance-global triggers).
		GameObjectS* owner;

		/// World instance this trigger executes in. This is always set when known, even for
		/// ownerless (instance-global) triggers, so that named targets and spawners can be resolved.
		WorldInstance* world;

		/// Id of the spell that triggered this trigger with it's hit.
		uint32 spellHitId;

		/// Unit that raised this trigger by raising an event or nullptr if not suitable for this trigger event.
		std::weak_ptr<GameUnitS> triggeringUnit;

		/// @param owner_ The owning object of the trigger or nullptr for instance-global triggers.
		/// @param triggering The unit which raised the event causing this trigger or nullptr.
		/// @param world_ Explicit world instance. If nullptr, it is derived from owner_ when possible.
		explicit TriggerContext(GameObjectS* owner_, GameUnitS* triggering, WorldInstance* world_ = nullptr);
	};

	/// Interface for trigger handlers.
	class ITriggerHandler : public NonCopyable
	{
	public:
		explicit ITriggerHandler() = default;
		virtual ~ITriggerHandler() override = default;

		/// Executes a unit trigger.
		/// @param entry The trigger to execute.
		/// @param actionOffset The action to execute.
		/// @param owner The executing owner. TODO: Replace by context object
		virtual void ExecuteTrigger(const proto::TriggerEntry& entry, TriggerContext context, uint32 actionOffset = 0, bool ignoreProbability = false) = 0;
	};
}
