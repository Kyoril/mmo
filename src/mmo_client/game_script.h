// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/macros.h"

#include "deps/lua/lua.hpp"

#include <memory>

#include "base/typedefs.h"


namespace mmo
{
	class CharSelect;
	class CharCreateInfo;
}

namespace mmo
{
	class UnitHandle;
	class PartyInfo;
}

namespace mmo
{
	class QuestClient;
	class TrainerClient;
	class SpellCast;
	class ActionBar;
	struct ItemInfo;
	class VendorClient;
	class LootClient;
	class IAudio;

	namespace proto_client
	{
		class SpellEntry;
		class Project;
	}

	class LoginState;

	/// Helper class for deleting a lua_State in a smart pointer.
	struct LuaStateDeleter final
	{
		void operator()(lua_State* state) const
		{
			// Call lua_close on the state to delete it
			lua_close(state);
		}
	};

	class LoginConnector;
	class RealmConnector;

	/// This class manages everything related to lua scripts for the game client.
	class GameScript final
		: public NonCopyable
	{
	public:
		GameScript(
			LoginConnector& loginConnector,
			RealmConnector& realmConnector,
			LootClient& lootClient,
			VendorClient& vendorClient,
			std::shared_ptr<LoginState> loginState,
			const proto_client::Project& project,
			ActionBar& actionBar,
			SpellCast& spellCast,
			TrainerClient& trainerClient,
			QuestClient& questClient,
			IAudio& audio,
			PartyInfo& partyInfo,
			CharCreateInfo& charCreateInfo,
			CharSelect& charSelect);

	public:
		/// Gets the current lua state
		inline lua_State& GetLuaState() { ASSERT(m_luaState);  return *m_luaState; }

		void GetVendorItemInfo(int32 slot, const ItemInfo*& outItem, String& outIcon, int32& outPrice, int32& outQuantity, int32& outNumAvailable, bool& outUsable) const;

		void GetTrainerSpellInfo(int32 slot, int32& outSpellId, String& outName, String& outIcon, int32& outPrice) const;

	private:
		/// Registers global functions to the internal lua state.
		void RegisterGlobalFunctions();

		void PickupContainerItem(::uint32 slot) const;

		void UseContainerItem(::uint32 slot) const;

		void TargetUnit(const char* name) const;

		void LootSlot(int32 slot, bool force) const;

		int32 GetNumLootItems() const;

		bool LootSlotIsItem(uint32 slot) const;

		bool LootSlotIsCoin(uint32 slot) const;

		void GetLootSlotInfo(uint32 slot, String& out_icon, String& out_text, int32& out_count) const;

		const ItemInfo* GetLootSlotItem(uint32 slot) const;

		void CloseLoot() const;

		int32 GetContainerNumSlots(int32 container) const;

		void BuyVendorItem(uint32 slot, uint8 count) const;

		void AddAttributePoint(uint32 attribute) const;

		const char* GetItemSpellTriggerType(const ItemInfo* item, int32 index);

		const proto_client::SpellEntry* GetItemSpell(const ItemInfo* item, int32 index);

		void PlaySound(const char* sound) const;

		std::shared_ptr<UnitHandle> GetUnitHandleByName(const std::string& unitName) const;

		void SendChatMessage(const char* message, const char* type, const char* target = nullptr) const;

	private:
		typedef std::unique_ptr<lua_State, LuaStateDeleter> LuaStatePtr;

	private:
		LoginConnector& m_loginConnector;

		RealmConnector& m_realmConnector;

		LootClient& m_lootClient;

		VendorClient& m_vendorClient;

		/// The current lua state.
		LuaStatePtr m_luaState;

		/// Whether the global functions have been registered.
		bool m_globalFunctionsRegistered = false;

		std::shared_ptr<LoginState> m_loginState;

		const proto_client::Project& m_project;

		ActionBar& m_actionBar;

		SpellCast& m_spellCast;

		TrainerClient& m_trainerClient;

		QuestClient& m_questClient;

		IAudio& m_audio;

		PartyInfo& m_partyInfo;

		CharCreateInfo& m_charCreateInfo;

		CharSelect& m_charSelect;

	private:
		void Script_ReviveMe() const;
	};
}
