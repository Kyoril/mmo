# Spell/Item Editor List Filters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add property-based filters (item class/subclass/quality/inventory type/stat type/level; spell effect/aura/kind/class/race/level) to the entry-list panel of the item and spell editors in mmo_edit.

**Architecture:** The shared template base `EditorEntryWindowBase<T1,T2>` gains filter hook virtuals (`HasFilters`, `DrawFilters`, `MatchesFilters`, `ActiveFilterCount`, `ResetFilters`) and renders a collapsible "Filters" panel between the search bar and the entry list. `ItemEditorWindow` and `SpellEditorWindow` override the hooks with member-variable filter state. Spec: `docs/superpowers/specs/2026-07-26-editor-list-filters-design.md`.

**Tech Stack:** C++17, Dear ImGui (immediate mode), protobuf-generated data classes. No test framework covers editor windows — each task is verified by compiling the `mmo_edit` target; final verification is a manual editor smoke test.

## Global Constraints

- Code style: Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` (all touched files already have it).
- No exceptions; use `ASSERT`/`DLOG` from `base/macros.h` if needed.
- Filter state must be member variables, never `static` locals.
- All filters AND-combine; every dropdown defaults to "Any" (`-1`), level bounds default to `0` = unbounded.
- Build command: `cmake --build build -t mmo_edit --config Debug` — must end with `0 errors`.
- **Deviation from spec (intentional):** `DrawFilters()` returns `void`, not `bool`. The base filters every frame (see Task 2 cleanup), so a change flag is dead weight. Task 1 amends the spec accordingly.

---

### Task 1: Feature branch + spec commit

**Files:**
- Modify: `docs/superpowers/specs/2026-07-26-editor-list-filters-design.md` (already exists uncommitted; amend `DrawFilters` signature)

**Interfaces:** none (setup task).

- [ ] **Step 1: Create the branch**

```bash
git checkout -b feature/editor-list-filters
```

Expected: `Switched to a new branch 'feature/editor-list-filters'`

- [ ] **Step 2: Amend the spec's DrawFilters signature**

In `docs/superpowers/specs/2026-07-26-editor-list-filters-design.md`, replace:

```markdown
- `virtual bool DrawFilters()` — renders the editor-specific filter widgets.
  Returns `true` when any filter value changed this frame. Default: renders
  nothing, returns `false`.
```

with:

```markdown
- `virtual void DrawFilters()` — renders the editor-specific filter widgets.
  Default: renders nothing. (The base recomputes the filtered list every
  frame, so no change flag is needed.)
- `virtual bool HasFilters() const` — whether this window provides filters;
  gates rendering of the Filters panel so unrelated editors are visually
  unchanged. Default: `false`.
```

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-07-26-editor-list-filters-design.md
git commit -m "Add design spec for spell/item editor list filters"
```

---

### Task 2: Filter hooks in EditorEntryWindowBase

**Files:**
- Modify: `src/mmo_edit/editor_windows/editor_entry_window_base.h`

**Interfaces:**
- Produces (used by Tasks 3 and 4 — exact signatures):
  - `virtual bool HasFilters() const { return false; }`
  - `virtual void DrawFilters() {}`
  - `virtual bool MatchesFilters(const T2& entry) const { return true; }`
  - `virtual int ActiveFilterCount() const { return 0; }`
  - `virtual void ResetFilters() {}`
  All `protected` on `EditorEntryWindowBase<T1, T2>`.

- [ ] **Step 1: Add the filter hook virtuals**

In the `protected:` section of the class declaration (after `SupportsDuplicate()`, before `DuplicateSelectedEntry()`), add:

```cpp
		/// @brief Whether this window provides additional list filters. Gates the Filters panel.
		virtual bool HasFilters() const { return false; }

		/// @brief Draws the editor-specific filter widgets inside the Filters panel.
		virtual void DrawFilters() {}

		/// @brief Returns true if the given entry passes the editor-specific filters.
		virtual bool MatchesFilters(const T2 &entry) const { return true; }

		/// @brief Number of filters currently not in their default state (badge in the panel header).
		virtual int ActiveFilterCount() const { return 0; }

		/// @brief Resets all editor-specific filters to their default state.
		virtual void ResetFilters() {}
```

- [ ] **Step 2: Render the Filters panel and collapse the dual-branch filter loop**

In `Draw()`, replace everything from the comment `// Search bar with clear button` down to (and including) the closing brace of the `else` branch that ends just before `// Show entry count` (currently lines ~137–238) with:

```cpp
				// Search bar with clear button
				static char searchBuffer[256] = "";

				ImGui::SetNextItemWidth(-38);
				ImGui::InputTextWithHint("##search", "Search by name or ID...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

				ImGui::SameLine();
				if (strlen(searchBuffer) > 0)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.3f, 0.8f));
					if (ImGui::Button("X", ImVec2(30, 0)))
					{
						searchBuffer[0] = '\0';
					}
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::BeginDisabled(true);
					ImGui::Button("X", ImVec2(30, 0));
					ImGui::EndDisabled();
				}

				// Editor-specific filter panel
				if (HasFilters())
				{
					char filterHeader[64];
					const int activeFilters = ActiveFilterCount();
					if (activeFilters > 0)
					{
						snprintf(filterHeader, sizeof(filterHeader), "Filters (%d active)###entryFilters", activeFilters);
					}
					else
					{
						snprintf(filterHeader, sizeof(filterHeader), "Filters###entryFilters");
					}

					if (ImGui::CollapsingHeader(filterHeader))
					{
						DrawFilters();

						ImGui::BeginDisabled(activeFilters == 0);
						if (ImGui::Button("Reset Filters", ImVec2(-1, 0)))
						{
							ResetFilters();
						}
						ImGui::EndDisabled();
					}
				}

				// Filter the entry list by search text and editor-specific filters.
				// Entry counts are small enough that a full pass per frame is fine.
				std::string lowerSearchText = searchBuffer;
				std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
							   [](unsigned char c)
							   { return std::tolower(c); });

				std::vector<int> filteredIndices;
				for (int i = 0; i < m_manager.count(); ++i)
				{
					const auto &entry = m_manager.getTemplates().entry().at(i);

					if (!MatchesFilters(entry))
					{
						continue;
					}

					if (!lowerSearchText.empty())
					{
						std::string lowerEntryName = EntryDisplayName(entry).c_str();
						std::transform(lowerEntryName.begin(), lowerEntryName.end(), lowerEntryName.begin(),
									   [](unsigned char c)
									   { return std::tolower(c); });

						const std::string idStr = std::to_string(entry.id());
						if (lowerEntryName.find(lowerSearchText) == std::string::npos &&
							idStr.find(lowerSearchText) == std::string::npos)
						{
							continue;
						}
					}

					filteredIndices.push_back(i);
				}
```

This deletes the `searchChanged` / `lastSearchText` / `lastEntryCount` caching (both of its branches recomputed the full list every frame anyway). Everything from `// Show entry count` onward stays unchanged.

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: builds with 0 errors (all other entry editors compile against the new virtuals via defaults).

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editor_windows/editor_entry_window_base.h
git commit -m "Add per-editor filter hooks to entry window list panel"
```

---

### Task 3: Item editor filters

**Files:**
- Modify: `src/mmo_edit/editor_windows/item_editor_window.h`
- Modify: `src/mmo_edit/editor_windows/item_editor_window.cpp`

**Interfaces:**
- Consumes: the Task 2 virtuals; file-scope tables in item_editor_window.cpp: `s_itemQualityStrings` (vector<String>), `s_inventoryTypeStrings` (vector<String>), `s_statTypeStrings` (const char*[]); data-driven `m_project.itemClasses` / `m_project.itemSubclasses` template managers (each subclass entry has `itemclass()`).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Declare overrides and filter state in the header**

In `item_editor_window.h`, in the `private:` section after `void ImportItemFromFile(EntryType& entry);`, add:

```cpp
		bool HasFilters() const override { return true; }

		void DrawFilters() override;

		bool MatchesFilters(const EntryType& entry) const override;

		int ActiveFilterCount() const override;

		void ResetFilters() override;
```

And in the member section at the bottom (after `m_openImportExportPopup`), add:

```cpp
		/// List filter state: entry ids / enum indices, -1 = Any.
		int m_filterItemClass = -1;
		int m_filterSubclass = -1;
		int m_filterQuality = -1;
		int m_filterInventoryType = -1;
		int m_filterStatType = -1;
		/// Required-level bounds, 0 = unbounded.
		int m_filterMinRequiredLevel = 0;
		int m_filterMaxRequiredLevel = 0;
```

- [ ] **Step 2: Implement the overrides in the .cpp**

Add to `item_editor_window.cpp` (inside `namespace mmo`, near the other `ItemEditorWindow` methods — e.g. directly before `DrawDetailsImpl`):

```cpp
	void ItemEditorWindow::DrawFilters()
	{
		// Item class (data-driven), narrows the subclass dropdown below
		const auto* filterClassEntry = m_filterItemClass >= 0 ? m_project.itemClasses.getById(m_filterItemClass) : nullptr;
		if (ImGui::BeginCombo("Class##listFilter", filterClassEntry != nullptr ? filterClassEntry->name().c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterItemClass < 0))
			{
				m_filterItemClass = -1;
				m_filterSubclass = -1;
			}
			for (int i = 0; i < m_project.itemClasses.count(); ++i)
			{
				const auto& entry = m_project.itemClasses.getTemplates().entry(i);
				ImGui::PushID(i);
				if (ImGui::Selectable(entry.name().c_str(), m_filterItemClass == static_cast<int>(entry.id())))
				{
					if (m_filterItemClass != static_cast<int>(entry.id()))
					{
						// Subclass filter may no longer be valid for the new class
						m_filterSubclass = -1;
					}
					m_filterItemClass = static_cast<int>(entry.id());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		// Subclass, only offered once a class is chosen
		ImGui::BeginDisabled(m_filterItemClass < 0);
		const auto* filterSubclassEntry = m_filterSubclass >= 0 ? m_project.itemSubclasses.getById(m_filterSubclass) : nullptr;
		if (ImGui::BeginCombo("Subclass##listFilter", filterSubclassEntry != nullptr ? filterSubclassEntry->name().c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterSubclass < 0))
			{
				m_filterSubclass = -1;
			}
			for (int i = 0; i < m_project.itemSubclasses.count(); ++i)
			{
				const auto& subclass = m_project.itemSubclasses.getTemplates().entry(i);
				if (static_cast<int>(subclass.itemclass()) != m_filterItemClass)
				{
					continue;
				}
				ImGui::PushID(i);
				if (ImGui::Selectable(subclass.name().c_str(), m_filterSubclass == static_cast<int>(subclass.id())))
				{
					m_filterSubclass = static_cast<int>(subclass.id());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		// Quality
		if (ImGui::BeginCombo("Quality##listFilter", m_filterQuality >= 0 ? s_itemQualityStrings[m_filterQuality].c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterQuality < 0))
			{
				m_filterQuality = -1;
			}
			for (int i = 0; i < static_cast<int>(s_itemQualityStrings.size()); ++i)
			{
				if (ImGui::Selectable(s_itemQualityStrings[i].c_str(), m_filterQuality == i))
				{
					m_filterQuality = i;
				}
			}
			ImGui::EndCombo();
		}

		// Inventory type
		if (ImGui::BeginCombo("Inv. Type##listFilter", m_filterInventoryType >= 0 ? s_inventoryTypeStrings[m_filterInventoryType].c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterInventoryType < 0))
			{
				m_filterInventoryType = -1;
			}
			for (int i = 0; i < static_cast<int>(s_inventoryTypeStrings.size()); ++i)
			{
				if (ImGui::Selectable(s_inventoryTypeStrings[i].c_str(), m_filterInventoryType == i))
				{
					m_filterInventoryType = i;
				}
			}
			ImGui::EndCombo();
		}

		// Stat type (matches if any stat entry has this type)
		if (ImGui::BeginCombo("Stat##listFilter", m_filterStatType >= 0 ? s_statTypeStrings[m_filterStatType] : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterStatType < 0))
			{
				m_filterStatType = -1;
			}
			for (int i = 0; i < static_cast<int>(std::size(s_statTypeStrings)); ++i)
			{
				if (ImGui::Selectable(s_statTypeStrings[i], m_filterStatType == i))
				{
					m_filterStatType = i;
				}
			}
			ImGui::EndCombo();
		}

		// Required level bounds (0 = unbounded)
		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Min Req. Level##listFilter", &m_filterMinRequiredLevel))
		{
			m_filterMinRequiredLevel = std::max(0, m_filterMinRequiredLevel);
		}
		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Max Req. Level##listFilter", &m_filterMaxRequiredLevel))
		{
			m_filterMaxRequiredLevel = std::max(0, m_filterMaxRequiredLevel);
		}
	}

	bool ItemEditorWindow::MatchesFilters(const EntryType& entry) const
	{
		if (m_filterItemClass >= 0 && static_cast<int>(entry.itemclass()) != m_filterItemClass)
		{
			return false;
		}
		if (m_filterSubclass >= 0 && static_cast<int>(entry.subclass()) != m_filterSubclass)
		{
			return false;
		}
		if (m_filterQuality >= 0 && static_cast<int>(entry.quality()) != m_filterQuality)
		{
			return false;
		}
		if (m_filterInventoryType >= 0 && static_cast<int>(entry.inventorytype()) != m_filterInventoryType)
		{
			return false;
		}
		if (m_filterStatType >= 0)
		{
			bool hasStat = false;
			for (const auto& stat : entry.stats())
			{
				if (static_cast<int>(stat.type()) == m_filterStatType)
				{
					hasStat = true;
					break;
				}
			}
			if (!hasStat)
			{
				return false;
			}
		}
		if (m_filterMinRequiredLevel > 0 && static_cast<int>(entry.requiredlevel()) < m_filterMinRequiredLevel)
		{
			return false;
		}
		if (m_filterMaxRequiredLevel > 0 && static_cast<int>(entry.requiredlevel()) > m_filterMaxRequiredLevel)
		{
			return false;
		}
		return true;
	}

	int ItemEditorWindow::ActiveFilterCount() const
	{
		int count = 0;
		count += m_filterItemClass >= 0 ? 1 : 0;
		count += m_filterSubclass >= 0 ? 1 : 0;
		count += m_filterQuality >= 0 ? 1 : 0;
		count += m_filterInventoryType >= 0 ? 1 : 0;
		count += m_filterStatType >= 0 ? 1 : 0;
		count += m_filterMinRequiredLevel > 0 ? 1 : 0;
		count += m_filterMaxRequiredLevel > 0 ? 1 : 0;
		return count;
	}

	void ItemEditorWindow::ResetFilters()
	{
		m_filterItemClass = -1;
		m_filterSubclass = -1;
		m_filterQuality = -1;
		m_filterInventoryType = -1;
		m_filterStatType = -1;
		m_filterMinRequiredLevel = 0;
		m_filterMaxRequiredLevel = 0;
	}
```

Note: the implementations must live **below** the file-scope name tables (`s_itemQualityStrings` etc., defined near the top of the second `namespace mmo` block) — placing them just before `DrawDetailsImpl` satisfies this. `<algorithm>` (for `std::max`/`std::size`) is already transitively included; add `#include <algorithm>` at the top of the .cpp only if the build complains.

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: 0 errors.

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editor_windows/item_editor_window.h src/mmo_edit/editor_windows/item_editor_window.cpp
git commit -m "Add list filters to item editor (class, subclass, quality, inv type, stat, level)"
```

---

### Task 4: Spell editor filters

**Files:**
- Modify: `src/mmo_edit/editor_windows/spell_editor_window.h`
- Modify: `src/mmo_edit/editor_windows/spell_editor_window.cpp`

**Interfaces:**
- Consumes: the Task 2 virtuals; file-scope tables in spell_editor_window.cpp: `s_spellEffectNames` (String[], size == `spell_effects::Count_`), `s_auraTypeNames` (String[], size == `aura_type::Count_`); `spell_attributes::Ability` (0x10) and `spell_attributes::Passive` (0x40) from `game/spell.h`; `m_project.classes` / `m_project.races` (`getById(i)` may return nullptr; mask bit = `1 << (id - 1)`).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Declare overrides and filter state in the header**

In `spell_editor_window.h`, in the `private:` section after `OnNewEntry`, add:

```cpp
		bool HasFilters() const override { return true; }

		void DrawFilters() override;

		bool MatchesFilters(const proto::SpellEntry& entry) const override;

		int ActiveFilterCount() const override;

		void ResetFilters() override;
```

And in the member section (after `m_iconCache`), add:

```cpp
		/// List filter state: enum indices / entry ids, -1 = Any.
		int m_filterEffectType = -1;
		int m_filterAuraType = -1;
		/// 0 = Any, 1 = Ability, 2 = Passive.
		int m_filterKind = 0;
		int m_filterClass = -1;
		int m_filterRace = -1;
		/// Spell-level bounds, 0 = unbounded.
		int m_filterMinLevel = 0;
		int m_filterMaxLevel = 0;
```

- [ ] **Step 2: Implement the overrides in the .cpp**

Add to `spell_editor_window.cpp` (inside `namespace mmo`, below the `s_spellEffectNames` / `s_auraTypeNames` tables — e.g. directly before `DrawDetailsImpl`):

```cpp
	static const char* s_spellKindFilterNames[] = {
		"Any",
		"Ability",
		"Passive"};

	void SpellEditorWindow::DrawFilters()
	{
		// Effect type (matches if any effect has this type)
		if (ImGui::BeginCombo("Effect##listFilter", m_filterEffectType >= 0 ? s_spellEffectNames[m_filterEffectType].c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterEffectType < 0))
			{
				m_filterEffectType = -1;
			}
			for (int i = 0; i < static_cast<int>(std::size(s_spellEffectNames)); ++i)
			{
				if (ImGui::Selectable(s_spellEffectNames[i].c_str(), m_filterEffectType == i))
				{
					m_filterEffectType = i;
				}
			}
			ImGui::EndCombo();
		}

		// Aura type (matches if any effect applies this aura)
		if (ImGui::BeginCombo("Aura##listFilter", m_filterAuraType >= 0 ? s_auraTypeNames[m_filterAuraType].c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterAuraType < 0))
			{
				m_filterAuraType = -1;
			}
			for (int i = 0; i < static_cast<int>(std::size(s_auraTypeNames)); ++i)
			{
				if (ImGui::Selectable(s_auraTypeNames[i].c_str(), m_filterAuraType == i))
				{
					m_filterAuraType = i;
				}
			}
			ImGui::EndCombo();
		}

		// Kind: Any / Ability / Passive (attribute bits)
		ImGui::SetNextItemWidth(160);
		ImGui::Combo("Kind##listFilter", &m_filterKind, s_spellKindFilterNames, IM_ARRAYSIZE(s_spellKindFilterNames));

		// Class (explicit classmask bit; unrestricted spells are excluded)
		const auto* filterClassEntry = m_filterClass >= 0 ? m_project.classes.getById(m_filterClass) : nullptr;
		if (ImGui::BeginCombo("Class##listFilter", filterClassEntry != nullptr ? filterClassEntry->name().c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterClass < 0))
			{
				m_filterClass = -1;
			}
			for (uint32 i = 1; i < 32; ++i)
			{
				if (const proto::ClassEntry* classEntry = m_project.classes.getById(i))
				{
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(classEntry->name().c_str(), m_filterClass == static_cast<int>(i)))
					{
						m_filterClass = static_cast<int>(i);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}

		// Race (explicit racemask bit; unrestricted spells are excluded)
		const auto* filterRaceEntry = m_filterRace >= 0 ? m_project.races.getById(m_filterRace) : nullptr;
		if (ImGui::BeginCombo("Race##listFilter", filterRaceEntry != nullptr ? filterRaceEntry->name().c_str() : "Any"))
		{
			if (ImGui::Selectable("Any", m_filterRace < 0))
			{
				m_filterRace = -1;
			}
			for (uint32 i = 1; i < 32; ++i)
			{
				if (const proto::RaceEntry* raceEntry = m_project.races.getById(i))
				{
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(raceEntry->name().c_str(), m_filterRace == static_cast<int>(i)))
					{
						m_filterRace = static_cast<int>(i);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}

		// Spell level bounds (0 = unbounded)
		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Min Level##listFilter", &m_filterMinLevel))
		{
			m_filterMinLevel = std::max(0, m_filterMinLevel);
		}
		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Max Level##listFilter", &m_filterMaxLevel))
		{
			m_filterMaxLevel = std::max(0, m_filterMaxLevel);
		}
	}

	bool SpellEditorWindow::MatchesFilters(const proto::SpellEntry& entry) const
	{
		if (m_filterEffectType >= 0)
		{
			bool hasEffect = false;
			for (const auto& effect : entry.effects())
			{
				if (static_cast<int>(effect.type()) == m_filterEffectType)
				{
					hasEffect = true;
					break;
				}
			}
			if (!hasEffect)
			{
				return false;
			}
		}
		if (m_filterAuraType >= 0)
		{
			bool hasAura = false;
			for (const auto& effect : entry.effects())
			{
				if (static_cast<int>(effect.aura()) == m_filterAuraType)
				{
					hasAura = true;
					break;
				}
			}
			if (!hasAura)
			{
				return false;
			}
		}
		if (m_filterKind != 0)
		{
			const uint32 attributes = entry.attributes_size() > 0 ? entry.attributes(0) : 0;
			const uint32 requiredBit = m_filterKind == 1 ? spell_attributes::Ability : spell_attributes::Passive;
			if ((attributes & requiredBit) == 0)
			{
				return false;
			}
		}
		if (m_filterClass >= 0 && (entry.classmask() & (1 << (m_filterClass - 1))) == 0)
		{
			return false;
		}
		if (m_filterRace >= 0 && (entry.racemask() & (1 << (m_filterRace - 1))) == 0)
		{
			return false;
		}
		if (m_filterMinLevel > 0 && entry.spelllevel() < m_filterMinLevel)
		{
			return false;
		}
		if (m_filterMaxLevel > 0 && entry.spelllevel() > m_filterMaxLevel)
		{
			return false;
		}
		return true;
	}

	int SpellEditorWindow::ActiveFilterCount() const
	{
		int count = 0;
		count += m_filterEffectType >= 0 ? 1 : 0;
		count += m_filterAuraType >= 0 ? 1 : 0;
		count += m_filterKind != 0 ? 1 : 0;
		count += m_filterClass >= 0 ? 1 : 0;
		count += m_filterRace >= 0 ? 1 : 0;
		count += m_filterMinLevel > 0 ? 1 : 0;
		count += m_filterMaxLevel > 0 ? 1 : 0;
		return count;
	}

	void SpellEditorWindow::ResetFilters()
	{
		m_filterEffectType = -1;
		m_filterAuraType = -1;
		m_filterKind = 0;
		m_filterClass = -1;
		m_filterRace = -1;
		m_filterMinLevel = 0;
		m_filterMaxLevel = 0;
	}
```

Note: `spelllevel` is an `int32` proto field, so the comparisons are signed-safe. `spell_attributes` comes from `game/spell.h`, which spell_editor_window.cpp already includes (it uses `spell_effects::Count_` / `aura_type::Count_`).

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: 0 errors.

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editor_windows/spell_editor_window.h src/mmo_edit/editor_windows/spell_editor_window.cpp
git commit -m "Add list filters to spell editor (effect, aura, kind, class, race, level)"
```

---

### Task 5: Manual smoke test in the editor

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Launch mmo_edit** (Debug build from `bin/`), open the Item editor window.

- [ ] **Step 2: Item editor checks**
- "Filters" header appears between search bar and count; other editor windows (e.g. creature editor) show no Filters header.
- Class = some class → list narrows; header shows "Filters (1 active)"; Subclass dropdown enables and only offers subclasses of that class.
- Pick a subclass, then change Class → subclass filter resets to Any.
- Quality = Epic + Stat = Stamina → only epic items with a Stamina stat entry.
- Min/Max Req. Level narrows accordingly; text search combines (AND) with filters; "Showing X of Y entries" is consistent.
- Reset Filters clears everything (button disabled at 0 active).
- Selecting an entry, then filtering it out: list shows no selection, details pane still shows the entry, no crash.

- [ ] **Step 3: Spell editor checks**
- Aura = a damage-over-time aura type → only DoT spells.
- Kind = Passive → only passives; Kind = Ability → only abilities.
- Class = Mage (or similar) → only spells with that class's mask bit; unrestricted (mask 0) spells excluded.
- Effect filter + level bounds work and AND-combine.

- [ ] **Step 4: Report results** — fix anything broken before proceeding to gate/merge.
