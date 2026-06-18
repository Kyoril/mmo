// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_combat_script.h"

#include <map>
#include <memory>
#include <functional>
#include <string>

namespace mmo
{
	class CreatureAICombatState;

	/**
	 * @brief Registry for creature combat scripts.
	 *
	 * Maps script names (strings) to factory functions that create combat scripts.
	 * Creatures reference scripts by name via the script_name field in proto data.
	 * When a creature enters combat, the combat state queries this registry
	 * using the creature's script_name to create the appropriate script instance.
	 *
	 * Usage:
	 * @code
	 * // Register a script with a unique name
	 * CreatureCombatScriptRegistry::Instance().RegisterScript("training_dummy",
	 *     [](CreatureAICombatState& state) {
	 *         return std::make_unique<TrainingDummyCombatScript>(state);
	 *     });
	 *
	 * // Or use the convenience helper:
	 * CreatureCombatScriptRegistry::Instance().RegisterScript<TrainingDummyCombatScript>("training_dummy");
	 * @endcode
	 */
	class CreatureCombatScriptRegistry final
	{
	public:

		/**
		 * @brief Gets the singleton instance.
		 * @return Reference to the global registry instance.
		 */
		static CreatureCombatScriptRegistry& Instance();

		/**
		 * @brief Registers a combat script factory with a unique name.
		 * @param scriptName The unique script name.
		 * @param factory Factory function that creates the script.
		 */
		void RegisterScript(const std::string& scriptName, CombatScriptFactory factory);

		/**
		 * @brief Convenience: registers a script type with a unique name.
		 * @tparam T The script class (must derive from CreatureCombatScript).
		 * @param scriptName The unique script name.
		 */
		template <typename T>
		void RegisterScript(const std::string& scriptName)
		{
			RegisterScript(scriptName, [](CreatureAICombatState& state)
			{
				return std::make_unique<T>(state);
			});
		}

		/**
		 * @brief Creates a combat script by name, if registered.
		 * @param scriptName The script name to look up.
		 * @param combatState The combat state that will own the script.
		 * @return The created script, or nullptr if no script is registered.
		 */
		std::unique_ptr<CreatureCombatScript> CreateScript(
			const std::string& scriptName,
			CreatureAICombatState& combatState) const;

		/**
		 * @brief Checks if a script is registered with the given name.
		 * @param scriptName The script name.
		 * @return True if a script is registered.
		 */
		bool HasScript(const std::string& scriptName) const;

		/**
		 * @brief Removes a registered script.
		 * @param scriptName The script name to unregister.
		 */
		void UnregisterScript(const std::string& scriptName);

		/**
		 * @brief Removes all registered scripts.
		 */
		void Clear();

		/**
		 * @brief Registers all built-in combat scripts.
		 *
		 * Called automatically by Instance() on first access. This ensures
		 * all built-in scripts are registered reliably, avoiding static
		 * library linker issues with self-registration macros.
		 */
		void RegisterBuiltinScripts();

	private:

		/// Private constructor (singleton).
		CreatureCombatScriptRegistry() = default;

		/// Map of script names to script factories.
		std::map<std::string, CombatScriptFactory> m_scripts;
	};

	/**
	 * @brief Helper macro for auto-registering combat scripts at static init time.
	 *
	 * Usage at file scope:
	 * @code
	 * REGISTER_COMBAT_SCRIPT(MyBossScript, "my_boss");
	 * @endcode
	 */
#define REGISTER_COMBAT_SCRIPT(ScriptClass, ScriptName) \
	namespace \
	{ \
		static const bool s_##ScriptClass##_registered = []() \
		{ \
			::mmo::CreatureCombatScriptRegistry::Instance().RegisterScript<ScriptClass>(ScriptName); \
			return true; \
		}(); \
	}
}
