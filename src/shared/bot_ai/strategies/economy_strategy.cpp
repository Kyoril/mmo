// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "economy_strategy.h"

#include "bot_ai/bot_action.h"
#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_gear.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_strategy.h"
#include "bot_ai/bot_trigger.h"
#include "bot_ai/npc_index.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_inventory.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_nav_service.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "game/item.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace mmo
{
	namespace
	{
		/// After an equip the server refused, the item is not tried again; after one it took, the
		/// next upgrade waits this long, so a bot does not shuffle items every tick while the
		/// inventory update is on its way.
		constexpr GameTime EquipSettleMs = 1500;

		/// How long after a failed or finished trip the bot considers selling again.
		constexpr GameTime SellRetryMs = 60 * 1000;
		constexpr GameTime SellUnreachableRetryMs = 5 * 60 * 1000;

		class EconomyTrigger final : public BotTrigger
		{
		public:
			using Predicate = std::function<bool(BotAiContext&, BotSession&)>;

			EconomyTrigger(std::string name, Predicate predicate, const GameTime intervalMs = 0)
				: BotTrigger(std::move(name), intervalMs)
				, m_predicate(std::move(predicate))
			{
			}

			[[nodiscard]] bool IsActive(BotAiContext& context) const override
			{
				BotSession* session = context.GetSession();
				return session && context.GetPerception().valid && context.GetPerception().alive
					&& m_predicate(context, *session);
			}

		private:
			Predicate m_predicate;
		};

		class EconomyAction final : public BotAction
		{
		public:
			using Body = std::function<bool(BotAiContext&, BotSession&)>;

			EconomyAction(std::string name, Body body)
				: BotAction(std::move(name))
				, m_body(std::move(body))
			{
			}

			bool Execute(BotAiContext& context) override
			{
				BotSession* session = context.GetSession();
				return session && m_body(context, *session);
			}

		private:
			Body m_body;
		};

		class EconomyStrategy final : public BotStrategy
		{
		public:
			EconomyStrategy(std::string name, std::vector<BotTriggerNode> nodes)
				: BotStrategy(std::move(name))
				, m_nodes(std::move(nodes))
			{
			}

			[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override { return m_nodes; }

		private:
			std::vector<BotTriggerNode> m_nodes;
		};

		const proto::Project* projectOf(BotSession& session)
		{
			const BotNavService* nav = session.GetContext().GetNavService();
			return nav ? nav->GetProject() : nullptr;
		}

		/// The bot's active class and level, from its own unit.
		bool selfClassAndLevel(BotSession& session, uint32& outClass, uint32& outLevel)
		{
			const BotUnit* self = session.GetContext().GetSelf();
			if (!self)
			{
				return false;
			}

			outClass = self->GetClassId();
			outLevel = self->GetLevel();
			return true;
		}

		struct Upgrade
		{
			BotInventoryItem item;
			float gain { 0.0f };
		};

		/// The best item in the bags worth putting on: an upgrade over what is in its slot, or a
		/// bag for an empty bag slot.
		std::optional<Upgrade> findUpgrade(const BotAiContext& context, BotSession& session, const BotInventory& inventory)
		{
			const proto::Project* project = projectOf(session);
			uint32 classId = 0;
			uint32 level = 0;
			if (!project || !selfClassAndLevel(session, classId, level))
			{
				return std::nullopt;
			}

			const BotEconomyState& economy = context.GetEconomyState();
			bool freeBagSlot = false;
			for (uint8 slot = player_inventory_slots::Start; slot < player_inventory_slots::End; ++slot)
			{
				freeBagSlot = freeBagSlot || session.GetContext().GetObjectManager().GetSelfInventorySlot(slot) == 0;
			}

			std::optional<Upgrade> best;
			for (const BotInventoryItem& candidate : inventory.GetItems())
			{
				if (candidate.equipped || economy.unusableItems.count(candidate.entry) != 0)
				{
					continue;
				}

				const proto::ItemEntry* entry = project->items.getById(candidate.entry);
				if (!entry || entry->requiredlevel() > level)
				{
					continue;
				}

				// A bag goes into a free bag slot: more room is always an upgrade.
				if (entry->inventorytype() == inventory_type::Bag)
				{
					if (freeBagSlot && candidate.bag == player_inventory_slots::Bag_0)
					{
						const float gain = 1000.0f + static_cast<float>(entry->containerslots());
						if (!best || gain > best->gain)
						{
							best = Upgrade { candidate, gain };
						}
					}
					continue;
				}

				const std::vector<uint8> slots = EquipmentSlotsFor(entry->inventorytype());
				if (slots.empty())
				{
					continue;
				}

				const float score = ScoreItem(*entry, classId);

				// The weakest of the slots it fits: a ring replaces the worse of the two rings.
				float worstEquipped = -1.0f;
				for (const uint8 slot : slots)
				{
					const BotInventoryItem* equipped = inventory.GetEquipped(slot);
					const proto::ItemEntry* equippedEntry = equipped ? project->items.getById(equipped->entry) : nullptr;
					const float equippedScore = equippedEntry ? ScoreItem(*equippedEntry, classId) : 0.0f;
					if (worstEquipped < 0.0f || equippedScore < worstEquipped)
					{
						worstEquipped = equippedScore;
					}
				}

				const float gain = score - worstEquipped;
				if (gain > 0.5f && (!best || gain > best->gain))
				{
					best = Upgrade { candidate, gain };
				}
			}

			return best;
		}

		/// Whether the bot would part with an item at a vendor: junk, and gear it does not wear
		/// that is no better than green. Bags, consumables, cloth, reagents, quest items and
		/// anything blue or better are kept.
		bool isSellable(const BotAiContext& context, BotSession& session, const BotInventoryItem& item)
		{
			const proto::Project* project = projectOf(session);
			const proto::ItemEntry* entry = project ? project->items.getById(item.entry) : nullptr;
			if (item.equipped || !entry || entry->sellprice() == 0)
			{
				return false;
			}

			if (IsJunk(*entry))
			{
				return true;
			}

			if (!IsWearable(*entry) || entry->quality() > item_quality::Uncommon)
			{
				return false;
			}

			// Gear left in the bags is not an upgrade, or it would have been put on; unless it is
			// only waiting for a level, in which case it is kept.
			const BotUnit* self = session.GetContext().GetSelf();
			const bool waitingForLevel = self && entry->requiredlevel() > self->GetLevel();
			return !waitingForLevel || context.GetEconomyState().unusableItems.count(item.entry) != 0;
		}

		bool hasSellable(const BotAiContext& context, BotSession& session, const BotInventory& inventory)
		{
			for (const BotInventoryItem& item : inventory.GetItems())
			{
				if (isSellable(context, session, item))
				{
					return true;
				}
			}

			return false;
		}
	}

	void RegisterEconomyStrategies(BotAiRegistry& registry)
	{
		const auto addTrigger = [&registry](std::string name, EconomyTrigger::Predicate predicate, const GameTime intervalMs = 0)
		{
			registry.RegisterTrigger(std::make_unique<EconomyTrigger>(std::move(name), std::move(predicate), intervalMs));
		};

		const auto addAction = [&registry](std::string name, EconomyAction::Body body)
		{
			registry.RegisterAction(std::make_unique<EconomyAction>(std::move(name), std::move(body)));
		};

		addTrigger("lootable_corpse_nearby", [](BotAiContext& context, BotSession&)
			{
				const BotPerception& perception = context.GetPerception();
				return perception.nearestLootableGuid != 0 && perception.freeBagSlots > 0 && !perception.IsInCombat();
			});

		addTrigger("gear_upgrade_in_bags", [](BotAiContext& context, BotSession& session)
			{
				const BotEconomyState& economy = context.GetEconomyState();
				if (context.GetPerception().IsInCombat() || context.GetNow() < economy.lastEquipMs + EquipSettleMs)
				{
					return false;
				}

				const BotInventory inventory = BotInventory::Snapshot(session.GetContext().GetObjectManager());
				return findUpgrade(context, session, inventory).has_value();
			}, 2000);

		addTrigger("bags_nearly_full", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				BotEconomyState& economy = context.GetEconomyState();

				// A trip under way is finished, whatever the bags say now.
				if (economy.selling)
				{
					return true;
				}

				if (perception.IsInCombat() || context.GetFollowState().IsFollowing() || !context.GetNpcIndex()
					|| perception.totalBagSlots == 0 || perception.freeBagSlots > BotSellAtFreeSlots
					|| context.GetNow() < economy.nextSellAttemptMs)
				{
					return false;
				}

				const BotInventory inventory = BotInventory::Snapshot(session.GetContext().GetObjectManager());
				return hasSellable(context, session, inventory);
			}, 1000);

		addAction("loot_corpse", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				BotEconomyState& economy = context.GetEconomyState();
				BotContext& world = session.GetContext();

				const uint64 corpseGuid = perception.nearestLootableGuid;
				const BotUnit* corpse = world.GetUnit(corpseGuid);
				if (!corpse)
				{
					return false;
				}

				// Asked already: wait for the answer, or give up on a corpse that never sends one.
				if (economy.pendingLootGuid == corpseGuid)
				{
					if (context.GetNow() - economy.pendingLootMs < BotLootTimeoutMs)
					{
						return true;
					}

					economy.lootedCorpses[corpseGuid] = context.GetNow();
					economy.pendingLootGuid = 0;
					return false;
				}

				const Vector3 corpsePosition = corpse->GetPosition();
				BotMovementController& mover = session.GetMovementController();
				if (PlanarDistance(world.GetPosition(), corpsePosition) > BotLootRange
					|| std::abs(world.GetPosition().y - corpsePosition.y) > 2.5f)
				{
					if (mover.IsActive())
					{
						return true;
					}

					if (!mover.MoveTo(world, corpsePosition, BotLootRange * 0.7f))
					{
						// Out of reach: not worth another try.
						economy.lootedCorpses[corpseGuid] = context.GetNow();
						return false;
					}

					return true;
				}

				if (mover.IsActive())
				{
					mover.Stop(world, "loot");
				}

				session.GetRealm().Loot(corpseGuid);
				economy.pendingLootGuid = corpseGuid;
				economy.pendingLootMs = context.GetNow();
				return true;
			});

		addAction("equip_upgrade", [](BotAiContext& context, BotSession& session)
			{
				const BotInventory inventory = BotInventory::Snapshot(session.GetContext().GetObjectManager());
				const std::optional<Upgrade> upgrade = findUpgrade(context, session, inventory);
				if (!upgrade)
				{
					return false;
				}

				BotEconomyState& economy = context.GetEconomyState();
				session.GetRealm().AutoEquipItem(upgrade->item.bag, upgrade->item.slot);
				economy.lastEquipEntry = upgrade->item.entry;
				economy.lastEquipMs = context.GetNow();
				return true;
			});

		addAction("sell_at_vendor", [](BotAiContext& context, BotSession& session)
			{
				BotEconomyState& economy = context.GetEconomyState();
				BotContext& world = session.GetContext();
				const NpcIndex* npcs = context.GetNpcIndex();
				const BotPerception& perception = context.GetPerception();

				const auto giveUp = [&economy, &context](const GameTime retryMs, const char* reason = nullptr)
				{
					if (reason)
					{
						DLOG("Bot " << context.GetBotIndex() << " gives up selling: " << reason);
					}

					economy.selling = false;
					economy.nextSellAttemptMs = context.GetNow() + retryMs;
					return false;
				};

				if (!economy.selling)
				{
					const std::optional<BotNpcSpot> vendor = npcs ? npcs->FindNearestVendor(perception.mapId, perception.position) : std::nullopt;
					if (!vendor)
					{
						return giveUp(SellUnreachableRetryMs, "no vendor on this map");
					}

					economy.selling = true;
					economy.vendorMapId = vendor->mapId;
					economy.vendorEntry = vendor->unitEntry;
					economy.vendorPosition = vendor->position;
					economy.sellingSinceMs = context.GetNow();

					// Whatever the bot was grinding is not where it is going now.
					context.GetGrindState().ClearSpot();
				}

				if (perception.mapId != economy.vendorMapId || context.GetNow() - economy.sellingSinceMs > BotSellTripTimeoutMs)
				{
					return giveUp(SellUnreachableRetryMs, "trip took too long");
				}

				BotMovementController& mover = session.GetMovementController();
				if (PlanarDistance(world.GetPosition(), economy.vendorPosition) > BotVendorApproachRange + 1.0f)
				{
					if (mover.IsActive())
					{
						return true;
					}

					if (!mover.MoveTo(world, economy.vendorPosition, BotVendorApproachRange))
					{
						return giveUp(SellUnreachableRetryMs, "no path to the vendor");
					}

					return true;
				}

				// There: find the vendor itself, which may have wandered a little from its spawn.
				const BotUnit* vendor = nullptr;
				for (const BotUnit* unit : world.GetNearbyUnits(BotVendorSearchRange))
				{
					if (unit->GetEntry() == economy.vendorEntry && unit->IsAlive())
					{
						vendor = unit;
						break;
					}
				}

				if (!vendor)
				{
					return giveUp(SellUnreachableRetryMs, "vendor not at its spawn");
				}

				if (PlanarDistance(world.GetPosition(), vendor->GetPosition()) > BotVendorApproachRange + 1.0f)
				{
					if (!mover.IsActive() && !mover.MoveTo(world, vendor->GetPosition(), BotVendorApproachRange))
					{
						return giveUp(SellUnreachableRetryMs, "no path to the vendor itself");
					}

					return true;
				}

				const BotInventory inventory = BotInventory::Snapshot(world.GetObjectManager());
				uint32 sold = 0;
				for (const BotInventoryItem& item : inventory.GetItems())
				{
					if (isSellable(context, session, item))
					{
						session.GetRealm().SellItem(vendor->GetGuid(), item.guid);
						++sold;
					}
				}

				ILOG("Bot " << context.GetBotIndex() << " sold " << sold << " items to vendor " << economy.vendorEntry);

				giveUp(SellRetryMs);
				return true;
			});

		// Out of combat, over the grind or party strategy. Selling outranks travelling to a grind
		// spot, or a bot with full bags would never leave for the vendor; looting outranks picking
		// the next fight, so corpses are emptied before the bot moves on.
		registry.RegisterStrategy(std::make_unique<EconomyStrategy>("economy_world", std::vector<BotTriggerNode> {
			{ "bags_nearly_full", { { "sell_at_vendor", bot_relevance::Move + 2 } } },
			{ "gear_upgrade_in_bags", { { "equip_upgrade", bot_relevance::High + 5 } } },
			{ "lootable_corpse_nearby", { { "loot_corpse", bot_relevance::Normal + 3 } } },
		}));
	}
}
