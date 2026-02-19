// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_combat_script_registry.h"
#include "creature_ai_combat_state.h"
#include "log/default_log_levels.h"

// Built-in combat scripts
#include "scripts/training_dummy_combat_script.h"
#include "scripts/example_dungeon_boss_script.h"

namespace mmo
{
	CreatureCombatScriptRegistry& CreatureCombatScriptRegistry::Instance()
	{
		static CreatureCombatScriptRegistry instance;
		static bool initialized = false;
		if (!initialized)
		{
			initialized = true;
			instance.RegisterBuiltinScripts();
		}
		return instance;
	}

	void CreatureCombatScriptRegistry::RegisterScript(const std::string& scriptName, CombatScriptFactory factory)
	{
		if (m_scripts.contains(scriptName))
		{
			WLOG("CombatScriptRegistry: Overwriting script '" << scriptName << "'");
		}

		m_scripts[scriptName] = std::move(factory);
		DLOG("CombatScriptRegistry: Registered script '" << scriptName << "'");
	}

	std::unique_ptr<CreatureCombatScript> CreatureCombatScriptRegistry::CreateScript(
		const std::string& scriptName,
		CreatureAICombatState& combatState) const
	{
		const auto it = m_scripts.find(scriptName);
		if (it != m_scripts.end())
		{
			return it->second(combatState);
		}

		return nullptr;
	}

	bool CreatureCombatScriptRegistry::HasScript(const std::string& scriptName) const
	{
		return m_scripts.contains(scriptName);
	}

	void CreatureCombatScriptRegistry::UnregisterScript(const std::string& scriptName)
	{
		m_scripts.erase(scriptName);
	}

	void CreatureCombatScriptRegistry::Clear()
	{
		m_scripts.clear();
	}

	void CreatureCombatScriptRegistry::RegisterBuiltinScripts()
	{
		RegisterScript<TrainingDummyCombatScript>("training_dummy");
		RegisterScript<ExampleDungeonBossScript>("example_dungeon_boss");
	}
}
