// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

#include "action_bar.h"
#include "cursor.h"
#include "loot_client.h"
#include "platform.h"
#include "quest_client.h"
#include "trainer_client.h"
#include "vendor_client.h"
#include "game/item.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "game_client/game_bag_c.h"
#include "game_client/game_item_c.h"
#include "game_client/object_mgr.h"
#include "game_client/game_player_c.h"
#include "game_client/unit_handle.h"
#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/out_value_policy.hpp"
#include "shared/client_data/proto_client/spells.pb.h"

namespace luabind
{
	// A little helper to combine multiple policies
	template <typename... T>
 	using joined = typename luabind::meta::join<T...>::type;
}

namespace mmo
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

	static const char* s_consumableSubclassStrings[] = {
		"CONSUMABLE",
		"POTION",
		"ELIXIR",
		"FLASK",
		"SCROLL",
		"FOOD",
		"ITEM_ENHANCEMENT",
		"BANDAGE"
	};

	static_assert(std::size(s_consumableSubclassStrings) == item_subclass_consumable::Count_, "Consumable subclass strings array size mismatch");

	static const char* s_containerSubclassStrings[] = {
		"CONTAINER"
	};

	static_assert(std::size(s_containerSubclassStrings) == item_subclass_container::Count_, "Container subclass strings array size mismatch");

	static const char* s_weaponSubclassStrings[] = {
		"ONE_HANDED_AXE",
		"TWO_HANDED_AXE",
		"BOW",
		"GUN",
		"ONE_HANDED_MACE",
		"TWO_HANDED_MACE",
		"POLEARM",
		"ONE_HANDED_SWORD",
		"TWO_HANDED_SWORD",
		"STAFF",
		"FIST",
		"DAGGER",
		"THROWN",
		"SPEAR",
		"CROSS_BOW",
		"WAND",
		"FISHING_POLE"
	};

	static_assert(std::size(s_weaponSubclassStrings) == item_subclass_weapon::Count_, "Weapon subclass strings array size mismatch");

	static const char* s_gemSubclassStrings[] = {
		"RED",
		"BLUE",
		"YELLOW",
		"PURPLE",
		"GREEN",
		"ORANGE",
		"PRISMATIC"
	};

	static_assert(std::size(s_gemSubclassStrings) == item_subclass_gem::Count_, "Gem subclass strings array size mismatch");

	static const char* s_armorSubclassStrings[] = {
		"MISC",
		"CLOTH",
		"LEATHER",
		"MAIL",
		"PLATE",
		"BUCKLER",
		"SHIELD",
		"LIBRAM",
		"IDOL",
		"TOTEM"
	};

	static_assert(std::size(s_armorSubclassStrings) == item_subclass_armor::Count_, "Armor subclass strings array size mismatch");

	static const char* s_projectileSubclassStrings[] = {
		"WAND",
		"BOLT",
		"ARROW",
		"BULLET",
		"THROWN"
	};

	static_assert(std::size(s_projectileSubclassStrings) == item_subclass_projectile::Count_, "Projectile subclass strings array size mismatch");

	static const char* s_tradeGoodsSubclassStrings[] = {
		"TRADE_GOODS",
		"PARTS",
		"EXPLOSIVES",
		"DEVICES",
		"JEWELCRAFTING",
		"CLOTH",
		"LEATHER",
		"METAL_STONE",
		"MEAT",
		"HERB",
		"ELEMENTAL",
		"TRADE_GOODS_OTHER",
		"ENCHANTING",
		"MATERIAL"
	};

	static_assert(std::size(s_tradeGoodsSubclassStrings) == item_subclass_trade_goods::Count_, "Trade goods subclass strings array size mismatch");

	static const char* s_inventoryTypeStrings[] = {
		"NON_EQUIP",
		"HEAD",
		"NECK",
		"SHOULDERS",
		"BODY",
		"CHEST",
		"WAIST",
		"LEGS",
		"FEET",
		"WRISTS",
		"HANDS",
		"FINGER",
		"TRINKET",
		"WEAPON",
		"SHIELD",
		"RANGED",
		"CLOAK",
		"TWO_HANDED_WEAPON",
		"BAG",
		"TABARD",
		"ROBE",
		"MAIN_HAND_WEAPON",
		"OFF_HAND_WEAPON",
		"HOLDABLE",
		"AMMO",
		"THROWN",
		"RANGED_RIGHT",
		"QUIVER",
		"RELIC",
	};

	static_assert(std::size(s_inventoryTypeStrings) == inventory_type::Count_, "Inventory type strings array size mismatch");


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

		std::shared_ptr<UnitHandle> Script_GetUnitHandleByName(const std::string& unitName)
		{
			if (unitName == "player")
			{
				if (const auto player = ObjectMgr::GetActivePlayer())
				{
					return std::make_shared<UnitHandle>(*player);
				}
			}
			else if (unitName == "target")
			{
				if (const auto playerObject = ObjectMgr::GetActivePlayer())
				{
					if (const auto target = ObjectMgr::Get<GameUnitC>(playerObject->Get<uint64>(object_fields::TargetUnit)); target)
					{
						return std::make_shared<UnitHandle>(*target);
					}
				}
			}

			return nullptr;
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
			const GameUnitC* player = ObjectMgr::GetActivePlayer().get();
			if (!player)
			{
				return nullptr;
			}

			return player->GetSpell(index);
		}

		bool Script_UnitExists(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return true;
			}

			return false;
		}

		int32 Script_UnitAttributeCost(const std::string& unitName, uint32 attribute)
		{
			if (const auto unit = Script_GetUnitByName(unitName))
			{
				if (const auto player = std::dynamic_pointer_cast<GamePlayerC>(unit))
				{
					return player->GetAttributeCost(attribute);
				}
			}

			return 0;
		}

		int32 Script_UnitDisplayId(const std::string& unitName)
		{
			if (auto unit = Script_GetUnitByName(unitName))
			{
				return unit->Get<uint32>(object_fields::DisplayId);
			}

			return -1;
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
				if ((static_cast<uint16>(slotId) >> 8) == player_inventory_slots::Bag_0 && 
					(slotId & 0xFF) >= player_inventory_pack_slots::Start && 
					(slotId & 0xFF) < player_inventory_pack_slots::End)
				{
					const uint8 slotFieldOffset = (static_cast<uint8>(slotId & 0xFF) - player_inventory_slots::End) * 2;
					itemGuid = unit->Get<uint64>(object_fields::PackSlot_1 + slotFieldOffset);
				}
				else if ((static_cast<uint16>(slotId) >> 8) == player_inventory_slots::Bag_0 &&
					(slotId & 0xFF) >= player_equipment_slots::Start &&
					(slotId & 0xFF) < player_equipment_slots::End)
				{
					const uint8 slotFieldOffset = static_cast<uint8>(slotId & 0xFF) * 2;
					itemGuid = unit->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);
				}
				else if ((static_cast<uint16>(slotId) >> 8) == player_inventory_slots::Bag_0 &&
					(slotId & 0xFF) >= player_inventory_slots::Start &&
					(slotId & 0xFF) < player_inventory_slots::End)
				{
					const uint8 slotFieldOffset = static_cast<uint8>(slotId & 0xFF) * 2;
					itemGuid = unit->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);
				}
				else if ((static_cast<uint16>(slotId) >> 8) == player_inventory_slots::Bag_0 &&
					(slotId & 0xFF) >= player_inventory_slots::Start &&
					(slotId & 0xFF) < player_inventory_slots::End)
				{
					const uint8 slotFieldOffset = static_cast<uint8>(slotId & 0xFF) * 2;
					itemGuid = unit->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);
				}
				else if (static_cast<uint16>(slotId) >> 8 >= player_inventory_slots::Start &&
					static_cast<uint16>(slotId) >> 8 < player_inventory_slots::End)
				{
					// Bag slots, get bag item first
					const uint8 slotFieldOffset = (static_cast<uint16>(slotId) >> 8) * 2;
					itemGuid = unit->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);

					if (itemGuid == 0)
					{
						return nullptr;
					}

					// The actual bag item
					std::shared_ptr<GameBagC> bag = ObjectMgr::Get<GameBagC>(itemGuid);
					if (!bag)
					{
						return nullptr;
					}

					// Out of bag bounds?
					if ((slotId & 0xFF) >= bag->Get<uint32>(object_fields::NumSlots))
					{
						return nullptr;
					}

					itemGuid = bag->Get<uint64>(object_fields::Slot_1 + (slotId & 0xFF) * 2);
					if (itemGuid == 0)
					{
						return nullptr;
					}

					return ObjectMgr::Get<GameItemC>(itemGuid);
				}

				if (itemGuid == 0)
				{
					return nullptr;
				}

				// Get item at the specified slot
				return ObjectMgr::Get<GameItemC>(itemGuid);
			}

			return nullptr;
		}

		void Script_GetInventorySlotType(const char* unitName, int32 slotId, const char*& out_class, const char*& out_subclass, const char*& out_inventoryType)
		{
			out_class = nullptr;
			out_subclass = nullptr;
			out_inventoryType = nullptr;

			std::shared_ptr<GameItemC> item = GetItemFromSlot(unitName, slotId);
			if (!item)
			{
				return;
			}

			if (!item->GetEntry())
			{
				return;
			}

			const uint32 itemClass = item->GetEntry()->itemClass;
			const uint32 itemSubclass = item->GetEntry()->itemSubclass;

			if (itemClass < std::size(s_itemClassStrings))
			{
				out_class = s_itemClassStrings[itemClass];
			}

			switch (itemClass)
			{
			case item_class::Consumable:	out_subclass = s_consumableSubclassStrings[itemSubclass]; break;
			case item_class::Container:		out_subclass = s_containerSubclassStrings[itemSubclass]; break;
			case item_class::Weapon:		out_subclass = s_weaponSubclassStrings[itemSubclass]; break;
			case item_class::Gem:			out_subclass = s_gemSubclassStrings[itemSubclass]; break;
			case item_class::Armor:			out_subclass = s_armorSubclassStrings[itemSubclass]; break;
			case item_class::Projectile:	out_subclass = s_projectileSubclassStrings[itemSubclass]; break;
			case item_class::TradeGoods:	out_subclass = s_tradeGoodsSubclassStrings[itemSubclass]; break;
			}

			const uint32 inventoryType = item->GetEntry()->inventoryType;
			out_inventoryType = s_inventoryTypeStrings[inventoryType];
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

		void Script_UnitAura(const std::string& unitName, uint32 id, const proto_client::SpellEntry*& out_spell, int32& out_duration)
		{
			out_duration = -1;
			out_spell = nullptr;

			std::shared_ptr<GameUnitC> unit = Script_GetUnitByName(unitName);
			if (!unit)
			{
				return;
			}

			// TODO: Check unit auras


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

		void Script_Jump()
		{
			if (!WorldState::GetInputControl()) return;
			WorldState::GetInputControl()->Jump();
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

			const std::shared_ptr<GameUnitC> player = ObjectMgr::GetActivePlayer();
			ASSERT(player);

			const int32 level = player->GetLevel();

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
								snprintf(buffer, 128, format->c_str(), displayValue);
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
	}


	GameScript::GameScript(LoginConnector& loginConnector, RealmConnector& realmConnector, LootClient& lootClient, VendorClient& vendorClient, std::shared_ptr<LoginState> loginState, const proto_client::Project& project, ActionBar& actionBar, SpellCast& spellCast, TrainerClient& trainerClient, QuestClient& questClient)
		: m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_lootClient(lootClient)
		, m_vendorClient(vendorClient)
		, m_loginState(std::move(loginState))
		, m_project(project)
		, m_actionBar(actionBar)
		, m_spellCast(spellCast)
		, m_trainerClient(trainerClient)
		, m_questClient(questClient)
	{
		// Initialize the lua state instance
		m_luaState = LuaStatePtr(luaL_newstate());

		luaL_openlibs(m_luaState.get());
		
		// Initialize luabind
		luabind::open(m_luaState.get());

		// Register custom global functions to the lua state instance
		RegisterGlobalFunctions();
	}

	void GameScript::GetVendorItemInfo(int32 slot, String& outName, String& outIcon, int32& outPrice, int32& outQuantity, int32& outNumAvailable, bool& outUsable) const
	{
		const auto& vendorItems = m_vendorClient.GetVendorItems();
		if (slot < 0 || slot >= vendorItems.size())
		{
			outName.clear();
			outIcon.clear();
			outPrice = 0;
			outQuantity = 0;
			outNumAvailable = 0;
			outUsable = false;
			return;
		}

		ASSERT(vendorItems[slot].itemData);
		outName = vendorItems[slot].itemData->name;
		outIcon = vendorItems[slot].itemData->icon;
		outPrice = vendorItems[slot].buyPrice + vendorItems[slot].extendedCost;
		outQuantity = vendorItems[slot].buyCount;
		outNumAvailable = vendorItems[slot].maxCount;
		outUsable = false;
	}

	void GameScript::GetTrainerSpellInfo(int32 slot, int32& outSpellId, String& outName, String& outIcon, int32& outPrice) const
	{
		const auto& trainerSpells = m_trainerClient.GetTrainerSpells();
		if (slot < 0 || slot >= trainerSpells.size())
		{
			outSpellId = -1;
			outName.clear();
			outIcon.clear();
			outPrice = 0;
			return;
		}

		ASSERT(trainerSpells[slot].spell);
		outSpellId = trainerSpells[slot].spell->id();
		outName = trainerSpells[slot].spell->name();
		outIcon = trainerSpells[slot].spell->icon();
		outPrice = trainerSpells[slot].cost;
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
				.def_readonly("armor", &ItemInfo::armor)
				.def_readonly("block", &ItemInfo::block)
				.def_readonly("maxdurability", &ItemInfo::maxdurability)
				.def_readonly("icon", &ItemInfo::icon)
				.def_readonly("sellPrice", &ItemInfo::sellPrice)),

			luabind::scope(
				luabind::class_<QuestListEntry>("QuestListEntry")
				.def_readonly("id", &QuestListEntry::questId)
				.def_readonly("title", &QuestListEntry::questTitle)
				.def_readonly("icon", &QuestListEntry::menuIcon)
				.def_readonly("isActive", &QuestListEntry::isActive)),

			luabind::scope(
				luabind::class_<QuestInfo>("Quest")
				.def_readonly("id", &QuestInfo::id)
				.def_readonly("title", &QuestInfo::title)),

			luabind::scope(
				luabind::class_<QuestLogEntry>("QuestLogEntry")
				.def_readonly("id", &QuestLogEntry::questId)
				.def_readonly("quest", &QuestLogEntry::quest)
				.def_readonly("status", &QuestLogEntry::status)),

			luabind::scope(
				luabind::class_<QuestDetails>("QuestDetails")
				.def_readonly("id", &QuestDetails::questId)
				.def_readonly("title", &QuestDetails::questTitle)
				.def_readonly("details", &QuestDetails::questDetails)
				.def_readonly("objectives", &QuestDetails::questObjectives)
				.def_readonly("offerReward", &QuestDetails::questOfferRewardText)
				.def_readonly("requestItems", &QuestDetails::questRequestItemsText)
				.def_readonly("rewardedXp", &QuestDetails::rewardXp)
				.def_readonly("rewardedMoney", &QuestDetails::rewardMoney)
				.def_readonly("rewardedSpell", &QuestDetails::rewardSpell)),

			luabind::scope(
				luabind::class_<UnitHandle>("UnitHandle")
				.def("GetHealth", &UnitHandle::GetHealth)
				.def("GetMaxHealth", &UnitHandle::GetMaxHealth)
				.def("GetPower", &UnitHandle::GetPower)
				.def("GetMaxPower", &UnitHandle::GetMaxPower)
				.def("GetLevel", &UnitHandle::GetLevel)
				.def("GetAuraCount", &UnitHandle::GetAuraCount)
				.def("GetAura", &UnitHandle::GetAura)
				.def("GetName", &UnitHandle::GetName)
				.def("GetPowerType", &UnitHandle::GetPowerType)
				.def("GetMinDamage", &UnitHandle::GetMinDamage)
				.def("GetMaxDamage", &UnitHandle::GetMaxDamage)
				.def("GetArmor", &UnitHandle::GetArmor)
				.def("GetAvailableAttributePoints", &UnitHandle::GetAvailableAttributePoints)
				.def("GetArmorReductionFactor", &UnitHandle::GetArmorReductionFactor)
				.def("GetStat", &UnitHandle::GetStat)
				.def("GetPosStat", &UnitHandle::GetPosStat)
				.def("GetNegStat", &UnitHandle::GetNegStat)),

			luabind::scope(
				luabind::class_<AuraHandle>("AuraHandle")
				.def("IsExpired", &AuraHandle::IsExpired)
				.def("CanExpire", &AuraHandle::CanExpire)
				.def("GetDuration", &AuraHandle::GetDuration)
				.def("GetSpell", &AuraHandle::GetSpell)),

			luabind::scope(
				luabind::class_<proto_client::SpellEntry>("Spell")
				.def_readonly("id", &proto_client::SpellEntry::id)
				.def_readonly("name", &proto_client::SpellEntry::name)
				.def_readonly("description", &proto_client::SpellEntry::description)
				.def_readonly("rank", &proto_client::SpellEntry::rank)
				.def_readonly("cost", &proto_client::SpellEntry::cost)
				.def_readonly("cooldown", &proto_client::SpellEntry::cooldown)
				.def_readonly("powertype", &proto_client::SpellEntry::powertype)
				.def_readonly("level", &proto_client::SpellEntry::spelllevel)
				.def_readonly("casttime", &proto_client::SpellEntry::casttime)
				.def_readonly("icon", &proto_client::SpellEntry::icon)
				.def_readonly("auratext", &proto_client::SpellEntry::auratext)),

			luabind::def("GetUnit", &Script_GetUnitHandleByName),

			luabind::def("RunConsoleCommand", &Script_RunConsoleCommand),
			luabind::def("GetCVar", &Script_GetConsoleVar),
			luabind::def("EnterWorld", &Script_EnterWorld),
			luabind::def("print", &Script_Print),

			luabind::def("IsShiftKeyDown", Platform::IsShiftKeyDown),

			luabind::def("UnitExists", &Script_UnitExists),
			luabind::def("UnitAttributeCost", &Script_UnitAttributeCost),
			luabind::def("UnitStat", &Script_UnitStat, luabind::joined<luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),
			luabind::def("UnitArmor", &Script_UnitArmor, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>>()),
			luabind::def("UnitMoney", &Script_UnitMoney),
			luabind::def("UnitDisplayId", &Script_UnitDisplayId),
			luabind::def("PlayerXp", &Script_PlayerXp),
			luabind::def("PlayerNextLevelXp", &Script_PlayerNextLevelXp),
			luabind::def<std::function<void(const char*)>>("TargetUnit", [this](const char* unitName) { this->TargetUnit(unitName); }),

			luabind::def("GetSpell", &Script_GetSpell),
			luabind::def<std::function<void(int32)>>("CastSpell", [this](int32 spellIndex) { if (const auto* spell = Script_GetSpell(spellIndex)) m_spellCast.CastSpell(spell->id()); }),
			luabind::def("UnitAura", &Script_UnitAura, luabind::joined<luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),

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

			luabind::def("Jump", &Script_Jump),

			luabind::def("GetBackpackSlot", &Script_GetBackpackSlot),
			luabind::def("IsBackpackSlot", &Script_IsBackpackSlot),
			luabind::def("GetBagSlot", &Script_GetBagSlot),

			luabind::def("GetInventorySlotItem", &Script_GetInventorySlotItem),

			luabind::def("GetInventorySlotIcon", &Script_GetInventorySlotIcon),
			luabind::def("GetInventorySlotCount", &Script_GetInventorySlotCount),
			luabind::def("GetInventorySlotQuality", &Script_GetInventorySlotQuality),
			luabind::def("GetInventorySlotType", &Script_GetInventorySlotType, luabind::joined<luabind::pure_out_value<3>, luabind::pure_out_value<4>, luabind::pure_out_value<5>>()),

			// Quest
			luabind::def<std::function<String()>>("GetGreetingText", [this]() { return m_questClient.GetGreetingText(); }),
			luabind::def<std::function<int32()>>("GetNumAvailableQuests", [this]() { return m_questClient.GetNumAvailableQuests(); }),
			luabind::def<std::function<const QuestListEntry*(uint32)>>("GetAvailableQuest", [this](uint32 index) { return m_questClient.GetAvailableQuest(index); }),
			luabind::def<std::function<void(uint32)>>("QueryQuestDetails", [this](uint32 questId) { m_questClient.QueryQuestDetails(questId); }),
			luabind::def<std::function<const QuestDetails*()>>("GetQuestDetails", [this]() { return m_questClient.GetQuestDetails(); }),
			luabind::def<std::function<void(uint32)>>("AcceptQuest", [this](uint32 questId) { m_questClient.AcceptQuest(questId); }),
			luabind::def<std::function<uint32()>>("GetNumQuestLogEntries", [this]() { return m_questClient.GetNumQuestLogEntries(); }),
			luabind::def<std::function<const QuestLogEntry*(uint32)>>("GetQuestLogEntry", [this](uint32 index) { return m_questClient.GetQuestLogEntry(index); }),
			luabind::def<std::function<void(uint32)>>("AbandonQuest", [this](uint32 questId) { m_questClient.AbandonQuest(questId); }),
			luabind::def<std::function<void(uint32)>>("GetQuestReward", [this](uint32 rewardChoice) { m_questClient.GetQuestReward(rewardChoice); }),
			luabind::def<std::function<void(uint32)>>("QuestLogSelectQuest", [this](uint32 questId) { m_questClient.QuestLogSelectQuest(questId); }),
			luabind::def<std::function<uint32()>>("GetQuestLogSelection", [this]() { return m_questClient.GetSelectedQuestLogQuest(); }),
			luabind::def<std::function<uint32()>>("GetQuestObjectiveCount", [this]() { return m_questClient.GetQuestObjectiveCount(); }),
			luabind::def<std::function<const char*(uint32)>>("GetQuestObjectiveText", [this](uint32 index) { return m_questClient.GetQuestObjectiveText(index); }),

			luabind::def<std::function<String(const QuestInfo* quest)>>("GetQuestDetailsText", [this](const QuestInfo* quest) -> String { if (!quest) { return ""; } String questText = quest->description; m_questClient.ProcessQuestText(questText); return questText; }),
			luabind::def<std::function<String(const QuestInfo* quest)>>("GetQuestObjectivesText", [this](const QuestInfo* quest) -> String { if (!quest) { return ""; } String questText = quest->summary; m_questClient.ProcessQuestText(questText); return questText; }),

			// Spellbook
			luabind::def<std::function<void(uint32)>>("PickupSpell", [this](uint32 spell) { g_cursor.SetSpell(spell); }),

			// ActionBar
			luabind::def<std::function<void(int32)>>("UseActionButton", [this](int32 slot) { this->m_actionBar.UseActionButton(slot); }),
			luabind::def<std::function<void(int32)>>("PickupActionButton", [this](int32 slot) { this->m_actionBar.PickupActionButton(slot); }),
			luabind::def<std::function<bool(int32)>>("IsActionButtonUsable", [this](int32 slot) { return this->m_actionBar.IsActionButtonUsable(slot); }),
			luabind::def<std::function<bool(int32)>>("IsActionButtonItem", [this](int32 slot) { return this->m_actionBar.IsActionButtonItem(slot); }),
			luabind::def<std::function<bool(int32)>>("IsActionButtonSpell", [this](int32 slot) { return this->m_actionBar.IsActionButtonSpell(slot); }),
			luabind::def<std::function<const proto_client::SpellEntry*(int32)>>("GetActionButtonSpell", [this](int32 slot) { return this->m_actionBar.GetActionButtonSpell(slot); }),
			luabind::def<std::function<const ItemInfo*(int32)>>("GetActionButtonItem", [this](int32 slot) { return this->m_actionBar.GetActionButtonItem(slot); }),

			// Vendor
			luabind::def<std::function<uint32()>>("GetVendorNumItems", [this]() { return this->m_vendorClient.GetNumVendorItems(); }),
			luabind::def<std::function<void(int32, String&, String&, int32&, int32&, int32&, bool&)>>("GetVendorItemInfo", [this](int32 slot, String& out_name, String& out_icon, int32& out_price, int32& out_quantity, int32& out_numAvailable, bool& out_usable) { return this->GetVendorItemInfo(slot, out_name, out_icon, out_price, out_quantity, out_numAvailable, out_usable); }, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, luabind::pure_out_value<4>, luabind::pure_out_value<5>, luabind::pure_out_value<6>, luabind::pure_out_value<7>>()),
			luabind::def<std::function<void(uint32)>>("BuyVendorItem", [this](uint32 slot) { this->BuyVendorItem(slot, 1); }),
			luabind::def<std::function<void()>>("CloseVendor", [this]() { this->m_vendorClient.CloseVendor(); }),

			// Trainer
			luabind::def<std::function<uint32()>>("GetNumTrainerSpells", [this]() { return this->m_trainerClient.GetNumTrainerSpells(); }),
			luabind::def<std::function<void(int32, int32&, String&, String&, int32&)>>("GetTrainerSpellInfo", [this](int32 slot, int32& out_spellId, String& out_name, String& out_icon, int32& out_price) { return this->GetTrainerSpellInfo(slot, out_spellId, out_name, out_icon, out_price); }, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, luabind::pure_out_value<4>, luabind::pure_out_value<5>>()),
			luabind::def<std::function<void(uint32)>>("BuyTrainerSpell", [this](uint32 slot) { this->m_trainerClient.BuySpell(slot); }),
			luabind::def<std::function<void()>>("CloseTrainer", [this]() { this->m_trainerClient.CloseTrainer(); }),

			luabind::def<std::function<const char* (const ItemInfo*, int32)>>("GetItemSpellTriggerType", [this](const ItemInfo* item, int32 index) { return this->GetItemSpellTriggerType(item, index); }),
			luabind::def<std::function<const proto_client::SpellEntry* (const ItemInfo*, int32)>>("GetItemSpell", [this](const ItemInfo* item, int32 index) { return this->GetItemSpell(item, index); }),

			luabind::def<std::function<void(uint32)>>("AddAttributePoint", [this](uint32 attributeId) { return this->AddAttributePoint(attributeId); }),
			luabind::def<std::function<uint32(int32)>>("GetContainerNumSlots", [this](int32 slot) { return this->GetContainerNumSlots(slot); }),
			luabind::def<std::function<void(uint32)>>("PickupContainerItem", [this](uint32 slot) { this->PickupContainerItem(slot); }),

			luabind::def<std::function<void(uint32)>>("UseContainerItem", [this](uint32 slot) { this->UseContainerItem(slot); }),

			luabind::def<std::function<int32()>>("GetNumLootItems", [this]() { return this->GetNumLootItems(); }),
			luabind::def<std::function<void(int32, bool)>>("LootSlot", [this](int32 slot, bool force) { this->LootSlot(slot, force); }),
			luabind::def<std::function<bool(int32)>>("LootSlotIsCoin", [this](int32 slot) { return this->LootSlotIsCoin(slot); }),
			luabind::def<std::function<bool(int32)>>("LootSlotIsItem", [this](int32 slot) { return this->LootSlotIsItem(slot); }),
			luabind::def<std::function<void()>>("CloseLoot", [this]() { this->CloseLoot(); }),
			luabind::def<std::function<void(int32, String&, String&, int32&)>>("GetLootSlotInfo", [this](int32 slot, String& out_icon, String& out_text, int32& out_count) { return this->GetLootSlotInfo(slot, out_icon, out_text, out_count); }, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),

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
				// Check if its a bag or the player inventory
				if ((g_cursor.GetCursorItem() >> 8) == player_inventory_slots::Bag_0 &&
					(slot >> 8) == player_inventory_slots::Bag_0)
				{
					// Same bag
					m_realmConnector.SwapInvItem(g_cursor.GetCursorItem() & 0xFF, slot & 0xFF);
				}
				else
				{
					// Different bag
					m_realmConnector.SwapItem(g_cursor.GetCursorItem() >> 8, g_cursor.GetCursorItem() & 0xFF, slot >> 8, slot & 0xFF);
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

	void GameScript::UseContainerItem(uint32 slot) const
	{
		// Get item at slot
		std::shared_ptr<GameItemC> item = GetItemFromSlot("player", slot);
		if (!item)
		{
			return;
		}

		if (m_vendorClient.HasVendor())
		{
			m_vendorClient.SellItem(item->GetGuid());
			return;
		}

		// Check if item has any usable spells
		const auto* entry = item->GetEntry();
		if (!entry)
		{
			ELOG("Unknown item entry!");
			return;
		}

		if (entry->itemClass == item_class::Weapon ||
			entry->itemClass == item_class::Armor ||
			entry->itemClass == item_class::Container)
		{
			m_realmConnector.AutoEquipItem((slot >> 8) & 0xff, slot & 0xff);
			return;
		}

		bool isUsable = false;
		for (const ItemSpell& spell : entry->spells)
		{
			if (spell.triggertype == item_spell_trigger::OnUse)
			{
				isUsable = true;
				break;
			}
		}

		if (!isUsable)
		{
			ELOG("Item is not usable");
			return;
		}

		SpellTargetMap targetMap;
		// TODO: spell target

		m_realmConnector.UseItem((slot >> 8) & 0xFF, slot & 0xFF, item->GetGuid(), targetMap);
	}

	void GameScript::TargetUnit(const char* name) const
	{
		if (!name || !*name)
		{
			ELOG("No unit name given to TargetUnit function!");
			return;
		}

		auto player = Script_GetUnitByName("player");
		if (!player)
		{
			return;
		}

		auto target = Script_GetUnitByName(name);
		if (!target)
		{
			ELOG("Unable to find target unit " << name);
			return;
		}

		m_realmConnector.SetSelection(target->GetGuid());
	}

	void GameScript::LootSlot(int32 slot, bool force) const
	{
		if (slot < 1 || slot > GetNumLootItems())
		{
			ELOG("Unable to loot: Invalid slot " << slot);
			return;
		}

		if (m_lootClient.HasMoney() && slot == 1)
		{
			m_realmConnector.LootMoney();
		}
		else
		{
			// Loot item from slot
			m_realmConnector.AutoStoreLootItem(slot - 1);
		}

		// TODO
	}

	int32 GameScript::GetNumLootItems() const
	{
		return m_lootClient.GetNumLootSlots();
	}

	bool GameScript::LootSlotIsItem(const int32 slot) const
	{
		if (slot < 1)
		{
			return false;
		}

		if (m_lootClient.HasMoney())
		{
			if (slot == 1)
			{
				return false;
			}

			return slot - 1 <= m_lootClient.GetNumLootItems();
		}

		return slot <= m_lootClient.GetNumLootItems();
	}

	bool GameScript::LootSlotIsCoin(const int32 slot) const
	{
		if (slot < 1)
		{
			return false;
		}

		if (!m_lootClient.HasMoney() || slot != 1)
		{
			return false;
		}

		return true;
	}

	void GameScript::GetLootSlotInfo(int32 slot, String& out_icon, String& out_text, int32& out_count) const
	{
		if (slot < 1 || slot > m_lootClient.GetNumLootSlots())
		{
			out_icon = "";
			out_text = "";
			out_count = 0;
			return;
		}

		if (m_lootClient.HasMoney() && slot == 1)
		{
			out_icon = "Interface/Icons/Items/Tex_spare_parts_11_b.htex";
			out_text = m_lootClient.GetLootMoneyString();
			out_count = 1;
			return;
		}

		// Get item from slot
		if (m_lootClient.HasMoney()) slot--;

		LootClient::LootItem* item = m_lootClient.GetLootItem(slot - 1);
		if (!item || !item->itemInfo)
		{
			out_icon = "";
			out_text = "";
			out_count = 0;
			return;
		}

		out_icon = item->itemInfo->icon;
		out_text = item->itemInfo->name;
		out_count = item->count;
	}

	void GameScript::CloseLoot() const
	{
		m_lootClient.CloseLoot();
	}

	int32 GameScript::GetContainerNumSlots(int32 container) const
	{
		if (container < 0 || container >= player_inventory_slots::End - player_inventory_slots::Start)
		{
			return 0;
		}

		// Check if the container is a bag
		uint16 slotId = (static_cast<uint16>(player_inventory_slots::Bag_0) << 8) | static_cast<uint16>(container + player_inventory_slots::Start);
		std::shared_ptr<GameItemC> item = GetItemFromSlot("player", slotId);
		if (!item)
		{
			return 0;
		}

		std::shared_ptr<GameBagC> bag = std::dynamic_pointer_cast<GameBagC>(item);
		if (!bag)
		{
			return 0;
		}

		return bag->Get<uint32>(object_fields::NumSlots);
	}

	void GameScript::BuyVendorItem(uint32 slot, uint8 count) const
	{
		m_vendorClient.BuyItem(slot, count);
	}

	void GameScript::AddAttributePoint(uint32 attribute) const
	{
		if (attribute >= 5)
		{
			ELOG("AddAttributePoint: Attribute id must be in range of 0 .. 4");
			return;
		}

		m_realmConnector.AddAttributePoint(attribute);
	}

	const char* GameScript::GetItemSpellTriggerType(const ItemInfo* item, int32 index)
	{
		if (item == nullptr || index < 0 || index >= std::size(item->spells))
		{
			return nullptr;
		}

		static const char* s_triggerTypeNames[] = {
			"ON_USE",
			"ON_EQUIP",
			"HIT_CHANCE"
		};

		if (item->spells[index].triggertype > std::size(s_triggerTypeNames))
		{
			return nullptr;
		}

		return s_triggerTypeNames[item->spells[index].triggertype];
	}

	const proto_client::SpellEntry* GameScript::GetItemSpell(const ItemInfo* item, int32 index)
	{
		if (item == nullptr || index < 0 || index >= std::size(item->spells))
		{
			return nullptr;
		}

		return m_project.spells.getById(item->spells[index].spellId);
	}

	void GameScript::Script_ReviveMe() const
	{
		m_realmConnector.SendReviveRequest();
	}
}
