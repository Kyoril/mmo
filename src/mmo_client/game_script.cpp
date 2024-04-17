// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_script.h"
#include "console/console.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "game_states/game_state_mgr.h"
#include "game_states/world_state.h"
#include "game_states/login_state.h"
#include "console/console_var.h"

#include <string>
#include <functional>
#include <utility>

#include "object_mgr.h"
#include "game/game_player_c.h"
#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "shared/client_data/spells.pb.h"
#include "shared/proto_data/spells.pb.h"


namespace mmo
{
	CharacterView s_selectedCharacter;

	// Static script methods
	namespace
	{
		/// Allows executing a console command from within lua.
		void Script_RunConsoleCommand(const char* cmdLine)
		{
			ASSERT(cmdLine);
			Console::ExecuteCommand(cmdLine);
		}
		
		const mmo::RealmData* Script_GetRealmData(const LoginConnector& connector, const int32 index)
		{
			const auto& realms = connector.GetRealms();
			if (index < 0 || index >= realms.size())
			{
				ELOG("GetRealm: Invalid realm index provided (" << index << ")");
				return nullptr;
			}

			return &realms[index];
		}

		const char* Script_GetConsoleVar(const std::string& name)
		{
			ConsoleVar* cvar = ConsoleVarMgr::FindConsoleVar(name);
			if (!cvar)
			{
				return nullptr;
			}

			return cvar->GetStringValue().c_str();
		}

		void Script_EnterWorld(const CharacterView& characterView)
		{
			s_selectedCharacter = characterView;

			GameStateMgr::Get().SetGameState(WorldState::Name);
		}
		
		void Script_Print(const std::string& text)
		{
			ILOG(text);
		}

		std::shared_ptr<GameUnitC> Script_GetUnitByName(const std::string& unitName)
		{
			if (unitName == "player")
			{
				return ObjectMgr::GetActivePlayer();
			}

			if (unitName == "target")
			{
				const auto playerObject = ObjectMgr::GetActivePlayer();
				if (playerObject)
				{
					return ObjectMgr::Get<GameUnitC>(playerObject->Get<uint64>(object_fields::TargetUnit));
				}
			}

			return nullptr;
		}

		const proto_client::SpellEntry* Script_GetSpell(uint32 index)
		{
			const GameUnitC* player = dynamic_cast<GameUnitC*>(ObjectMgr::GetActivePlayer().get());
			if (!player)
			{
				return nullptr;
			}

			return player->GetSpell(index);
		}


		void Script_CastSpell(uint32 index)
		{
			const auto* spell = Script_GetSpell(index);
			if (spell == nullptr)
			{
				WLOG("Unknown spell for index " << index);
				return;
			}

			// TODO: Make this more efficient than just executing a console command!
			Console::ExecuteCommand("cast " + std::to_string(spell->id()));
		}

		bool Script_UnitExists(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return true;
			}

			return false;
		}

		int32 Script_UnitHealth(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::Health);
			}

			return 0;
		}

		int32 Script_UnitHealthMax(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::MaxHealth);
			}

			return 1;
		}

		int32 Script_UnitMana(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::Mana);
			}

			return 0;
		}

		int32 Script_UnitManaMax(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::MaxMana);
			}

			return 1;
		}

		int32 Script_UnitLevel(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::Level);
			}

			return 1;
		}

		int32 Script_PlayerXp()
		{
			if (auto unit = Script_GetUnitByName("player"))
			{
				return unit->Get<int32>(object_fields::Xp);
			}

			return 0;
		}

		int32 Script_PlayerNextLevelXp()
		{
			if (auto unit = Script_GetUnitByName("player"))
			{
				return unit->Get<int32>(object_fields::NextLevelXp);
			}

			return 0;
		}

		std::string Script_UnitName(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return "UNIT";
			}

			return "Unknown";
		}

		void Spell_GetEffectPoints(const proto_client::SpellEntry& spell, int effectIndex, int& min, int& max)
		{
			if (effectIndex < 0 || effectIndex >= spell.effects_size())
			{
				min = 0;
				max = 0;
				return;
			}

			const auto& effect = spell.effects(effectIndex);
			min = effect.basepoints();
			max = effect.basepoints() + effect.diesides();
		}

		std::string Script_GetSpellDescription(const proto_client::SpellEntry* spell)
		{
			if (spell == nullptr)
			{
				return "<NULL>";
			}

			std::ostringstream strm;

			int min = 0, max = 0, effectIndex = 0;

			const String& desc = spell->description();
			for (int i = 0; i < desc.size(); ++i)
			{
				if (desc[i] == '$' && i < desc.size() - 1)
				{
					i++;

					char token = desc[i];
					switch(token)
					{
					case 'd':
					case 'D':
						strm << std::setprecision(2) << static_cast<float>(spell->duration()) / 1000.0f;
						break;

					case 'm':
						if (i < desc.size() - 1 && desc[i + 1] != ' ') effectIndex = desc[i+1] - '0';
						Spell_GetEffectPoints(*spell, effectIndex, min, max);
						strm << min;
						break;

					case 'M':
						if (i < desc.size() - 1 && desc[i + 1] != ' ') effectIndex = desc[i + 1] - '0';
						Spell_GetEffectPoints(*spell, effectIndex, min, max);
						strm << max;
						break;

					case 's':
					case 'S':
						if (i < desc.size() - 1 && desc[i + 1] != ' ') effectIndex = desc[i + 1] - '0';
						Spell_GetEffectPoints(*spell, effectIndex, min, max);
						if (min == max)
						{
							strm << min;
						}
						else
						{
							strm << min << " - " << max;
						}
						break;
					}

					// Skip everything after token until a space is found or string ends
					while(i < desc.size() - 1 && desc[i] != ' ')
					{
						i++;
					}

					strm << " ";
				}
				else
				{
					strm << desc[i];
				}
			}

			return strm.str();
		}
	}


	GameScript::GameScript(LoginConnector& loginConnector, RealmConnector& realmConnector, std::shared_ptr<LoginState> loginState, const proto_client::Project& project)
		: m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_loginState(std::move(loginState))
		, m_project(project)
	{
		// Initialize the lua state instance
		m_luaState = LuaStatePtr(luaL_newstate());

		luaL_openlibs(m_luaState.get());
		
		// Initialize luabind
		luabind::open(m_luaState.get());

		// Register custom global functions to the lua state instance
		RegisterGlobalFunctions();
	}

	void GameScript::RegisterGlobalFunctions()
	{
		// Check for double initialization
		ASSERT(!m_globalFunctionsRegistered);

		// Register common functions
		luabind::module(m_luaState.get())
		[
		luabind::scope(
			luabind::class_<mmo::RealmData>("RealmData")
			.def_readonly("id", &mmo::RealmData::id)
			.def_readonly("name", &mmo::RealmData::name)),

			luabind::scope(
				luabind::class_<mmo::CharacterView>("CharacterView")
				.def_readonly("guid", &mmo::CharacterView::GetGuid)
				.def_readonly("name", &mmo::CharacterView::GetName)
				.def_readonly("level", &mmo::CharacterView::GetLevel)),

			luabind::scope(
				luabind::class_<LoginConnector>("LoginConnector")
				.def("GetRealms", &LoginConnector::GetRealms, luabind::return_stl_iterator())
				.def("IsConnected", &LoginConnector::IsConnected)),

			luabind::scope(
				luabind::class_<proto_client::Project>("Project")
				.def_readonly("spells", &mmo::proto_client::Project::spells)),

			luabind::scope(
				luabind::class_<proto_client::SpellManager>("SpellManager")
				.def_const<const proto_client::SpellEntry*, proto_client::SpellManager, uint32>("GetById", &mmo::proto_client::SpellManager::getById)),

			luabind::scope(
				luabind::class_<RealmConnector>("RealmConnector")
	               .def("ConnectToRealm", &RealmConnector::ConnectToRealm)
					.def("IsConnected", &RealmConnector::IsConnected)
	               .def("GetCharViews", &RealmConnector::GetCharacterViews, luabind::return_stl_iterator())
	               .def("GetRealmName", &RealmConnector::GetRealmName)
	               .def("CreateCharacter", &RealmConnector::CreateCharacter)
	               .def("DeleteCharacter", &RealmConnector::DeleteCharacter)),

			luabind::scope(
				luabind::class_<LoginState>("LoginState")
					.def("EnterWorld", &LoginState::EnterWorld)),

			luabind::scope(
				luabind::class_<proto_client::SpellEntry>("Spell")
				.def_readonly("id", &proto_client::SpellEntry::id)
				.def_readonly("name", &proto_client::SpellEntry::name)
				.def_readonly("description", &proto_client::SpellEntry::description)
				.def_readonly("cost", &proto_client::SpellEntry::cost)
				.def_readonly("cooldown", &proto_client::SpellEntry::cooldown)
				.def_readonly("level", &proto_client::SpellEntry::spelllevel)
				.def_readonly("casttime", &proto_client::SpellEntry::casttime)
				.def_readonly("icon", &proto_client::SpellEntry::icon)),

			luabind::def("RunConsoleCommand", &Script_RunConsoleCommand),
			luabind::def("GetCVar", &Script_GetConsoleVar),
			luabind::def("EnterWorld", &Script_EnterWorld),
			luabind::def("print", &Script_Print),

			luabind::def("UnitExists", &Script_UnitExists),
			luabind::def("UnitHealth", &Script_UnitHealth),
			luabind::def("UnitHealthMax", &Script_UnitHealthMax),
			luabind::def("UnitMana", &Script_UnitMana),
			luabind::def("UnitManaMax", &Script_UnitManaMax),
			luabind::def("UnitLevel", &Script_UnitLevel),
			luabind::def("UnitName", &Script_UnitName),
			luabind::def("PlayerXp", &Script_PlayerXp),
			luabind::def("PlayerNextLevelXp", &Script_PlayerNextLevelXp),

			luabind::def("GetSpell", &Script_GetSpell),
			luabind::def("CastSpell", &Script_CastSpell),

			luabind::def("GetSpellDescription", &Script_GetSpellDescription)
		];

		luabind::globals(m_luaState.get())["loginConnector"] = &m_loginConnector;
		luabind::globals(m_luaState.get())["realmConnector"] = &m_realmConnector;
		luabind::globals(m_luaState.get())["loginState"] = m_loginState.get();

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}


}
