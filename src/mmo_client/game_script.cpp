// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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

#include "cursor.h"
#include "game/item.h"
#include "game/spell.h"
#include "game_client/game_item_c.h"
#include "game_client/object_mgr.h"
#include "game_client/game_player_c.h"
#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/out_value_policy.hpp"
#include "shared/client_data/spells.pb.h"
#include "shared/proto_data/spells.pb.h"



namespace luabind
{
	// A little helper to combine multiple policies
	template <typename... T>
 	using joined = typename luabind::meta::join<T...>::type;
}

namespace mmo
{
	CharacterView s_selectedCharacter;

	Cursor g_cursor;

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

		int32 Script_UnitPower(const std::string& unitName, int32 powerType)
		{
			if (powerType < 0 || powerType > power_type::Energy)
			{
				return -1;
			}

			if (auto unit = Script_GetUnitByName(unitName))
			{
				const int32 power = unit->Get<int32>(object_fields::Mana + powerType);
				return power;
			}

			return -1;
		}

		int32 Script_UnitPowerMax(const std::string& unitName, int32 powerType)
		{
			if (powerType < 0 || powerType > power_type::Energy)
			{
				return -1;
			}

			if (auto unit = Script_GetUnitByName(unitName))
			{
				const int32 maxPower = unit->Get<int32>(object_fields::MaxMana + powerType);
				return maxPower;
			}

			return -1;
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

		int32 Script_GetBackpackSlot(int32 slotId)
		{
			// Invalid backpack slot
			if (slotId < 0 || slotId >= player_inventory_pack_slots::End - player_inventory_pack_slots::Start)
			{
				return -1;
			}

			return (static_cast<uint16>(player_inventory_slots::Bag_0) << 8) | static_cast<uint16>(player_inventory_pack_slots::Start + slotId);
		}

		bool Script_IsBackpackSlot(int32 slotId)
		{
			return static_cast<uint16>(slotId >> 8) == player_inventory_slots::Bag_0 &&
				static_cast<uint16>(slotId & 0xFF) >= player_inventory_pack_slots::Start &&
				static_cast<uint16>(slotId & 0xFF) <= player_inventory_pack_slots::End;
		}

		int32 Script_GetBagSlot(int32 bagIndex, int32 slotId)
		{
			// Only 4 bags are supported
			if (bagIndex < 1 || bagIndex >= player_inventory_slots::End - player_inventory_slots::Start)
			{
				return -1;
			}

			// Maximum number of slots per bag is 36
			if (slotId < 0 || slotId >= 36)
			{
				return -1;
			}

			// Bag index starts at 1, so we need to subtract 1
			return (static_cast<uint16>(player_inventory_slots::Start + bagIndex - 1) << 8) | static_cast<uint16>(slotId);
		}

		std::shared_ptr<GameItemC> GetItemFromSlot(const char* unitName, int32 slotId)
		{
			if (const auto unit = Script_GetUnitByName(unitName))
			{
				if (unit->GetTypeId() != ObjectTypeId::Player)
				{
					return nullptr;
				}

				// Backpack?
				uint64 itemGuid = 0;
				if ((static_cast<uint16>(slotId) >> 8) == player_inventory_slots::Bag_0)
				{
					const uint8 slotFieldOffset = (static_cast<uint8>(slotId & 0xFF) - player_inventory_slots::End) * 2;
					itemGuid = unit->Get<uint64>(object_fields::PackSlot_1 + slotFieldOffset);
				}



				if (itemGuid == 0)
				{
					return nullptr;
				}

				// Get item at the specified slot
				std::shared_ptr<GameItemC> item = ObjectMgr::Get<GameItemC>(itemGuid);
				return item;
			}

			return nullptr;
		}

		const ItemInfo* Script_GetInventorySlotItem(const char* unitName, int32 slotId)
		{
			std::shared_ptr<GameItemC> item = GetItemFromSlot(unitName, slotId);
			if (!item)
			{
				return nullptr;
			}

			return item->GetEntry();
		}

		const char* Script_GetInventorySlotIcon(const char* unitName, int32 slotId)
		{
			static const String s_defaultItemIcon = "Interface\\Icons\\Spells\\S_Attack.htex";

			std::shared_ptr<GameItemC> item = GetItemFromSlot(unitName, slotId);
			if (!item)
			{
				return nullptr;
			}

			const ItemInfo* itemEntry = item->GetEntry();
			if (itemEntry && !itemEntry->icon.empty())
			{
				return itemEntry->icon.c_str();
			}

			return s_defaultItemIcon.c_str();
		}

		int32 Script_GetInventorySlotCount(const char* unitName, int32 slotId)
		{
			std::shared_ptr<GameItemC> item = GetItemFromSlot(unitName, slotId);
			if (!item)
			{
				return -1;
			}

			return item->Get<uint32>(object_fields::StackCount);
		}

		int32 Script_GetInventorySlotQuality(const char* unitName, int32 slotId)
		{
			std::shared_ptr<GameItemC> item = GetItemFromSlot(unitName, slotId);
			if (!item)
			{
				return -1;
			}

			const auto* itemEntry = item->GetEntry();
			return itemEntry ? itemEntry->quality : -1;
		}

		int32 Script_PlayerXp()
		{
			if (auto unit = Script_GetUnitByName("player"))
			{
				return unit->Get<int32>(object_fields::Xp);
			}

			return 0;
		}

		int32 Script_GetPlayerAura(int32 id)
		{
			// TODO: Get buff index

			return -1;
		}

		int32 Script_UnitPowerType(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<int32>(object_fields::PowerType);
			}

			return -1;
		}

		void Script_UnitStat(const std::string& unitName, uint32 statId, int32& out_base, int32& out_modifier)
		{
			out_base = -1;
			out_modifier = -1;

			if (statId >= 5)
			{
				return;
			}

			if (auto unit = Script_GetUnitByName(unitName))
			{
				const int32 bonus = unit->Get<int32>(object_fields::PosStatStamina + statId) - unit->Get<int32>(object_fields::NegStatStamina + statId);
				out_base = unit->Get<int32>(object_fields::StatStamina + statId) - bonus;
				out_modifier = bonus;
			}
		}

		void Script_UnitArmor(const std::string& unitName, int32& out_base, int32& out_modifier)
		{
			out_base = -1;
			out_modifier = -1;

			if (auto unit = Script_GetUnitByName(unitName))
			{
				const int32 bonus = unit->Get<int32>(object_fields::PosStatArmor) - unit->Get<int32>(object_fields::NegStatArmor);
				out_base = unit->Get<int32>(object_fields::Armor) - bonus;
				out_modifier = bonus;
			}
		}

		int32 Script_UnitMoney(const std::string& unitName)
		{
			if (std::shared_ptr<GameUnitC> unit = nullptr; (unit = Script_GetUnitByName(unitName)) != nullptr && unit->GetTypeId() == ObjectTypeId::Player)
			{
				return unit->Get<int32>(object_fields::Money);
			}

			return 0;
		}

		int32 Script_PlayerNextLevelXp()
		{
			if (auto unit = Script_GetUnitByName("player"))
			{
				if (unit->Get<int32>(object_fields::Level) >= unit->Get<int32>(object_fields::MaxLevel))
				{
					return 0;
				}

				return unit->Get<int32>(object_fields::NextLevelXp);
			}

			return 0;
		}

		std::string Script_UnitName(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->GetName();
			}

			return "Unknown";
		}

		void Script_ReviveMe()
		{
			
		}

		void Script_MoveForwardStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::MoveForwardKey, true);
		}

		void Script_MoveForwardStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::MoveForwardKey, false);
		}

		void Script_MoveBackwardStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::MoveBackwardKey, true);
		}

		void Script_MoveBackwardStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::MoveBackwardKey, false);
		}

		void Script_TurnLeftStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::TurnLeftKey, true);
		}

		void Script_TurnLeftStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::TurnLeftKey, false);
		}

		void Script_TurnRightStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::TurnRightKey, true);
		}

		void Script_TurnRightStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::TurnRightKey, false);
		}

		void Script_StrafeLeftStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::StrafeLeftKey, true);
		}

		void Script_StrafeLeftStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::StrafeLeftKey, false);
		}

		void Script_StrafeRightStart()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::StrafeRightKey, true);
		}

		void Script_StrafeRightStop()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->SetControlBit(ControlFlags::StrafeRightKey, false);
		}

		void CalculateEffectBasePoints(const proto_client::SpellEffect& effect, const proto_client::SpellEntry& spell, int32 casterLevel, int32& minBasePoints, int32& maxBasePoints)
		{
			if (casterLevel > spell.maxlevel() && spell.maxlevel() > 0)
			{
				casterLevel = spell.maxlevel();
			}
			else if (casterLevel < spell.baselevel())
			{
				casterLevel = spell.baselevel();
			}
			casterLevel -= spell.spelllevel();

			// Calculate the damage done
			const float basePointsPerLevel = effect.pointsperlevel();
			const float randomPointsPerLevel = effect.diceperlevel();
			const int32 basePoints = effect.basepoints() + casterLevel * basePointsPerLevel;
			const int32 randomPoints = effect.diesides() + casterLevel * randomPointsPerLevel;

			minBasePoints = basePoints + effect.basedice();
			maxBasePoints = basePoints + randomPoints;
		}

		void Spell_GetEffectPoints(const proto_client::SpellEntry& spell, int32 level, int effectIndex, int& min, int& max)
		{
			min = 0;
			max = 0;

			if (effectIndex < 0 || effectIndex >= spell.effects_size())
			{
				return;
			}

			if (effectIndex >= spell.effects_size())
			{
				return;
			}

			const auto& effect = spell.effects(effectIndex);

			int32 minPoints = 0, maxPoints = 0;
			CalculateEffectBasePoints(effect, spell, level, minPoints, maxPoints);

			min = std::abs(minPoints);
			max = std::abs(maxPoints);
		}

		std::string Script_GetSpellDescription(const proto_client::SpellEntry* spell)
		{
			if (spell == nullptr)
			{
				return "<NULL>";
			}

			int32 level = Script_UnitLevel("player");

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
						{
							// Default display value is seconds
							double displayValue = static_cast<double>(spell->duration()) / 1000.0;
							String formatTemplate = "FORMAT_DURATION_SECONDS";

							if (spell->duration() >= 60000 * 60)
							{
								// Hour display, each hour has 60 minutes * 60 seconds = 3600 seconds
								displayValue /= 3600.0;
								formatTemplate = "FORMAT_DURATION_HOURS";
							}
							else if (spell->duration() >= 60000)
							{
								displayValue /= 60.0;
								formatTemplate = "FORMAT_DURATION_MINUTES";
							}

							if (token == 'd')
							{
								formatTemplate += "_PRECISE";
							}

							auto* format = FrameManager::Get().GetLocalization().FindStringById(formatTemplate);
							if (format)
							{
								char buffer[128];
								sprintf_s(buffer, format->c_str(), displayValue);
								strm << buffer;
							}
							else
							{
								strm << formatTemplate;
							}
						}
						break;

					case 'm':
						if (i < desc.size() - 1 && desc[i + 1] != ' ')
						{
							effectIndex = desc[i + 1] - '0';
							i++;
						}

						Spell_GetEffectPoints(*spell, level, effectIndex, min, max);
						strm << min;
						break;

					case 'M':
						if (i < desc.size() - 1 && desc[i + 1] != ' ')
						{
							effectIndex = desc[i + 1] - '0';
							i++;
						}
						Spell_GetEffectPoints(*spell, level, effectIndex, min, max);
						strm << max;
						break;

					case 's':
					case 'S':
						if (i < desc.size() - 1 && desc[i + 1] != ' ')
						{
							effectIndex = desc[i + 1] - '0';
							i++;
						}

						Spell_GetEffectPoints(*spell, level, effectIndex, min, max);
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
				}
				else
				{
					strm << desc[i];
				}
			}

			return strm.str();
		}

		const char* Script_GetItemClass(ItemInfo* self)
		{
			static const char* s_itemClassStrings[] = {
				"CONSUMABLE",
				"CONTAINER",
				"WEAPON",
				"GEM",
				"ARMOR",
				"REAGENT",
				"PROJECTILE",
				"TRADEGOODS",
				"GENERIC",
				"RECIPE",
				"MONEY",
				"QUIVER",
				"QUEST",
				"KEY",
				"PERMANENT",
				"JUNK"
			};

			static_assert(std::size(s_itemClassStrings) == item_class::Count_, "Item class strings array size mismatch");

			if(!self)
			{
				return nullptr;
			}

			return s_itemClassStrings[self->itemClass];
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
				.def_readonly("level", &mmo::CharacterView::GetLevel)
				.def_readonly("displayId", &mmo::CharacterView::GetDisplayId)
				.def_readonly("dead", &mmo::CharacterView::IsDead)
				.def_readonly("raceId", &mmo::CharacterView::GetRaceId)
				.def_readonly("classId", &mmo::CharacterView::GetClassId)
				.def_readonly("map", &mmo::CharacterView::GetMapId)),

			luabind::scope(
				luabind::class_<LoginConnector>("LoginConnector")
				.def("GetRealms", &LoginConnector::GetRealms, luabind::return_stl_iterator())
				.def("IsConnected", &LoginConnector::IsConnected)),

			luabind::scope(
				luabind::class_<proto_client::Project>("Project")
				.def_readonly("spells", &mmo::proto_client::Project::spells)
				.def_readonly("models", &mmo::proto_client::Project::models)),

			luabind::scope(
				luabind::class_<proto_client::SpellManager>("SpellManager")
				.def_const<const proto_client::SpellEntry*, proto_client::SpellManager, uint32>("GetById", &mmo::proto_client::SpellManager::getById)),

			luabind::scope(
				luabind::class_<proto_client::ModelDataManager>("ModelDataManager")
				.def_const<const proto_client::ModelDataEntry*, proto_client::ModelDataManager, uint32>("GetById", &mmo::proto_client::ModelDataManager::getById)),

			luabind::scope(
				luabind::class_<proto_client::ModelDataEntry>("ModelData")
				.def_readonly("id", &proto_client::ModelDataEntry::id)
				.def_readonly("name", &proto_client::ModelDataEntry::name)
				.def_readonly("filename", &proto_client::ModelDataEntry::filename)),

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
				luabind::class_<ItemInfo>("Item")
				.def_readonly("id", &ItemInfo::id)
				.def_readonly("name", &ItemInfo::name)
				.def_readonly("description", &ItemInfo::description)
				.def_readonly("quality", &ItemInfo::quality)
				.def("class", &Script_GetItemClass)
				.def_readonly("subclass", &ItemInfo::itemSubclass)
				.def_readonly("icon", &ItemInfo::icon)),

			luabind::scope(
				luabind::class_<proto_client::SpellEntry>("Spell")
				.def_readonly("id", &proto_client::SpellEntry::id)
				.def_readonly("name", &proto_client::SpellEntry::name)
				.def_readonly("description", &proto_client::SpellEntry::description)
				.def_readonly("cost", &proto_client::SpellEntry::cost)
				.def_readonly("cooldown", &proto_client::SpellEntry::cooldown)
				.def_readonly("powertype", &proto_client::SpellEntry::powertype)
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
			luabind::def("UnitPower", &Script_UnitPower),
			luabind::def("UnitPowerMax", &Script_UnitPowerMax),
			luabind::def("UnitLevel", &Script_UnitLevel),
			luabind::def("UnitStat", &Script_UnitStat, luabind::joined<luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),
			luabind::def("UnitArmor", &Script_UnitArmor, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>>()),
			luabind::def("UnitName", &Script_UnitName),
			luabind::def("UnitMoney", &Script_UnitMoney),
			luabind::def("UnitPowerType", &Script_UnitPowerType),
			luabind::def("PlayerXp", &Script_PlayerXp),
			luabind::def("PlayerNextLevelXp", &Script_PlayerNextLevelXp),

			luabind::def("GetSpell", &Script_GetSpell),
			luabind::def("CastSpell", &Script_CastSpell),
			luabind::def("GetPlayerAura", &Script_GetPlayerAura),

			luabind::def("GetSpellDescription", &Script_GetSpellDescription),

			luabind::def("MoveForwardStart", &Script_MoveForwardStart),
			luabind::def("MoveForwardStop", &Script_MoveForwardStop),
			luabind::def("MoveBackwardStart", &Script_MoveBackwardStart),
			luabind::def("MoveBackwardStop", &Script_MoveBackwardStop),
			luabind::def("TurnLeftStart", &Script_TurnLeftStart),
			luabind::def("TurnLeftStop", &Script_TurnLeftStop),
			luabind::def("TurnRightStart", &Script_TurnRightStart),
			luabind::def("TurnRightStop", &Script_TurnRightStop),
			luabind::def("StrafeLeftStart", &Script_StrafeLeftStart),
			luabind::def("StrafeLeftStop", &Script_StrafeLeftStop),
			luabind::def("StrafeRightStart", &Script_StrafeRightStart),
			luabind::def("StrafeRightStop", &Script_StrafeRightStop),

			luabind::def("GetBackpackSlot", &Script_GetBackpackSlot),
			luabind::def("IsBackpackSlot", &Script_IsBackpackSlot),
			luabind::def("GetBagSlot", &Script_GetBagSlot),

			luabind::def("GetInventorySlotItem", &Script_GetInventorySlotItem),

			luabind::def("GetInventorySlotIcon", &Script_GetInventorySlotIcon),
			luabind::def("GetInventorySlotCount", &Script_GetInventorySlotCount),
			luabind::def("GetInventorySlotQuality", &Script_GetInventorySlotQuality),

			luabind::def<std::function<void(uint32)>>("PickupContainerItem", [this](uint32 slot) { this->PickupContainerItem(slot); }),

			luabind::def<std::function<void()>>("ReviveMe", [this]() { m_realmConnector.SendReviveRequest(); })
		];

		luabind::globals(m_luaState.get())["loginConnector"] = &m_loginConnector;
		luabind::globals(m_luaState.get())["realmConnector"] = &m_realmConnector;
		luabind::globals(m_luaState.get())["loginState"] = m_loginState.get();
		luabind::globals(m_luaState.get())["gameData"] = &m_project;

		// Functions now registered
		m_globalFunctionsRegistered = true;
	}

	void GameScript::PickupContainerItem(uint32 slot) const
	{
		// Check if the cursor has an item
		if (g_cursor.GetCursorItem() != static_cast<uint32>(-1))
		{
			// We already have an item picked up, so swap items in slots
			if (slot != g_cursor.GetCursorItem())
			{
				// Actually a different slot: Check the target slot and if we should swap items
				auto targetItem = GetItemFromSlot("player", slot);
				if (targetItem)
				{
					// Swap source and target slot
					m_realmConnector.SwapInvItem(g_cursor.GetCursorItem() & 0xFF, slot & 0xFF);
				}
				else
				{
					// Drop item in slot
					m_realmConnector.SwapInvItem(g_cursor.GetCursorItem() & 0xFF, slot & 0xFF);
				}
			}

			g_cursor.Clear();
		}
		else
		{
			// Pick up the item from the slot
			g_cursor.SetItem(slot);

			// Lock the old item slot
		}

	}

	void GameScript::Script_ReviveMe()
	{
		m_realmConnector.SendReviveRequest();
	}
}
