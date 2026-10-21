// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/inventory.h"
#include "game_server/inventory_repository.h"
#include "game_server/inventory_types.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

namespace
{
	/// Counts the snapshots that reach the repository, so a resend can be told apart from a skipped save.
	class CountingInventoryRepository final : public IInventoryRepository
	{
	public:
		std::vector<InventoryItemData> LoadItems(uint64 characterId) override { return m_storage.LoadItems(characterId); }

		bool SaveItem(const uint64 characterId, const InventoryItemData& item) override { return m_storage.SaveItem(characterId, item); }

		bool SaveAllItems(const uint64 characterId, const std::vector<InventoryItemData>& items) override
		{
			++snapshots;
			return m_storage.SaveAllItems(characterId, items);
		}

		bool DeleteItem(const uint64 characterId, const uint16 slot) override { return m_storage.DeleteItem(characterId, slot); }

		bool DeleteAllItems(const uint64 characterId) override { return m_storage.DeleteAllItems(characterId); }

		bool BeginTransaction() override { return m_storage.BeginTransaction(); }

		bool Commit() override { return m_storage.Commit(); }

		bool Rollback() override { return m_storage.Rollback(); }

		size_t GetItemCount(const uint64 characterId) const noexcept { return m_storage.GetItemCount(characterId); }

		uint32 snapshots = 0;

	private:
		InMemoryInventoryRepository m_storage;
	};
}

TEST_CASE("A failed inventory save is resent in full on the next save", "[inventory]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* entry = project.items.add(801);
	entry->set_name("Linen Shirt");
	entry->set_maxstack(1);

	auto player = test::MakeUnit(project, timers);
	Inventory& inventory = player->GetInventory();

	CountingInventoryRepository repository;
	inventory.SetRepository(&repository);

	auto item = std::make_shared<GameItemS>(project, *entry);
	item->Initialize();
	item->Set<uint64>(object_fields::Guid, CreateEntryGUID(1, entry->id(), GuidType::Item));
	inventory.AddItemToSlot(item, InventorySlot::FromRelative(player_inventory_slots::Bag_0, player_inventory_pack_slots::Start).GetAbsolute());
	inventory.MarkDirty();

	// The world side treats a sent snapshot as saved: the realm only reports back later.
	REQUIRE(inventory.SaveToRepository());
	CHECK(repository.snapshots == 1);
	CHECK_FALSE(inventory.IsDirty());

	// Nothing changed locally, so a periodic save sends nothing.
	REQUIRE(inventory.SaveToRepository());
	CHECK(repository.snapshots == 1);

	// The realm reported the snapshot as failed (RealmConnector::OnInventoryOperationResult).
	repository.DeleteAllItems(player->GetGuid());
	inventory.MarkDirty();
	CHECK(inventory.IsDirty());

	// The next save resends the whole inventory, although the inventory itself did not change.
	REQUIRE(inventory.SaveToRepository());
	CHECK(repository.snapshots == 2);
	CHECK(repository.GetItemCount(player->GetGuid()) == 1);
	CHECK_FALSE(inventory.IsDirty());
}
