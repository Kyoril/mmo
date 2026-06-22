#pragma once

#include "data/client_cache.h"
#include "base/non_copyable.h"
#include "client_data/project.h"
#include "game/action_button.h"

#include <set>

namespace mmo
{
	class SpellCast;
}

namespace mmo
{
	struct ItemInfo;

	namespace proto_client
	{
		class SpellEntry;
	}

	class RealmConnector;

	/// Class for the action bar on the client.
	class ActionBar final : public NonCopyable
	{
	public:
		explicit ActionBar(RealmConnector& realmConnector, const proto_client::SpellManager& spells, DBItemCache& items, SpellCast& spellCast);
		~ActionBar() override = default;

	public:
		const ActionButton& GetActionButton(int32 slot) const;

		[[nodiscard]] bool IsActionButtonUsable(int32 slot) const;

		[[nodiscard]] bool IsActionButtonSpell(int32 slot) const;

		[[nodiscard]] bool IsActionButtonItem(int32 slot) const;

		[[nodiscard]] const proto_client::SpellEntry* GetActionButtonSpell(int32 slot) const;

		[[nodiscard]] const ItemInfo* GetActionButtonItem(int32 slot) const;

		void UseActionButton(int32 slot);

		void PickupActionButton(int32 slot);

		void OnActionButtons(io::Reader& reader);

		void SetActionButton(int32 slot, const ActionButton& button);

		void ClearActionButton(int32 slot);

	private:
		static bool IsValidSlot(int32 slot);

		void ActionButtonChanged(int32 slot) const;

		/// Ensures the item data for the given item entry is loaded. If it is not yet cached,
		/// an async query is sent to the realm and the action bar is refreshed once the data arrives.
		void EnsureItemData(uint16 itemId);

		/// Requests item data for all item-type action buttons that are not yet cached.
		void EnsureItemDataForAllButtons();

	private:
		RealmConnector& m_connector;
		const proto_client::SpellManager& m_spells;
		DBItemCache& m_items;
		ActionButtons m_actionButtons;
		SpellCast& m_spellCast;
		std::set<uint16> m_pendingItemRequests;
	};
}
