#pragma once

#include <memory>

#include "game_unit_s.h"

namespace mmo
{
	namespace proto
	{
		class TriggerEntry;
	}

	struct TriggerContext
	{
		/// Owner of the trigger or nullptr if none.
		GameObjectS* owner;

		/// Id of the spell that triggered this trigger with it's hit.
		uint32 spellHitId;

		/// Unit that raised this trigger by raising an event or nullptr if not suitable for this trigger event.
		std::weak_ptr<GameUnitS> triggeringUnit;

		explicit TriggerContext(GameObjectS* owner_, GameUnitS* triggering);
	};


	class ITriggerHandler : public NonCopyable
	{
	public:
		ITriggerHandler() {};
		virtual ~ITriggerHandler() {};

		/// Executes a unit trigger.
		/// @param entry The trigger to execute.
		/// @param actionOffset The action to execute.
		/// @param owner The executing owner. TODO: Replace by context object
		virtual void ExecuteTrigger(const proto::TriggerEntry& entry, TriggerContext context, uint32 actionOffset = 0, bool ignoreProbability = false) = 0;
	};
}
