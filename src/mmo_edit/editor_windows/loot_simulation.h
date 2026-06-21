// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/utilities.h"
#include "game/item.h"
#include "proto_data/project.h"

#include <imgui.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mmo
{
	/// @brief Aggregated outcome of simulating one or more loot tables a number of times.
	///
	/// Used by the editor to estimate loot rarity by rolling loot tables many times and
	/// counting how often each item drops. This mirrors the server-side loot generation in
	/// LootInstance so the numbers reflect actual in-game drop behavior.
	struct LootSimulationResult
	{
		/// Per-item statistics accumulated over all simulated runs.
		struct ItemStat
		{
			uint32 itemId = 0;
			/// Number of runs that dropped this item at least once.
			uint32 runsWithDrop = 0;
			/// Total number of individual drop events for this item (an item can drop from
			/// multiple groups within a single run).
			uint32 dropEvents = 0;
			/// Summed item count across all drop events.
			uint64 totalQuantity = 0;
		};

		uint32 runs = 0;
		uint64 totalGold = 0;
		std::vector<ItemStat> items;
	};

	/// @brief Persistent UI state for the loot simulation widget, owned by a window.
	struct LootSimulationState
	{
		int runs = 1;
		bool hasResult = false;
		LootSimulationResult result;
	};

	namespace detail
	{
		/// Replicates LootInstance::GenerateItemsFromEntry for a single run, accumulating drops.
		/// Condition-gated items are treated as eligible since the editor has no player context.
		inline void SimulateLootEntry(const proto::ItemManager& items, const proto::LootEntry& entry,
			std::unordered_map<uint32, LootSimulationResult::ItemStat>& accumulator,
			std::unordered_set<uint32>& droppedThisRun)
		{
			for (int g = 0; g < entry.groups_size(); ++g)
			{
				const auto& group = entry.groups(g);

				std::uniform_real_distribution<float> lootDistribution(0.0f, 100.0f);
				const float groupRoll = lootDistribution(randomGenerator);

				std::vector<const proto::LootDefinition*> equalChanced;
				std::vector<const proto::LootDefinition*> nonEqualChanced;
				for (int d = 0; d < group.definitions_size(); ++d)
				{
					const auto& def = group.definitions(d);

					if (def.dropchance() == 0.0f)
					{
						equalChanced.push_back(&def);
						continue;
					}

					if (def.dropchance() > 0.0f && def.dropchance() >= groupRoll)
					{
						nonEqualChanced.push_back(&def);
					}
				}

				const proto::LootDefinition* chosen = nullptr;
				if (nonEqualChanced.empty() && !equalChanced.empty())
				{
					std::uniform_int_distribution<size_t> dist(0, equalChanced.size() - 1);
					chosen = equalChanced[dist(randomGenerator)];
				}
				else if (!nonEqualChanced.empty())
				{
					std::uniform_int_distribution<size_t> dist(0, nonEqualChanced.size() - 1);
					chosen = nonEqualChanced[dist(randomGenerator)];
				}

				if (!chosen)
				{
					continue;
				}

				// Determine the dropped count, mirroring LootInstance::AddLootItem.
				uint32 dropCount = chosen->mincount();
				if (chosen->maxcount() > chosen->mincount())
				{
					std::uniform_int_distribution<uint32> countDist(chosen->mincount(), chosen->maxcount());
					dropCount = countDist(randomGenerator);
				}

				if (const auto* lootItem = items.getById(chosen->item()))
				{
					if (lootItem->maxstack() > 0 && dropCount > lootItem->maxstack())
					{
						dropCount = lootItem->maxstack();
					}
				}

				if (dropCount == 0)
				{
					dropCount = 1;
				}

				auto& stat = accumulator[chosen->item()];
				stat.itemId = chosen->item();
				stat.dropEvents += 1;
				stat.totalQuantity += dropCount;
				droppedThisRun.insert(chosen->item());
			}
		}
	}

	/// @brief Simulates loot generation for a set of loot tables a number of times.
	/// @param project The data project, used to resolve item stack sizes.
	/// @param entries The loot tables to roll. Each is rolled independently per run, just like in-game.
	/// @param runs Number of simulated loot generations.
	/// @return Aggregated drop statistics sorted by how often each item dropped.
	inline LootSimulationResult SimulateLoot(const proto::Project& project,
		const std::vector<const proto::LootEntry*>& entries, const uint32 runs)
	{
		LootSimulationResult result;
		result.runs = runs;

		std::unordered_map<uint32, LootSimulationResult::ItemStat> accumulator;
		std::unordered_set<uint32> droppedThisRun;

		for (uint32 run = 0; run < runs; ++run)
		{
			droppedThisRun.clear();

			for (const auto* entry : entries)
			{
				if (!entry)
				{
					continue;
				}

				detail::SimulateLootEntry(project.items, *entry, accumulator, droppedThisRun);

				const uint32 minMoney = std::min(entry->minmoney(), entry->maxmoney());
				const uint32 maxMoney = std::max(entry->minmoney(), entry->maxmoney());
				if (maxMoney > 0)
				{
					std::uniform_int_distribution<uint32> goldDistribution(minMoney, maxMoney);
					result.totalGold += goldDistribution(randomGenerator);
				}
			}

			for (const uint32 itemId : droppedThisRun)
			{
				accumulator[itemId].runsWithDrop += 1;
			}
		}

		result.items.reserve(accumulator.size());
		for (const auto& [id, stat] : accumulator)
		{
			result.items.push_back(stat);
		}

		std::sort(result.items.begin(), result.items.end(),
			[](const LootSimulationResult::ItemStat& a, const LootSimulationResult::ItemStat& b)
			{
				if (a.runsWithDrop != b.runsWithDrop)
				{
					return a.runsWithDrop > b.runsWithDrop;
				}
				return a.dropEvents > b.dropEvents;
			});

		return result;
	}

	namespace detail
	{
		/// Maps an item quality to a display color matching the in-game rarity colors.
		inline ImVec4 GetItemQualityColor(const uint32 quality)
		{
			switch (quality)
			{
			case item_quality::Poor:		return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
			case item_quality::Common:		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			case item_quality::Uncommon:	return ImVec4(0.12f, 1.0f, 0.0f, 1.0f);
			case item_quality::Rare:		return ImVec4(0.0f, 0.44f, 0.87f, 1.0f);
			case item_quality::Epic:		return ImVec4(0.64f, 0.21f, 0.93f, 1.0f);
			case item_quality::Legendary:	return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
			default:						return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
	}

	/// @brief Draws the loot simulation widget: a button that opens a popup to roll loot tables
	///        a configurable number of times and view aggregated drop statistics.
	/// @param project The data project, used to resolve item names and quality colors.
	/// @param entries The loot tables to simulate together (rolled independently each run).
	/// @param state Persistent widget state owned by the calling window.
	/// @param buttonLabel Label of the button that opens the simulation popup.
	/// @param popupId Unique ImGui popup identifier (must differ between call sites).
	inline void DrawLootSimulationUI(const proto::Project& project,
		const std::vector<const proto::LootEntry*>& entries, LootSimulationState& state,
		const char* buttonLabel = "Simulate Loot", const char* popupId = "LootSimulationPopup")
	{
		const bool hasTables = !entries.empty();

		if (!hasTables)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Button(buttonLabel))
		{
			ImGui::OpenPopup(popupId);
		}
		if (!hasTables)
		{
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("No loot tables assigned to simulate.");
			}
		}

		if (!ImGui::BeginPopup(popupId))
		{
			return;
		}

		ImGui::TextUnformatted("Loot Simulation");
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
			"Rolls %d loot table(s) together. Item conditions are ignored.", static_cast<int>(entries.size()));
		ImGui::Separator();

		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::InputInt("Iterations", &state.runs))
		{
			if (state.runs < 1)
			{
				state.runs = 1;
			}
			if (state.runs > 1000000)
			{
				state.runs = 1000000;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Run Simulation"))
		{
			state.result = SimulateLoot(project, entries, static_cast<uint32>(state.runs));
			state.hasResult = true;
		}

		if (state.hasResult)
		{
			const auto& result = state.result;

			ImGui::Separator();
			ImGui::Text("Results over %u iteration(s):", result.runs);

			// Average gold per run, formatted as gold / silver / copper.
			const double avgGold = result.runs > 0 ? static_cast<double>(result.totalGold) / result.runs : 0.0;
			const int64 avgGoldInt = static_cast<int64>(avgGold);
			const int64 gold = avgGoldInt / 10000;
			const int64 silver = (avgGoldInt % 10000) / 100;
			const int64 copper = avgGoldInt % 100;
			ImGui::Text("Avg. money / run:");
			ImGui::SameLine();
			if (gold > 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.0f, 1.0f), "%lld g", static_cast<long long>(gold));
				ImGui::SameLine();
			}
			ImGui::TextColored(ImVec4(0.83f, 0.83f, 0.83f, 1.0f), "%lld s", static_cast<long long>(silver));
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.0f, 1.0f), "%lld c", static_cast<long long>(copper));

			if (result.items.empty())
			{
				ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "No items dropped.");
			}
			else if (ImGui::BeginTable("lootSimResults", 5,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings,
				ImVec2(560.0f, 320.0f)))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Drop Rate", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("Total Drops", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("Avg Qty/Run", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("Total Qty", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableHeadersRow();

				for (const auto& stat : result.items)
				{
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					const auto* itemEntry = project.items.getById(stat.itemId);
					if (itemEntry != nullptr)
					{
						ImGui::TextColored(detail::GetItemQualityColor(itemEntry->quality()), "%s", itemEntry->name().c_str());
					}
					else
					{
						ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "<Unknown item %u>", stat.itemId);
					}

					ImGui::TableNextColumn();
					const double dropRate = result.runs > 0 ? (100.0 * stat.runsWithDrop / result.runs) : 0.0;
					ImGui::Text("%.2f%%", dropRate);

					ImGui::TableNextColumn();
					ImGui::Text("%u", stat.dropEvents);

					ImGui::TableNextColumn();
					const double avgQty = result.runs > 0 ? static_cast<double>(stat.totalQuantity) / result.runs : 0.0;
					ImGui::Text("%.3f", avgQty);

					ImGui::TableNextColumn();
					ImGui::Text("%llu", static_cast<unsigned long long>(stat.totalQuantity));
				}

				ImGui::EndTable();
			}
		}

		ImGui::EndPopup();
	}
}
