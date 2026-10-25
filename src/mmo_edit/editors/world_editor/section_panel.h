// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/vector3.h"
#include "pcg/section_bake.h"

#include <imgui.h>

#include <array>
#include <vector>

namespace mmo
{
	class WorldEditorInstance;

	/// @brief Authoring panel of a section world: section size, variants, prop variation rules, connected prop
	///        runs and the bake into the section asset. Also draws the variant rectangles and a line through each
	///        run's members into the viewport.
	/// @details Every edit is written to the .hsecdef right away (the definition is the editable source
	///          besides the world). The stale indicator compares the baked asset's source stamp with a
	///          stamp of the current files; it is refreshed on open, after saving, after baking, after a
	///          definition edit and on request, not every frame (hashing the pages reads several MB).
	class SectionPanel final : public NonCopyable
	{
	public:
		/// @brief Loads the section definition of the world.
		/// @param world The section world. Must outlive the panel.
		explicit SectionPanel(WorldEditorInstance& world);

	public:
		/// @brief Draws the panel window.
		/// @param id ImGui window id.
		void Draw(const String& id);

		/// @brief Draws each variant rectangle, its falloff band and its centre line (+Z arrow), and a line
		///        through the members of each run, over the viewport image.
		/// @param drawList Draw list of the viewport window.
		/// @param viewportMin Screen position of the viewport image's top left corner.
		/// @param viewportSize Size of the viewport image.
		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize);

		/// @brief Called after the world was saved: refreshes the stale indicator and the asset list.
		void OnWorldSaved();

	private:
		/// @brief Writes the definition to the .hsecdef and refreshes the stale indicator.
		void Commit();

		/// @brief Recomputes the current source stamp and reads the baked asset's stamp.
		void RefreshStamp();

		/// @brief Collects the distinct assets placed in the section world.
		void RefreshPlacedAssets();

		/// @brief Saves the world, then bakes it.
		void Bake();

		/// @brief The smallest multiple of SectionVariantSpacing (>= 0) no variant uses yet.
		[[nodiscard]] float NextFreeVariantSlot() const;

		/// @brief Validation message for the current definition (empty when it can be baked).
		[[nodiscard]] String ValidateDefinition() const;

		void DrawSizeSection();
		void DrawVariantSection();
		void DrawRuleSection();
		void DrawRunSection();
		void DrawBakeSection();

		/// @brief Index of the variant whose capture rectangle contains a world position, or -1.
		[[nodiscard]] int32 FindVariantAt(float x, float z) const;

		/// @brief Validates the selected map entities as run members: at least one, all of one variant and none
		///        in another run than exceptRun. Sets the status message on failure.
		/// @param ids Receives the selected unique ids, sorted.
		/// @param exceptRun Run index whose members may be selected again (-1 = none).
		/// @return True if the selection can be used.
		bool CollectSelectionForRun(std::vector<uint64>& ids, int32 exceptRun);

		/// @brief Creates a run "Run N" from the selected map entities.
		void CreateRunFromSelection();

		/// @brief Adds the selected map entities to an existing run.
		void AddSelectionToRun(size_t runIndex);

		/// @brief Projects a world position into the viewport. False if it lies behind the camera.
		bool ProjectToScreen(const Vector3& position, ImVec2& out) const;

		/// @brief Terrain height at a world position (0 where the terrain is not loaded).
		float GroundHeight(float x, float z) const;

		/// @brief Draws a polyline along the ground from a to b.
		void DrawGroundLine(ImDrawList* drawList, float ax, float az, float bx, float bz, ImU32 color, float thickness) const;

	private:
		WorldEditorInstance& m_world;
		String m_worldPath;
		pcg::SectionDefinition m_definition;
		bool m_definitionLoaded = false;
		String m_status;
		bool m_statusIsError = false;

		bool m_hasBake = false;
		uint64 m_bakedStamp = 0;
		uint64 m_currentStamp = 0;
		/// Albedo texture per paint layer of the terrain default material (what the bake will record).
		std::array<String, 4> m_layerTextures;

		/// Distinct asset paths placed in the section world, sorted.
		std::vector<String> m_placedAssets;
		/// Text of the manual swap asset input per rule index.
		std::vector<String> m_swapInputs;

		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2 m_overlayViewportMin;
		ImVec2 m_overlayViewportSize;
	};
}
