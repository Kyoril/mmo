#pragma once

#include "client_cache.h"
#include "base/non_copyable.h"
#include "client_data/project.h"
#include "game/action_button.h"

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
		explicit ActionBar(RealmConnector& realmConnector, const proto_client::SpellManager& spells, DBItemCache& items);
		~ActionBar() override = default;

	public:
		const ActionButton& GetActionButton(int32 slot) const;

		[[nodiscard]] bool IsActionButtonUsable(int32 slot) const;

		[[nodiscard]] bool IsActionButtonSpell(int32 slot) const;

		[[nodiscard]] bool IsActionButtonItem(int32 slot) const;

		[[nodiscard]] const proto_client::SpellEntry* GetActionButtonSpell(int32 slot) const;

		[[nodiscard]] const ItemInfo* GetActionButtonItem(int32 slot) const;

		void UseActionButton(int32 slot);

	private:
		static bool IsValidSlot(int32 slot);

	private:
		RealmConnector& m_connector;
		const proto_client::SpellManager& m_spells;
		DBItemCache& m_items;
		ActionButtons m_actionButtons;
	};
}
