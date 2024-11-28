// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/macros.h"

#include "deps/lua/lua.hpp"

#include <memory>

#include "base/typedefs.h"


namespace mmo
{
	class VendorClient;
	class LootClient;

	namespace proto_client
	{
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
			const proto_client::Project& project);

	public:
		/// Gets the current lua state
		inline lua_State& GetLuaState() { ASSERT(m_luaState);  return *m_luaState; }

		void GetVendorItemInfo(int32 slot, String& outName, String& outIcon, int32& outPrice, int32& outQuantity, int32& outNumAvailable, bool& outUsable) const;

	private:
		/// Registers global functions to the internal lua state.
		void RegisterGlobalFunctions();

		void PickupContainerItem(::uint32 slot) const;

		void UseContainerItem(::uint32 slot) const;

		void TargetUnit(const char* name) const;

		void LootSlot(int32 slot, bool force) const;

		int32 GetNumLootItems() const;

		bool LootSlotIsItem(int32 slot) const;

		bool LootSlotIsCoin(int32 slot) const;

		void GetLootSlotInfo(int32 slot, String& out_icon, String& out_text, int32& out_count) const;

		void CloseLoot() const;

		int32 GetContainerNumSlots(int32 container) const;

		void BuyVendorItem(uint32 slot, uint8 count) const;

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

	private:
		void Script_ReviveMe() const;
	};
}
