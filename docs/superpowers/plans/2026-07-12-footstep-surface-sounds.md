# Footstep Surface Sounds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Data-driven footstep sounds selected by the surface type of the floor under the player, resolved from materials / material instances (including per-splat-layer resolution on terrain), played through `SoundEntryPlayer`.

**Architecture:** A new `surface_types` proto table maps surface type ids to footstep sound entries. Materials (HMAT) carry a base surface type id + 4 per-layer ids (for terrain splatting materials); material instances (HMI) can override them. The mesh collision `AABBTree` gains a serialized per-face submesh-index array so an entity ray hit resolves face → submesh → material → surface type; terrain tiles resolve via the dominant splat layer at the hit point. `GamePlayerC::OnFootstep` does a short downward ray query and plays the resolved sound entry, falling back to the legacy hardcoded WAVs when nothing resolves.

**Tech Stack:** C++17, protobuf (proto2), ImGui editor, Catch2 unit tests, CMake/MSVC.

**Spec:** `docs/superpowers/specs/2026-07-12-footstep-surface-sounds-design.md`

## Global Constraints

- Code style: Allman braces, tabs for indentation, `m_camelCase` members, `PascalCase` methods, `camelCase` locals, `#pragma once`, Doxygen on public members.
- Every new source file starts with: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions. Use `ASSERT`/`VERIFY` from `base/macros.h`; log with `DLOG`/`WLOG`/`ELOG`.
- proto_data and client_data `.proto` mirrors MUST use identical field numbers (ClientDB invariant).
- All builds on Windows: `cmake --build build --config Debug -t <target>`. After adding new files, re-run `cmake -S . -B build` once so the source globs pick them up.
- Backwards compatibility: old HMAT/HMI files and old `COLL` chunks (`BVH1`) must keep loading unchanged.

---

### Task 1: surface_types proto data (server + client mirror, project registration, client export)

**Files:**
- Create: `src/shared/proto_data/surface_types.proto`
- Create: `src/shared/client_data/surface_types.proto`
- Modify: `src/shared/proto_data/project.h` (include ~line 57, typedef ~line 110, member ~line 186, load entry ~line 280, save entry ~line 364)
- Modify: `src/shared/client_data/project.h` (include ~line 32, typedef ~line 59, member ~line 96, load entry ~line 161, save entry ~line 212)
- Modify: `src/mmo_edit/main_window.cpp` (`sharedManagers[]` array, ~line 987)

**Interfaces:**
- Produces: `proto::SurfaceTypeManager` / `mmo::proto::Project::surfaceTypes` (server+editor side) and `proto_client::SurfaceTypeManager` / `mmo::proto_client::Project::surfaceTypes` (client side). Entry type `SurfaceType` with `id()`, `name()`, `footstep_sound()`.

- [ ] **Step 1: Create `src/shared/proto_data/surface_types.proto`**

```proto
syntax = "proto2";
package mmo.proto;

// NOTE: Keep this file in sync with src/shared/client_data/surface_types.proto!
// The client reads the exported binary data with the mirrored proto_client schema,
// so every field number in here has to match the mirror exactly.

// A physical surface category resolved from materials (base surface type) and
// terrain splatting layers. Consumers hang per-surface data off these entries;
// currently that is the footstep sound.
message SurfaceType
{
	required uint32 id = 1;
	required string name = 2;

	// SoundEntry id played for footsteps on this surface (0 = none).
	optional uint32 footstep_sound = 3;
}

message SurfaceTypes
{
	repeated SurfaceType entry = 1;
}
```

- [ ] **Step 2: Create `src/shared/client_data/surface_types.proto`**

Identical content except the package line reads `package mmo.proto_client;` and the sync note points the other way (`Keep this file in sync with src/shared/proto_data/surface_types.proto!`).

- [ ] **Step 3: Register in `src/shared/proto_data/project.h`**

Add with the other includes (after `sounds.pb.h`):
```cpp
#include "shared/proto_data/surface_types.pb.h"
```
Add with the other typedefs (after `SoundManager`):
```cpp
typedef TemplateManager<mmo::proto::SurfaceTypes, mmo::proto::SurfaceType> SurfaceTypeManager;
```
Add member after `SoundManager sounds;`:
```cpp
/// Physical surface categories resolved from materials (footstep sounds etc.).
SurfaceTypeManager surfaceTypes;
```
Add load entry after `managers.push_back(ManagerEntry("sounds", sounds, true));`:
```cpp
managers.push_back(ManagerEntry("surface_types", surfaceTypes, true));
```
Add save entry after the `"sounds"` save entry:
```cpp
managers.push_back(ManagerEntry("surface_types", "surface_types", surfaceTypes));
```

- [ ] **Step 4: Register in `src/shared/client_data/project.h`**

Same four additions, using the existing `sounds` lines in that file as the template (note: the client project uses `mmo::proto_client::` types and its save list uses `managers.emplace_back("surface_types", "surface_types", surfaceTypes);` following the `sounds` line at ~212).

- [ ] **Step 5: Add to client data export**

In `src/mmo_edit/main_window.cpp`, `sharedManagers[]` (~line 987), append after `"sounds"`:
```cpp
	"surface_types",
```

- [ ] **Step 6: Re-run CMake configure and build**

Run:
```powershell
cmake -S . -B build
cmake --build build --config Debug -t proto_data client_data
```
Expected: both targets compile; protoc generates `surface_types.pb.cc/h` for both schemas.

- [ ] **Step 7: Commit**

```bash
git add src/shared/proto_data/surface_types.proto src/shared/client_data/surface_types.proto src/shared/proto_data/project.h src/shared/client_data/project.h src/mmo_edit/main_window.cpp
git commit -m "feat: add surface_types proto data table"
```

---

### Task 2: Surface Type editor window

**Files:**
- Create: `src/mmo_edit/editor_windows/surface_type_editor_window.h`
- Create: `src/mmo_edit/editor_windows/surface_type_editor_window.cpp`
- Modify: `src/mmo_edit/mmo_edit.cpp` (window registration, next to the `SoundEditorWindow` line ~257)

**Interfaces:**
- Consumes: `proto::SurfaceTypeManager` (Task 1), `DrawSoundEntryCombo` from `editor_windows/sound_entry_combo.h`.
- Produces: editor window for CRUD on surface types. No code interface for later tasks.

- [ ] **Step 1: Create the window header**

`src/mmo_edit/editor_windows/surface_type_editor_window.h` — mirror `sound_editor_window.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

#include <imgui.h>

namespace mmo
{
	/// Allows editing of surface types which are resolved from materials and referenced
	/// by footstep sound playback.
	class SurfaceTypeEditorWindow final
		: public EditorEntryWindowBase<proto::SurfaceTypes, proto::SurfaceType>
		, public NonCopyable
	{
	public:
		explicit SurfaceTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~SurfaceTypeEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::SurfaceType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::SurfaceTypes, proto::SurfaceType>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
		ImGuiTextFilter m_footstepSoundFilter;
	};
}
```

- [ ] **Step 2: Create the window implementation**

`src/mmo_edit/editor_windows/surface_type_editor_window.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "surface_type_editor_window.h"
#include "editor_imgui_helpers.h"
#include "sound_entry_combo.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	SurfaceTypeEditorWindow::SurfaceTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.surfaceTypes, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Surface Types";
	}

	void SurfaceTypeEditorWindow::OnNewEntry(proto::TemplateManager<proto::SurfaceTypes, proto::SurfaceType>::EntryType& entry)
	{
		entry.set_name("New Surface Type");
	}

	void SurfaceTypeEditorWindow::DrawDetailsImpl(proto::SurfaceType& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSoundEntryCombo(m_project.sounds, "Footstep Sound", currentEntry.footstep_sound(), m_footstepSoundFilter,
				[&currentEntry](const uint32 soundId)
				{
					if (soundId == 0)
					{
						currentEntry.clear_footstep_sound();
					}
					else
					{
						currentEntry.set_footstep_sound(soundId);
					}
				});
		}
	}
}
```
Note: if `ScopedEditorSection` does not exist in `editor_imgui_helpers.h`, open `sound_editor_window.cpp` and copy whatever section helper it actually uses.

- [ ] **Step 3: Register the window**

In `src/mmo_edit/mmo_edit.cpp`, after the `SoundEditorWindow` registration (~line 257):
```cpp
	mainWindow.AddEditorWindow(std::make_unique<mmo::SurfaceTypeEditorWindow>("Surface Type Editor", project, mainWindow));
```
Add the matching include next to the sound editor window include at the top of the file:
```cpp
#include "editor_windows/surface_type_editor_window.h"
```

- [ ] **Step 4: Build the editor**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_edit
```
Expected: compiles and links.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editor_windows/surface_type_editor_window.h src/mmo_edit/editor_windows/surface_type_editor_window.cpp src/mmo_edit/mmo_edit.cpp
git commit -m "feat: add surface type editor window"
```

---

### Task 3: Surface type ids on Material / MaterialInstance + HMAT/HMI serialization

**Files:**
- Modify: `src/shared/graphics/material.h` (MaterialInterface ~line 79-180; Material members/accessors near the foliage accessors ~line 332-382)
- Modify: `src/shared/graphics/material_instance.h` (near the foliage override block ~line 139-182)
- Modify: `src/shared/scene_graph/material_serializer.h` (version enum ~line 37, deserializer method list)
- Modify: `src/shared/scene_graph/material_serializer.cpp` (chunk magic ~line 22, version dispatch ~line 73, new read handler, Export ~line 743)
- Modify: `src/shared/scene_graph/material_instance_serializer.h` (version enum ~line 30, method list)
- Modify: `src/shared/scene_graph/material_instance_serializer.cpp` (same pattern as the MFOL instance chunk)

**Interfaces:**
- Produces:
  - `virtual uint32 MaterialInterface::GetSurfaceTypeId() const` (0 = unset)
  - `virtual uint32 MaterialInterface::GetLayerSurfaceTypeId(uint8 layer) const` (0 = fall back to base)
  - `Material::SetSurfaceTypeId(uint32)`, `Material::SetLayerSurfaceTypeId(uint8, uint32)`
  - `MaterialInstance::IsOverridingSurfaceTypes()`, `SetOverrideSurfaceTypes(bool)`, `SetOwnSurfaceTypeId(uint32)`, `SetOwnLayerSurfaceTypeId(uint8, uint32)`, `GetOwnSurfaceTypeId()`, `GetOwnLayerSurfaceTypeId(uint8)`
- Consumed by: Task 4 (editor UI), Task 7 (Entity), Task 8 (Tile).

- [ ] **Step 1: Extend `MaterialInterface` (material.h)**

Add to the `MaterialInterface` class (near `GetFoliageEntries`, virtual with default so exotic implementers keep compiling):
```cpp
		/// @brief Gets the surface type id of this material (0 = unset). Surface types are
		///        proto data entries driving per-surface behavior such as footstep sounds.
		[[nodiscard]] virtual uint32 GetSurfaceTypeId() const { return 0; }

		/// @brief Gets the surface type id of a terrain splatting layer (0 = fall back to
		///        the base surface type). Only meaningful for terrain splatting materials.
		/// @param layer The splat layer index (0-3).
		[[nodiscard]] virtual uint32 GetLayerSurfaceTypeId(uint8 layer) const { return 0; }
```

- [ ] **Step 2: Implement storage on `Material` (material.h)**

Next to the foliage accessors (~line 332):
```cpp
		[[nodiscard]] uint32 GetSurfaceTypeId() const override { return m_surfaceTypeId; }

		/// @brief Sets the surface type id of this material (0 = unset).
		void SetSurfaceTypeId(const uint32 id) { m_surfaceTypeId = id; }

		[[nodiscard]] uint32 GetLayerSurfaceTypeId(const uint8 layer) const override { return layer < m_layerSurfaceTypeIds.size() ? m_layerSurfaceTypeIds[layer] : 0; }

		/// @brief Sets the surface type id of a terrain splatting layer (0 = fall back to base).
		void SetLayerSurfaceTypeId(const uint8 layer, const uint32 id)
		{
			if (layer < m_layerSurfaceTypeIds.size())
			{
				m_layerSurfaceTypeIds[layer] = id;
			}
		}
```
Members (next to `m_foliage`, include `<array>` if not present):
```cpp
		uint32 m_surfaceTypeId = 0;
		std::array<uint32, 4> m_layerSurfaceTypeIds{};
```

- [ ] **Step 3: Implement override on `MaterialInstance` (material_instance.h)**

Next to the foliage override block (~line 139), following its exact pattern:
```cpp
		[[nodiscard]] uint32 GetSurfaceTypeId() const override
		{
			if (m_overrideSurfaceTypes)
			{
				return m_surfaceTypeId;
			}

			return m_parent ? m_parent->GetSurfaceTypeId() : 0;
		}

		[[nodiscard]] uint32 GetLayerSurfaceTypeId(const uint8 layer) const override
		{
			if (m_overrideSurfaceTypes)
			{
				return layer < m_layerSurfaceTypeIds.size() ? m_layerSurfaceTypeIds[layer] : 0;
			}

			return m_parent ? m_parent->GetLayerSurfaceTypeId(layer) : 0;
		}

		/// @brief Whether this instance overrides the parent material's surface types.
		[[nodiscard]] bool IsOverridingSurfaceTypes() const { return m_overrideSurfaceTypes; }

		/// @brief Sets whether this instance overrides the parent material's surface types.
		void SetOverrideSurfaceTypes(const bool value) { m_overrideSurfaceTypes = value; }

		/// @brief Gets the instance-level base surface type id (only used when overriding).
		[[nodiscard]] uint32 GetOwnSurfaceTypeId() const { return m_surfaceTypeId; }

		/// @brief Sets the instance-level base surface type id.
		void SetOwnSurfaceTypeId(const uint32 id) { m_surfaceTypeId = id; }

		/// @brief Gets an instance-level layer surface type id (only used when overriding).
		[[nodiscard]] uint32 GetOwnLayerSurfaceTypeId(const uint8 layer) const { return layer < m_layerSurfaceTypeIds.size() ? m_layerSurfaceTypeIds[layer] : 0; }

		/// @brief Sets an instance-level layer surface type id.
		void SetOwnLayerSurfaceTypeId(const uint8 layer, const uint32 id)
		{
			if (layer < m_layerSurfaceTypeIds.size())
			{
				m_layerSurfaceTypeIds[layer] = id;
			}
		}
```
Members (next to `m_overrideFoliage`, include `<array>` if needed):
```cpp
		/// Instance-level surface type override. Only used when m_overrideSurfaceTypes is true;
		/// otherwise the parent material's surface types apply.
		uint32 m_surfaceTypeId = 0;
		std::array<uint32, 4> m_layerSurfaceTypeIds{};
		bool m_overrideSurfaceTypes = false;
```
Note: `m_parent` is the existing parent material member in `MaterialInstance` — check its actual name (the foliage getter at line 147 uses `m_parent->GetFoliageEntries()`) and match it.

- [ ] **Step 4: HMAT serialization (material_serializer.h/.cpp)**

In `material_serializer.h` add to the version enum:
```cpp
			/// Version 0.7: adds an optional MSRF chunk carrying the material's surface type
			/// id and four per-splat-layer surface type ids.
			Version_0_7 = 0x0700,
```
Declare in `MaterialDeserializer`:
```cpp
			/// @brief Reads the v0.7 surface type chunk (MSRF).
			bool ReadMaterialSurfaceTypeChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
```
In `material_serializer.cpp` add the chunk magic next to `MaterialFoliageChunk`:
```cpp
	static const ChunkMagic MaterialSurfaceTypeChunk = MakeChunkMagic('FRSM');
```
In `ReadMaterialChunk` version dispatch (after the `Version_0_6` block):
```cpp
				if (version >= material_version::Version_0_7)
				{
					AddChunkHandler(*MaterialSurfaceTypeChunk, false, *this, &MaterialDeserializer::ReadMaterialSurfaceTypeChunk);
				}
```
Add the read handler (after `ReadMaterialFoliageChunk`):
```cpp
	bool MaterialDeserializer::ReadMaterialSurfaceTypeChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 surfaceTypeId = 0;
		reader >> io::read<uint32>(surfaceTypeId);
		m_material.SetSurfaceTypeId(surfaceTypeId);

		for (uint8 layer = 0; layer < 4; ++layer)
		{
			uint32 layerSurfaceTypeId = 0;
			reader >> io::read<uint32>(layerSurfaceTypeId);
			m_material.SetLayerSurfaceTypeId(layer, layerSurfaceTypeId);
		}

		return reader;
	}
```
In `Export`, after the foliage chunk block (~line 768):
```cpp
		// Surface type chunk (v0.7+). Only written when any surface type is assigned.
		bool hasSurfaceTypes = material.GetSurfaceTypeId() != 0;
		for (uint8 layer = 0; layer < 4 && !hasSurfaceTypes; ++layer)
		{
			hasSurfaceTypes = material.GetLayerSurfaceTypeId(layer) != 0;
		}

		if (hasSurfaceTypes)
		{
			ChunkWriter surfaceTypeChunkWriter { MaterialSurfaceTypeChunk, writer };
			writer << io::write<uint32>(material.GetSurfaceTypeId());
			for (uint8 layer = 0; layer < 4; ++layer)
			{
				writer << io::write<uint32>(material.GetLayerSurfaceTypeId(layer));
			}

			surfaceTypeChunkWriter.Finish();
		}
```
Find where `Export` resolves `material_version::Latest` into a concrete version number written into the header chunk (search for `Version_0_6` in the cpp) and bump it to `Version_0_7`.

- [ ] **Step 5: HMI serialization (material_instance_serializer.h/.cpp)**

Version enum addition:
```cpp
			/// Version 0.3: adds an optional MSRF chunk carrying an instance-level surface
			/// type override (base id + four layer ids).
			Version_0_3 = 0x0300,
```
Follow the MFOL instance chunk pattern exactly: declare `ReadMaterialSurfaceTypeChunk` in `MaterialInstanceDeserializer`, register it for `Version_0_3`+, and in `Export` write the chunk **only when `materialInstance.IsOverridingSurfaceTypes()`** (presence of the chunk = override active):
```cpp
		if (materialInstance.IsOverridingSurfaceTypes())
		{
			ChunkWriter surfaceTypeChunkWriter { MaterialSurfaceTypeChunk, writer };
			writer << io::write<uint32>(materialInstance.GetOwnSurfaceTypeId());
			for (uint8 layer = 0; layer < 4; ++layer)
			{
				writer << io::write<uint32>(materialInstance.GetOwnLayerSurfaceTypeId(layer));
			}

			surfaceTypeChunkWriter.Finish();
		}
```
Read handler sets values via `SetOwnSurfaceTypeId` / `SetOwnLayerSurfaceTypeId` and calls `SetOverrideSurfaceTypes(true)`. Bump the instance serializer's Latest resolution to `Version_0_3` (same search as Step 4). Reuse the same `MaterialSurfaceTypeChunk` magic constant (declare it in this cpp too, or share if both cpps already share chunk constants — mirror how `MFOL` is handled between the two serializers).

- [ ] **Step 6: Build**

```powershell
cmake --build build --config Debug -t mmo_edit mmo_client
```
Expected: compiles. Old materials still load (no MSRF chunk registered below v0.7 → fields stay 0).

- [ ] **Step 7: Commit**

```bash
git add src/shared/graphics/material.h src/shared/graphics/material_instance.h src/shared/scene_graph/material_serializer.h src/shared/scene_graph/material_serializer.cpp src/shared/scene_graph/material_instance_serializer.h src/shared/scene_graph/material_instance_serializer.cpp
git commit -m "feat: add surface type ids to materials and material instances (HMAT v0.7, HMI v0.3)"
```

---

### Task 4: Material editor + material instance editor UI

**Files:**
- Create: `src/mmo_edit/editor_windows/surface_type_combo.h`
- Modify: `src/mmo_edit/editors/material_editor/material_editor.h` (~line 20, ctor)
- Modify: `src/mmo_edit/editors/material_editor/material_editor.cpp` (instance creation ~line 410)
- Modify: `src/mmo_edit/editors/material_editor/material_editor_instance.h` (~line 61 ctor, ~line 97 add method)
- Modify: `src/mmo_edit/editors/material_editor/material_editor_instance.cpp` (details panel ~line 956, new section after `DrawFoliageSection` ~line 1001)
- Modify: `src/mmo_edit/editors/material_instance_editor/material_instance_editor.h` (~line 19, ctor)
- Modify: `src/mmo_edit/editors/material_instance_editor/material_instance_editor.cpp` (instance creation ~line 79)
- Modify: `src/mmo_edit/editors/material_instance_editor/material_instance_editor_instance.h`/.cpp (details panel near the Two Sided checkbox ~line 335)
- Modify: `src/mmo_edit/mmo_edit.cpp` (~line 293, pass `project` to both editors)

**Interfaces:**
- Consumes: `proto::SurfaceTypeManager` (Task 1), `Material::SetSurfaceTypeId`/`SetLayerSurfaceTypeId`, `MaterialInstance` override API (Task 3).
- Produces: `DrawSurfaceTypeCombo(const proto::SurfaceTypeManager&, const char* label, uint32 currentId, ImGuiTextFilter&, const std::function<void(uint32)>& setter)`; `MaterialEditor::GetProject()` / `MaterialInstanceEditor::GetProject()` returning `proto::Project&`.

- [ ] **Step 1: Create `src/mmo_edit/editor_windows/surface_type_combo.h`**

Mirror `sound_entry_combo.h` 1:1, renamed for surface types:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include <imgui.h>

#include "base/typedefs.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Draws a filtered combo box to pick a surface type (0 = none).
	/// @param surfaceTypes The surface type manager to list entries from.
	/// @param label The combo label.
	/// @param currentSurfaceTypeId The currently assigned surface type id (0 = none).
	/// @param filter Persistent text filter backing the popup's search box.
	/// @param setter Invoked with the picked surface type id (0 when cleared).
	/// @param noneLabel Display label of the "no surface type" option.
	inline void DrawSurfaceTypeCombo(const proto::SurfaceTypeManager& surfaceTypes, const char* label, const uint32 currentSurfaceTypeId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel = "(None)")
	{
		const proto::SurfaceType* currentType = nullptr;
		if (currentSurfaceTypeId != 0)
		{
			currentType = surfaceTypes.getById(currentSurfaceTypeId);
		}

		if (ImGui::BeginCombo(label, currentType ? currentType->name().c_str() : noneLabel, ImGuiComboFlags_HeightLargest))
		{
			if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
			{
				ImGui::SetKeyboardFocusHere(0);
			}

			filter.Draw("##surface_type_filter", -1.0f);

			if (ImGui::Selectable(noneLabel))
			{
				setter(0);
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginChild("##surface_type_scroll_area", ImVec2(0, 400)))
			{
				for (const auto& surfaceType : surfaceTypes.getTemplates().entry())
				{
					if (filter.IsActive() && !filter.PassFilter(surfaceType.name().c_str()))
					{
						continue;
					}

					ImGui::PushID(surfaceType.id());
					if (ImGui::Selectable(surfaceType.name().c_str()))
					{
						setter(surfaceType.id());

						filter.Clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}
	}
}
```

- [ ] **Step 2: Thread `proto::Project&` into both material editors**

`material_editor.h` (~line 20) — follow how `MeshEditor` takes its project (`mesh_editor.h:19`):
```cpp
		explicit MaterialEditor(EditorHost& host, PreviewProviderManager& previewManager, proto::Project& project)
```
Store `proto::Project& m_project;` and add:
```cpp
		/// @brief Gets the proto data project (surface type list etc.).
		[[nodiscard]] proto::Project& GetProject() const { return m_project; }
```
Add `#include "proto_data/project.h"` (check `mesh_editor.h` for whether a forward declaration + include in cpp is used instead, and match). Do the same for `MaterialInstanceEditor` (`material_instance_editor.h:19`).

In `mmo_edit.cpp` (~line 293) pass `project`:
```cpp
	mainWindow.AddEditor(std::make_unique<mmo::MaterialEditor>(mainWindow, previewProviderManager, project));
	mainWindow.AddEditor(std::make_unique<mmo::MaterialInstanceEditor>(mainWindow, previewProviderManager, project));
```
(Also split these onto separate lines — they are currently jammed on one line.)

- [ ] **Step 3: Material editor surface type section**

`material_editor_instance.h`: declare `void DrawSurfaceTypeSection();` next to `DrawFoliageSection()` (~line 97) and add members:
```cpp
		ImGuiTextFilter m_surfaceTypeFilter;
		ImGuiTextFilter m_layerSurfaceTypeFilters[4];
```
`material_editor_instance.cpp`: call `DrawSurfaceTypeSection();` right after the `DrawFoliageSection();` call (~line 956). Implement after `DrawFoliageSection` (~line 1080), accessing the editor via the member that holds the owning `MaterialEditor&` (the instance ctor receives `MaterialEditor& editor` — check the stored member name, likely `m_editor`):
```cpp
	void MaterialEditorInstance::DrawSurfaceTypeSection()
	{
		if (!ImGui::CollapsingHeader("Surface Type"))
		{
			return;
		}

		ImGui::TextWrapped("Physical surface category of this material, used to pick footstep sounds. "
			"For terrain splatting materials, assign one surface type per splat layer instead.");

		const proto::SurfaceTypeManager& surfaceTypes = m_editor.GetProject().surfaceTypes;

		DrawSurfaceTypeCombo(surfaceTypes, "Surface Type", m_material->GetSurfaceTypeId(), m_surfaceTypeFilter,
			[this](const uint32 id)
			{
				m_material->SetSurfaceTypeId(id);
			});

		ImGui::SeparatorText("Layer Surface Types (terrain splatting)");

		for (uint8 layer = 0; layer < 4; ++layer)
		{
			const String label = "Layer " + std::to_string(layer + 1);
			DrawSurfaceTypeCombo(surfaceTypes, label.c_str(), m_material->GetLayerSurfaceTypeId(layer), m_layerSurfaceTypeFilters[layer],
				[this, layer](const uint32 id)
				{
					m_material->SetLayerSurfaceTypeId(layer, id);
				});
		}
	}
```
Include `"editor_windows/surface_type_combo.h"` (check how the file includes other editor_windows headers for the correct relative path). Confirm `m_material` here is a `std::shared_ptr<Material>` (it is — `DrawFoliageSection` calls the non-const `GetFoliageEntries()`).

- [ ] **Step 4: Material instance editor override UI**

In `material_instance_editor_instance.cpp`, after the Two Sided checkbox block (~line 338), add (m_material here is the `MaterialInstance`; verify the member name used at line 335):
```cpp
			if (ImGui::CollapsingHeader("Surface Type"))
			{
				bool overrideSurfaceTypes = m_material->IsOverridingSurfaceTypes();
				if (ImGui::Checkbox("Override Surface Types", &overrideSurfaceTypes))
				{
					m_material->SetOverrideSurfaceTypes(overrideSurfaceTypes);
				}

				ImGui::BeginDisabled(!overrideSurfaceTypes);

				const proto::SurfaceTypeManager& surfaceTypes = m_editor.GetProject().surfaceTypes;

				DrawSurfaceTypeCombo(surfaceTypes, "Surface Type", m_material->GetOwnSurfaceTypeId(), m_surfaceTypeFilter,
					[this](const uint32 id)
					{
						m_material->SetOwnSurfaceTypeId(id);
					});

				for (uint8 layer = 0; layer < 4; ++layer)
				{
					const String label = "Layer " + std::to_string(layer + 1);
					DrawSurfaceTypeCombo(surfaceTypes, label.c_str(), m_material->GetOwnLayerSurfaceTypeId(layer), m_layerSurfaceTypeFilters[layer],
						[this, layer](const uint32 id)
						{
							m_material->SetOwnLayerSurfaceTypeId(layer, id);
						});
				}

				ImGui::EndDisabled();
			}
```
Add the same `ImGuiTextFilter` members to `material_instance_editor_instance.h` as in Step 3.

- [ ] **Step 5: Build and commit**

```powershell
cmake --build build --config Debug -t mmo_edit
```
Expected: compiles.
```bash
git add src/mmo_edit/editor_windows/surface_type_combo.h src/mmo_edit/editors/material_editor src/mmo_edit/editors/material_instance_editor src/mmo_edit/mmo_edit.cpp
git commit -m "feat: surface type editing in material and material instance editors"
```

---

### Task 5: AABBTree per-face submesh ids (TDD)

**Files:**
- Modify: `src/shared/math/aabb_tree.h` (Build overload ~line 81, accessor ~line 95, member ~line 162)
- Modify: `src/shared/math/aabb_tree.cpp` (`Clear` ~line 51, `Build` ~line 59-96, serialization ~line 385-479)
- Create: `src/unit_tests/test_aabb_tree.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `void AABBTree::Build(const std::vector<Vertex>& verts, const std::vector<Index>& indices, const std::vector<uint16>& faceSubMeshes)` — `faceSubMeshes.size()` must equal `indices.size() / 3`.
  - `const std::vector<uint16>& AABBTree::GetFaceSubMeshes() const` — indexed by the `faceIndex` that `IntersectRay` reports; empty when the tree was built without submesh ids or loaded from a `BVH1` payload.
  - Serialization: new magic `'BVH2'` (writes the array); reader accepts both `'BVH1'` (array stays empty) and `'BVH2'`.

- [ ] **Step 1: Write the failing tests**

Create `src/unit_tests/test_aabb_tree.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/aabb_tree.h"
#include "math/ray.h"

#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

using namespace mmo;

namespace
{
	// Two horizontal unit quads on the y=0 plane: submesh 0 spans x in [0,1],
	// submesh 1 spans x in [2,3]. Both span z in [0,1].
	AABBTree BuildTwoSubMeshTree()
	{
		const std::vector<AABBTree::Vertex> vertices = {
			// Quad A (submesh 0)
			{ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
			// Quad B (submesh 1)
			{ 2.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 1.0f }, { 2.0f, 0.0f, 1.0f }
		};

		const std::vector<AABBTree::Index> indices = {
			0, 1, 2,  0, 2, 3,
			4, 5, 6,  4, 6, 7
		};

		const std::vector<uint16> faceSubMeshes = { 0, 0, 1, 1 };

		AABBTree tree;
		tree.Build(vertices, indices, faceSubMeshes);
		return tree;
	}

	uint16 SubMeshHitByRayDownAt(const AABBTree& tree, const float x, const float z)
	{
		Ray ray(Vector3(x, 1.0f, z), Vector3(x, -1.0f, z));
		AABBTree::Index faceIndex = 0;
		REQUIRE(tree.IntersectRay(ray, &faceIndex));
		REQUIRE(faceIndex < tree.GetFaceSubMeshes().size());
		return tree.GetFaceSubMeshes()[faceIndex];
	}
}

TEST_CASE("AABBTree maps hit faces to their source submesh", "[aabb_tree]")
{
	const AABBTree tree = BuildTwoSubMeshTree();

	REQUIRE(tree.GetFaceSubMeshes().size() == 4);
	CHECK(SubMeshHitByRayDownAt(tree, 0.5f, 0.5f) == 0);
	CHECK(SubMeshHitByRayDownAt(tree, 2.5f, 0.5f) == 1);
}

TEST_CASE("AABBTree face submesh ids survive serialization round trip", "[aabb_tree]")
{
	const AABBTree tree = BuildTwoSubMeshTree();

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << tree;

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	AABBTree loaded;
	reader >> loaded;
	REQUIRE(reader);

	REQUIRE(loaded.GetFaceSubMeshes().size() == 4);
	CHECK(SubMeshHitByRayDownAt(loaded, 0.5f, 0.5f) == 0);
	CHECK(SubMeshHitByRayDownAt(loaded, 2.5f, 0.5f) == 1);
}

TEST_CASE("AABBTree built without submesh ids round trips with empty mapping", "[aabb_tree]")
{
	const std::vector<AABBTree::Vertex> vertices = {
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f }
	};
	const std::vector<AABBTree::Index> indices = { 0, 1, 2 };

	AABBTree tree;
	tree.Build(vertices, indices);
	CHECK(tree.GetFaceSubMeshes().empty());

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << tree;

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	AABBTree loaded;
	reader >> loaded;
	REQUIRE(reader);
	CHECK(loaded.GetFaceSubMeshes().empty());
}
```
Adjust the binary_io include paths/type names to whatever `src/unit_tests/test_binaryio.cpp` actually uses (it uses `io::VectorSink`, `io::Writer`, `io::MemorySource`, `io::Reader`). Check `math/ray.h` for the two-point `Ray` constructor (used the same way in `entity.cpp:548`); if `IntersectRay` requires a flags argument for two-sided hits, pass `raycast_flags::None`.

- [ ] **Step 2: Run tests to verify they fail**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t unit_tests
```
Expected: **compile failure** — `Build` has no 3-argument overload and `GetFaceSubMeshes` does not exist. That is the failing state; do not skip this check.

- [ ] **Step 3: Implement in `aabb_tree.h`**

Add after the existing `Build` declaration (~line 81):
```cpp
		/// @brief Builds the tree and records, for every face, the submesh index it came from.
		/// @param verts Vertices to build.
		/// @param indices Indices to build (3 per face).
		/// @param faceSubMeshes One submesh index per face (indices.size() / 3 entries).
		void Build(const std::vector<Vertex>& verts, const std::vector<Index>& indices, const std::vector<uint16>& faceSubMeshes);
```
Add accessor next to `GetIndices()` (~line 95):
```cpp
		/// @brief Per-face submesh indices aligned with the tree's face order (the faceIndex
		///        reported by IntersectRay indexes into this). Empty for trees built without
		///        submesh data or loaded from legacy files.
		const std::vector<uint16>& GetFaceSubMeshes() const { return m_faceSubMeshes; }
```
Add member next to `m_faceIndices` (~line 164):
```cpp
		std::vector<uint16> m_faceSubMeshes{};
```

- [ ] **Step 4: Implement in `aabb_tree.cpp`**

In `Clear()` (~line 51) add:
```cpp
		m_faceSubMeshes.clear();
```
Add the new overload right before the existing `Build`:
```cpp
	void AABBTree::Build(const std::vector<Vertex>& verts, const std::vector<Index>& indices, const std::vector<uint16>& faceSubMeshes)
	{
		ASSERT(faceSubMeshes.size() == indices.size() / 3);
		m_faceSubMeshes = faceSubMeshes;
		Build(verts, indices);
	}
```
In the existing `Build`, two changes:
1. At the top (after `m_faceIndices.clear();` ~line 65) do **not** clear `m_faceSubMeshes` — but guard against a stale array from a previous single-argument `Build` call by clearing it when its size doesn't match: 
```cpp
		if (m_faceSubMeshes.size() != indices.size() / 3)
		{
			m_faceSubMeshes.clear();
		}
```
2. Inside the reorder block (~line 86-95), permute the submesh ids with the same `m_faceIndices` mapping **before** `m_faceIndices.clear()`:
```cpp
		if (!m_faceSubMeshes.empty())
		{
			std::vector<uint16> sortedSubMeshes(numFaces);
			for (size_t i = 0; i < numFaces; ++i)
			{
				sortedSubMeshes[i] = m_faceSubMeshes[m_faceIndices[i]];
			}

			m_faceSubMeshes.swap(sortedSubMeshes);
		}
```
Serialization — writer (~line 385): change magic and append the array after the indices:
```cpp
		const uint32 magic = 'BVH2';
		...
		// Write indices
		w << io::write_dynamic_range<uint32>(tree.m_indices);

		// Write per-face submesh indices (may be empty)
		w << io::write_dynamic_range<uint32>(tree.m_faceSubMeshes);
```
Reader (~line 426): accept both magics and read the array only for the new one:
```cpp
		uint32 magic;
		r >> io::read<uint32>(magic);
		if (magic != static_cast<uint32>('BVH1') && magic != static_cast<uint32>('BVH2'))
		{
			r.setFailure();
			return r;
		}

		// Read vertices
		r >> io::read_container<uint32>(tree.m_vertices);

		// Read indices
		r >> io::read_container<uint32>(tree.m_indices);

		// Per-face submesh indices (BVH2+ only; legacy trees have no mapping)
		tree.m_faceSubMeshes.clear();
		if (magic == static_cast<uint32>('BVH2'))
		{
			r >> io::read_container<uint32>(tree.m_faceSubMeshes);
		}
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug -t unit_tests
build\bin\Debug\unit_tests.exe "[aabb_tree]"
```
(If the exe lands elsewhere, it's the path printed at the end of the build; repo convention is `bin/`.) Expected: all `[aabb_tree]` test cases PASS. Also run the full suite once (`unit_tests.exe`) to catch regressions.

- [ ] **Step 6: Commit**

```bash
git add src/shared/math/aabb_tree.h src/shared/math/aabb_tree.cpp src/unit_tests/test_aabb_tree.cpp
git commit -m "feat: per-face submesh indices in AABBTree (BVH2 serialization)"
```

---

### Task 6: Mesh editor collision build records submesh ids

**Files:**
- Modify: `src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp` ("Build Complex" handler, ~line 1469-1498)

**Interfaces:**
- Consumes: `AABBTree::Build(verts, indices, faceSubMeshes)` (Task 5).
- Produces: meshes re-saved after a collision rebuild carry the face→submesh mapping.

- [ ] **Step 1: Record submesh ids while gathering collision geometry**

In the "Build Complex" block, extend the gather loop (current code collects `vertices`/`indices` per included submesh via `ReadIndexData`):
```cpp
				std::vector<Vector3> vertices;
				std::vector<uint32> indices;
				std::vector<uint16> faceSubMeshes;

				for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
				{
					if (!m_includedSubMeshes.contains(i))
					{
						continue;
					}

					SubMesh& sub = m_mesh->GetSubMesh(i);
					// ... existing vertex/index gathering stays unchanged, then:

					const size_t indexCountBefore = indices.size();
					ReadIndexData(*sub.indexData, vertexOffset, indices);

					const size_t facesAdded = (indices.size() - indexCountBefore) / 3;
					faceSubMeshes.insert(faceSubMeshes.end(), facesAdded, i);
				}

				m_mesh->GetCollisionTree().Clear();
				m_mesh->GetCollisionTree().Build(vertices, indices, faceSubMeshes);
```
Keep the surrounding structure (the `sharedVertexData == nullptr` guard and `ReadVertexData` call) exactly as-is; the only changes are the `faceSubMeshes` vector, the `indexCountBefore` bookkeeping around the existing `ReadIndexData` call, and the 3-argument `Build`.

- [ ] **Step 2: Build**

```powershell
cmake --build build --config Debug -t mmo_edit
```
Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp
git commit -m "feat: record face submesh ids when building mesh collision"
```

---

### Task 7: Collision hit identity + Entity surface type resolution

**Files:**
- Modify: `src/shared/scene_graph/movable_object.h` (`CollisionResult` ~line 24, `ICollidable` ~line 35)
- Modify: `src/shared/scene_graph/entity.h` (declare overrides next to `TestRayCollision`)
- Modify: `src/shared/scene_graph/entity.cpp` (`TestRayCollision` ~line 530-659, new `GetSurfaceTypeAt`)

**Interfaces:**
- Consumes: `AABBTree::GetFaceSubMeshes()` (Task 5), `MaterialInterface::GetSurfaceTypeId()` (Task 3), `Entity::GetSubEntity(uint16)` / `SubEntity::GetMaterial()` (existing).
- Produces:
  - `CollisionResult::faceIndex` (`int32`, -1 = unknown) — index into the collision tree's face order.
  - `virtual uint32 ICollidable::GetSurfaceTypeAt(const CollisionResult& hit) const` (default returns 0).
  - `Entity::GetSurfaceTypeAt` override resolving face → submesh → material → surface type.

- [ ] **Step 1: Extend `CollisionResult` and `ICollidable` (movable_object.h)**

```cpp
	struct CollisionResult
	{
		bool hasCollision = false;    ///< Whether collision was detected
		Vector3 contactPoint;         ///< Point where collision occurred
		Vector3 contactNormal;        ///< Surface normal at collision point
		Vector3 a, b, c;				/// Vertices of the triangle (if applicable)
		float penetrationDepth = 0.0f;///< How deep the capsule penetrates
		float distance;
		int32 faceIndex = -1;         ///< Index of the hit face in the collidable's collision tree (-1 = unknown)
	};
```
Add to `ICollidable` (after `IsCollidable()`):
```cpp
		/// @brief Resolves the surface type id at a collision hit on this object.
		/// @param hit A collision result produced by this object's collision tests.
		/// @return The surface type id, or 0 if unknown.
		virtual uint32 GetSurfaceTypeAt(const CollisionResult& hit) const { return 0; }
```

- [ ] **Step 2: Fill `faceIndex` in `Entity::TestRayCollision` (entity.cpp)**

In the traversal (~line 561), add a tracking variable next to `closestDistance`:
```cpp
		int32 closestFaceIndex = -1;
```
Inside the `if (hitTriangle && hitDistance < closestDistance)` block (~line 617), record the face:
```cpp
						closestFaceIndex = static_cast<int32>(faceIndex);
```
In the result fill (~line 650):
```cpp
		if (foundCollision)
		{
			result.hasCollision = true;
			result.contactPoint = closestContactPoint;
			result.contactNormal = closestContactNormal;
			result.penetrationDepth = closestDistance; // Distance along ray to hit point
			result.faceIndex = closestFaceIndex;
		}
```

- [ ] **Step 3: Implement `Entity::GetSurfaceTypeAt`**

Declare in `entity.h` next to the `TestRayCollision` override:
```cpp
		/// @copydoc ICollidable::GetSurfaceTypeAt
		[[nodiscard]] uint32 GetSurfaceTypeAt(const CollisionResult& hit) const override;
```
Implement in `entity.cpp` (after `TestRayCollision`):
```cpp
	uint32 Entity::GetSurfaceTypeAt(const CollisionResult& hit) const
	{
		if (!m_mesh)
		{
			return 0;
		}

		// Map the hit face to its source submesh. Legacy collision trees have no mapping;
		// fall back to the first submesh so single-material meshes still resolve correctly.
		uint16 subMeshIndex = 0;
		const auto& faceSubMeshes = m_mesh->GetCollisionTree().GetFaceSubMeshes();
		if (hit.faceIndex >= 0 && static_cast<size_t>(hit.faceIndex) < faceSubMeshes.size())
		{
			subMeshIndex = faceSubMeshes[hit.faceIndex];
		}

		if (subMeshIndex >= GetNumSubEntities())
		{
			subMeshIndex = 0;
		}

		const SubEntity* subEntity = GetSubEntity(subMeshIndex);
		if (!subEntity)
		{
			return 0;
		}

		const MaterialPtr material = subEntity->GetMaterial();
		return material ? material->GetSurfaceTypeId() : 0;
	}
```
Check the actual name of the subentity-count accessor in `entity.h` (`GetNumSubEntities()` or `GetSubEntityCount()`) and use it. Include `"graphics/material.h"` if not already pulled in transitively.

- [ ] **Step 4: Build and run tests**

```powershell
cmake --build build --config Debug -t mmo_client mmo_edit unit_tests
build\bin\Debug\unit_tests.exe
```
Expected: compiles, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/scene_graph/movable_object.h src/shared/scene_graph/entity.h src/shared/scene_graph/entity.cpp
git commit -m "feat: collision hits carry face index; entities resolve surface type from hit submesh material"
```

---

### Task 8: Terrain tile surface type resolution (dominant splat layer)

**Files:**
- Modify: `src/shared/terrain/tile.h` (declare override near `GetMaterial()` ~line 81)
- Modify: `src/shared/terrain/tile.cpp` (implement)

**Interfaces:**
- Consumes: `ICollidable::GetSurfaceTypeAt` (Task 7), `MaterialInterface::GetLayerSurfaceTypeId`/`GetSurfaceTypeId` (Task 3), `Page::GetTerrain()` (`page.h:80`), `Terrain::GetLayerValueAt(float x, float z, uint8 layer)` (`terrain.h:110`), `Tile::GetMaterial()` (`tile.cpp:124`).
- Produces: `Tile::GetSurfaceTypeAt` — dominant splat layer at the hit point → layer surface type (0 → base surface type).

- [ ] **Step 1: Declare the override in `tile.h`**

```cpp
			/// @copydoc ICollidable::GetSurfaceTypeAt
			[[nodiscard]] uint32 GetSurfaceTypeAt(const CollisionResult& hit) const override;
```

- [ ] **Step 2: Implement in `tile.cpp`**

```cpp
	uint32 Tile::GetSurfaceTypeAt(const CollisionResult& hit) const
	{
		const MaterialPtr material = GetMaterial();
		if (!material)
		{
			return 0;
		}

		// The tile renders with one splatting material; pick the surface type of the
		// dominant splat layer at the hit position.
		Terrain& terrain = m_page.GetTerrain();

		uint8 dominantLayer = 0;
		float dominantValue = -1.0f;
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			const float value = terrain.GetLayerValueAt(hit.contactPoint.x, hit.contactPoint.z, layer);
			if (value > dominantValue)
			{
				dominantValue = value;
				dominantLayer = layer;
			}
		}

		const uint32 layerSurfaceType = material->GetLayerSurfaceTypeId(dominantLayer);
		return layerSurfaceType != 0 ? layerSurfaceType : material->GetSurfaceTypeId();
	}
```
Two things to verify while implementing (adjust in place, no design change):
1. The page member name in `Tile` (ctor takes `Page& page`; check whether it is stored as `m_page` or similar).
2. **Layer 0 semantics:** read `Terrain::GetLayerValueAt` in `terrain.cpp`. If layer 0 is stored explicitly in the coverage data, the loop above is correct as written. If layer 0 is the *implicit base* (not stored; base coverage = 1 − sum of layers 1–3), compute it that way instead:
```cpp
		float layerValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (uint8 layer = 1; layer < 4; ++layer)
		{
			layerValues[layer] = terrain.GetLayerValueAt(hit.contactPoint.x, hit.contactPoint.z, layer);
		}
		layerValues[0] = std::max(0.0f, 1.0f - layerValues[1] - layerValues[2] - layerValues[3]);
```
Then argmax over `layerValues`.

- [ ] **Step 3: Build**

```powershell
cmake --build build --config Debug -t mmo_client mmo_edit
```
Expected: compiles.

- [ ] **Step 4: Commit**

```bash
git add src/shared/terrain/tile.h src/shared/terrain/tile.cpp
git commit -m "feat: terrain tiles resolve surface type from dominant splat layer"
```

---

### Task 9: Floor surface query + SoundEntry-based footsteps in GamePlayerC

**Files:**
- Create: `src/shared/game_client/floor_surface_query.h`
- Create: `src/shared/game_client/floor_surface_query.cpp`
- Modify: `src/shared/game_client/game_player_c.h` (ctor ~line 26, member, method declarations)
- Modify: `src/shared/game_client/game_player_c.cpp` (`OnFootstep` ~line 551)
- Modify: `src/mmo_client/game_states/world_state.cpp` (`GamePlayerC` creation ~line 2069)

**Interfaces:**
- Consumes: `ICollidable::GetSurfaceTypeAt` (Tasks 7/8), `SoundEntryPlayer::PlayEntry(uint32, const Vector3&)` (existing), `proto_client::Project::surfaceTypes` (Task 1), `Scene::CreateAABBQuery` (existing, used the same way in `UnitMovement::SweepMultiCast`, `unit_movement.cpp:2561`).
- Produces: `uint32 ResolveFloorSurfaceType(Scene& scene, const Vector3& position, const MovableObject* ignore = nullptr)`.

- [ ] **Step 1: Create `floor_surface_query.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	class Scene;
	class MovableObject;

	/// @brief Resolves the surface type id of the floor beneath a world position by casting
	///        a short downward ray against collidable scene objects (terrain tiles resolve
	///        their dominant splat layer, entities the hit submesh material).
	/// @param scene The scene to query.
	/// @param position The world position (typically a unit's feet position).
	/// @param ignore Optional movable to skip (e.g. the querying unit's own entity).
	/// @return The surface type id at the floor hit, or 0 when nothing resolved.
	uint32 ResolveFloorSurfaceType(Scene& scene, const Vector3& position, const MovableObject* ignore = nullptr);
}
```

- [ ] **Step 2: Create `floor_surface_query.cpp`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "floor_surface_query.h"

#include "math/aabb.h"
#include "math/ray.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene.h"

#include <limits>

namespace mmo
{
	uint32 ResolveFloorSurfaceType(Scene& scene, const Vector3& position, const MovableObject* ignore)
	{
		// Short vertical segment: slightly above the feet down to well below any step height.
		const Vector3 start = position + Vector3(0.0f, 0.5f, 0.0f);
		const Vector3 end = position - Vector3(0.0f, 1.5f, 0.0f);
		const float maxDistanceSq = (end - start).GetSquaredLength();

		AABB queryBounds(
			Vector3(start.x - 0.1f, end.y, start.z - 0.1f),
			Vector3(start.x + 0.1f, start.y, start.z + 0.1f));

		const auto query = scene.CreateAABBQuery(queryBounds);
		if (!query)
		{
			return 0;
		}

		query->Execute(*query);

		const Ray ray(start, end);

		uint32 resultSurfaceType = 0;
		float closestDistanceSq = std::numeric_limits<float>::max();

		for (const auto& movable : query->GetLastResult())
		{
			if (movable == ignore)
			{
				continue;
			}

			const ICollidable* collidable = movable->GetCollidable();
			if (!collidable || !collidable->IsCollidable())
			{
				continue;
			}

			CollisionResult hit{};
			if (!collidable->TestRayCollision(ray, hit))
			{
				continue;
			}

			// Reject hits beyond the segment (implementations may treat the ray as infinite).
			const float distanceSq = (hit.contactPoint - start).GetSquaredLength();
			if (distanceSq > maxDistanceSq || distanceSq >= closestDistanceSq)
			{
				continue;
			}

			closestDistanceSq = distanceSq;
			resultSurfaceType = collidable->GetSurfaceTypeAt(hit);
		}

		return resultSurfaceType;
	}
}
```
Mirror the exact query idiom from `UnitMovement::SweepMultiCast` (`unit_movement.cpp:2561-2568`): `scene.CreateAABBQuery(bounds)` → `query->Execute(*query)` → `query->GetLastResult()`. If `MovableObject::GetCollidable()` in that code is called on a pointer type other than what `GetLastResult()` yields, match it.

- [ ] **Step 3: Extend `GamePlayerC` (game_player_c.h)**

Forward-declare `class SoundEntryPlayer;` next to `class IAudio;` (~line 18). Extend the ctor:
```cpp
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, const proto_client::Project& project, uint32 map, IAudio* audio, SoundEntryPlayer* soundEntryPlayer)
			: GameUnitC(scene, netDriver, project, map)
			, m_audio(audio)
			, m_soundEntryPlayer(soundEntryPlayer)
		{
		}
```
Add member next to `m_audio` (find it in the private section) and a private method declaration next to `OnFootstep`:
```cpp
		SoundEntryPlayer* m_soundEntryPlayer = nullptr;
```
```cpp
		/// @brief Legacy fallback footstep playback (hardcoded ground WAVs) used while
		///        surface types / footstep sound entries are not authored yet.
		void PlayLegacyFootstepSound();
```

- [ ] **Step 4: Rewrite `OnFootstep` (game_player_c.cpp ~line 551)**

Add includes at the top of the file:
```cpp
#include "floor_surface_query.h"
#include "sound_entry_player.h"
```
Replace the body of `OnFootstep`, moving the existing hardcoded playback (the `footstepSounds` array through the pitch/volume assignment) verbatim into the new `PlayLegacyFootstepSound()`:
```cpp
	void GamePlayerC::OnFootstep(const AnimationNotify& notify)
	{
		// Resolve the surface type of the floor under the player and play its assigned
		// footstep sound entry. Anything unresolved falls back to the legacy sounds so
		// footsteps keep working while content is not authored yet.
		uint32 footstepSoundId = 0;

		const uint32 surfaceTypeId = ResolveFloorSurfaceType(m_scene, GetPosition(), m_entity);
		if (surfaceTypeId != 0)
		{
			if (const auto* surfaceType = m_project.surfaceTypes.getById(surfaceTypeId))
			{
				footstepSoundId = surfaceType->footstep_sound();
			}
		}

		if (footstepSoundId != 0 && m_soundEntryPlayer)
		{
			m_soundEntryPlayer->PlayEntry(footstepSoundId, GetPosition());
			return;
		}

		PlayLegacyFootstepSound();
	}

	void GamePlayerC::PlayLegacyFootstepSound()
	{
		if (!m_audio)
		{
			return;
		}

		// ... the existing hardcoded playback code, moved verbatim ...
	}
```
Verify the protected member names inherited from `GameObjectC`/`GameUnitC` while editing: the scene reference (`m_scene`), the entity (`m_entity` — already used in `RegisterFootstepHandlers`), and the client data project (`m_project` — already used in `GetClass()` at line 289). If the scene member has a different name, `GetSceneNode()->GetScene()` is the fallback.

- [ ] **Step 5: Wire up in `world_state.cpp`**

Line ~2069:
```cpp
				case ObjectTypeId::Player:
					object = std::make_shared<GamePlayerC>(*m_scene, *this, m_project, g_mapId, &m_audio, &m_soundEntryPlayer);
					break;
```
(`m_soundEntryPlayer` is a `SoundEntryPlayer&` member of `WorldState`, declared at `world_state.h:621`.) Check for any other `GamePlayerC` construction sites:
```powershell
# from repo root
Select-String -Path src -Pattern "GamePlayerC>" -SimpleMatch -Recurse
```
and update them the same way (as of planning there is exactly one, in world_state.cpp).

- [ ] **Step 6: Build everything and run tests**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_client mmo_edit unit_tests
build\bin\Debug\unit_tests.exe
```
Expected: compiles, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client/floor_surface_query.h src/shared/game_client/floor_surface_query.cpp src/shared/game_client/game_player_c.h src/shared/game_client/game_player_c.cpp src/mmo_client/game_states/world_state.cpp
git commit -m "feat: surface-type-driven footstep sounds via SoundEntryPlayer with legacy fallback"
```

---

### Task 10: Full build + manual verification checklist

**Files:** none (verification only).

- [ ] **Step 1: Full build**

```powershell
cmake --build build --config Debug
build\bin\Debug\unit_tests.exe
```
Expected: whole solution builds; all unit tests pass.

- [ ] **Step 2: Manual editor verification (report results, do not skip)**

Launch `mmo_edit` and verify:
1. **Surface Type Editor** window opens (Window menu), can create a "Grass" entry and assign an existing footstep-suitable sound entry (create a SoundEntry pointing at the six `Sound/Character/Footsteps/ground_*.WAV` files with is_3d, pitch range ~0.7–1.3, volume ~0.35 if none exists). Save the project; confirm a `surface_types` file appears in the project data directory and survives an editor restart.
2. **Material editor**: open a terrain splatting material → "Surface Type" section shows base + 4 layer dropdowns; assign layer surface types; save; reopen; values persist. Open an old unrelated material → loads fine, dropdowns show "(None)".
3. **Material instance editor**: override checkbox gates the dropdowns; persists across save/reopen.
4. **Mesh editor**: open a multi-submesh mesh, "Build Complex" + save → re-open, no errors (collision tree now carries submesh ids).
5. **Client data export**: run the editor's client data export; confirm `surface_types` lands in the client data output.
6. **In game** (after export, with a zone whose terrain material has layer surface types authored): walk on terrain — footsteps play the assigned sound entry; walk on unauthored ground — legacy WAVs still play.

- [ ] **Step 3: Update memory + report**

Report verification results to the user, including anything that failed or was deferred.
