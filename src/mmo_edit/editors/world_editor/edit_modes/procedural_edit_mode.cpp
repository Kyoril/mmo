// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "procedural_edit_mode.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "math/matrix4.h"
#include "math/ray.h"
#include "math/vector4.h"
#include "pcg/graph/hashing.h"
#include "pcg/oasis_recipe.h"
#include "pcg/pcg_random.h"
#include "pcg/section_path.h"
#include "pcg/terrain_contribution.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"
#include "editor_windows/asset_picker_widget.h"
#include "../pcg/control_details.h"
#include "../pcg/text_parsing.h"

#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Asset path of the shared recipe.
		const char* const RecipeFileName = "Editor/Pcg/OasisRecipe.json";

		/// Extension of control record files.
		const char* const RecordExtension = ".hpcg";

		/// Extension of placed control files (JSON).
		const char* const PlacedRecordExtension = ".hpcgc";

		/// Side of the square of a new placed loop, and the length of a new placed spline, in metres.
		constexpr float NewControlSize = 32.0f;

		/// Half extents of a new placed volume, in metres.
		constexpr float NewVolumeHalfExtent = 16.0f;

		/// Half extents on x and z of a new placed rectangle (20 m x 20 m), and its unused y half extent, in metres.
		constexpr float NewRectangleHalfExtent = 10.0f;
		constexpr float NewRectangleHalfHeight = 0.5f;

		/// Length of one drawn segment of a rectangle's outline on the ground, and the most segments per side.
		constexpr float RectangleOutlineStep = 2.0f;
		constexpr int32 RectangleOutlineMaxSegments = 64;

		/// The handles of a rectangle, in drawing and hit-test order (corners before sides: a corner wins at equal distance).
		const pcg::graph::RectangleHandle RectangleHandles[] = {
			pcg::graph::RectangleHandle::CornerNegNeg, pcg::graph::RectangleHandle::CornerPosNeg,
			pcg::graph::RectangleHandle::CornerPosPos, pcg::graph::RectangleHandle::CornerNegPos,
			pcg::graph::RectangleHandle::SideNegX, pcg::graph::RectangleHandle::SidePosX,
			pcg::graph::RectangleHandle::SideNegZ, pcg::graph::RectangleHandle::SidePosZ
		};

		/// Side of the square area of a new graph instance, in metres.
		constexpr float NewGraphInstanceSize = 128.0f;

		/// Screen radius of the handle of a placed volume or point, in pixels.
		constexpr float TransformHandleRadius = 9.0f;

		constexpr float PiValue = 3.14159265358979f;
		constexpr float DefaultRadius = 40.0f;
		constexpr uint32 DefaultPointCount = 8;

		/// Distance between the two points of a new section path, in metres.
		constexpr float NewPathLength = 20.0f;

		/// Radius of a new section loop, in metres.
		constexpr float NewLoopRadius = 60.0f;

		/// Control points of a new section loop.
		constexpr uint32 NewLoopPointCount = 8;

		/// Overlay frames between attempts to load the terrain under a loop without heights.
		constexpr uint64 HeightAttemptIntervalFrames = 30;

		/// Failed attempts after which a loop without heights takes them from the loaded terrain.
		constexpr uint32 HeightAttemptLimit = 10;

		/// An oasis keeps at least this many loop points.
		constexpr uint32 MinLoopPoints = 3;

		/// A section path keeps at least this many points.
		constexpr uint32 MinPathPoints = 2;

		const ImVec4 StatusOkColor(0.6f, 0.9f, 0.6f, 1.0f);
		const ImVec4 StatusWarningColor(1.0f, 0.8f, 0.3f, 1.0f);
		const ImVec4 StatusErrorColor(1.0f, 0.4f, 0.3f, 1.0f);

		/// The mass layer group of the section path parameters. Returns true if a value changed.
		bool DrawSectionMassParams(pcg::SectionPathParams& params)
		{
			bool changed = ImGui::Checkbox("Mass behind the wall##massEnabled", &params.massEnabled);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Raises a broad terrain mass on the section's +X side (away from the floor): it rises from the\n"
					"floor-side edge, holds, and falls back to the ground at the depth. Within the section the higher of\n"
					"section and mass wins, so the facade keeps its detail where it stands above the mass.");
			}
			if (!params.massEnabled)
			{
				return changed;
			}

			ImGui::Indent();
			changed = ImGui::DragFloat("Mass height (m)##massHeight", &params.massHeight, 0.25f, 0.0f, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp) || changed;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Height above the centre line height, before noise.");
			}
			changed = ImGui::DragFloat("Mass depth (m)##massDepth", &params.massDepth, 0.5f, 1.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp) || changed;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How far the mass reaches, from the section's floor-side edge towards +X. A deep mass grows the\n"
					"influence (and the overlap rects) of the path by the same amount.");
			}
			changed = ImGui::SliderFloat("Mass rise##massRise", &params.massRise, 0.01f, 0.7f, "%.2f", ImGuiSliderFlags_AlwaysClamp) || changed;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Fraction of the depth over which the mass rises to full height. Larger keeps more of the facade\n"
					"visible in front of it.");
			}
			changed = ImGui::DragFloat("Noise amplitude (m)##massNoiseAmplitude", &params.massNoiseAmplitude, 0.1f, 0.0f, 300.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp) || changed;
			changed = ImGui::DragFloat("Noise wavelength (m)##massNoiseWavelength", &params.massNoiseWavelength, 0.5f, 1.0f, 2000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp) || changed;
			changed = ImGui::DragFloat("Detail amplitude (m)##massDetailAmplitude", &params.massDetailAmplitude, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp) || changed;
			changed = ImGui::DragFloat("Detail wavelength (m)##massDetailWavelength", &params.massDetailWavelength, 0.25f, 1.0f, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp) || changed;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("World-anchored noise: moving a control point never changes the mass far from it.");
			}

			const char* layers[] = { "Auto (wall layer)", "Layer 0", "Layer 1", "Layer 2", "Layer 3" };
			int layer = std::clamp(static_cast<int>(params.massPaintLayer) + 1, 0, 4);
			if (ImGui::Combo("Mass paint##massPaintLayer", &layer, layers, IM_ARRAYSIZE(layers)))
			{
				params.massPaintLayer = static_cast<int8>(layer - 1);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Section layer painted on the mass's slopes (none below 10 deg, full from 30 deg). Auto picks the\n"
					"layer the straight variants paint most above their floor.");
			}
			ImGui::Unindent();
			return changed;
		}

		/// The facade variety group of the section path parameters. Returns true if a value changed.
		bool DrawSectionFacadeParams(pcg::SectionPathParams& params)
		{
			bool changed = ImGui::Checkbox("Mirror copies##mirrorCopies", &params.mirrorCopies);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Lays about half of the copies (by hash) reversed along their length, props reflected,\n"
					"so a few variants repeat less visibly. The facade keeps facing the floor side.");
			}
			changed = ImGui::SliderFloat("Facade height jitter##facadeHeightJitter", &params.facadeHeightJitter, 0.0f, 0.9f, "%.2f",
				ImGuiSliderFlags_AlwaysClamp) || changed;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Scales each copy's section heights by a hashed factor in [1 - j, 1 + j]; props keep their\n"
					"height above the scaled ground.");
			}
			return changed;
		}

		String ToAssetPath(const std::filesystem::path& path)
		{
			String result = path.generic_string();
			while (!result.empty() && result.back() == '/')
			{
				result.pop_back();
			}
			return result;
		}

		String FormatId(const uint64 id)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(id));
			return String(buffer);
		}

		/// Tags as "a, b, c".
		String JoinTags(const std::set<String>& tags)
		{
			String text;
			for (const String& tag : tags)
			{
				text += (text.empty() ? "" : ", ") + tag;
			}
			return text;
		}

		/// "a, b ,c" -> { a, b, c } (trimmed, empty entries dropped).
		std::set<String> SplitTags(const String& text)
		{
			std::set<String> tags;
			size_t start = 0;
			while (start <= text.size())
			{
				const size_t end = std::min(text.find(',', start), text.size());
				String tag = text.substr(start, end - start);
				const size_t first = tag.find_first_not_of(" \t");
				const size_t last = tag.find_last_not_of(" \t");
				if (first != String::npos)
				{
					tags.insert(tag.substr(first, last - first + 1));
				}
				start = end + 1;
			}
			return tags;
		}

		/// Display name of a control shape.
		const char* ShapeName(const pcg::graph::ControlShape shape)
		{
			switch (shape)
			{
			case pcg::graph::ControlShape::ClosedLoop:
				return "Loop";
			case pcg::graph::ControlShape::OpenSpline:
				return "Spline";
			case pcg::graph::ControlShape::Volume:
				return "Volume";
			case pcg::graph::ControlShape::Point:
				return "Point";
			case pcg::graph::ControlShape::Rectangle:
				return "Rectangle";
			}
			return "?";
		}

		/// File name without folder and extension.
		String FileStem(const String& path)
		{
			return std::filesystem::path(path).stem().string();
		}

		/// A float parameter row of the details panel.
		struct FloatRow
		{
			const char* label;
			float pcg::OasisParams::* member;
			float speed;
			float min;
			float max;
		};

		const FloatRow s_basinRows[] = {
			{ "Basin depth", &pcg::OasisParams::basinDepth, 0.1f, 0.0f, 60.0f },
			{ "Rim width", &pcg::OasisParams::rimWidth, 0.1f, 0.5f, 100.0f },
			{ "Outer falloff", &pcg::OasisParams::outerFalloff, 0.1f, 0.5f, 100.0f },
			{ "Terrace sharpness", &pcg::OasisParams::terraceSharpness, 0.01f, 0.0f, 1.0f },
			{ "Floor noise", &pcg::OasisParams::floorNoiseAmplitude, 0.01f, 0.0f, 10.0f },
			{ "Rim percentile", &pcg::OasisParams::rimPercentile, 0.01f, 0.0f, 1.0f },
		};

		const FloatRow s_waterRows[] = {
			{ "Water inset", &pcg::OasisParams::waterInset, 0.1f, 2.0f, 100.0f },
			{ "Water depth", &pcg::OasisParams::waterDepth, 0.05f, 0.0f, 20.0f },
		};

		const FloatRow s_paintRows[] = {
			{ "Grass band", &pcg::OasisParams::grassBand, 0.1f, 0.0f, 100.0f },
			{ "Dirt band", &pcg::OasisParams::dirtBand, 0.1f, 0.0f, 100.0f },
			{ "Band noise", &pcg::OasisParams::bandNoiseAmplitude, 0.05f, 0.0f, 20.0f },
		};

		const FloatRow s_rockRows[] = {
			{ "Rock spacing", &pcg::OasisParams::rockSpacing, 0.1f, 1.0f, 100.0f },
			{ "Rock offset", &pcg::OasisParams::rockOffset, 0.1f, -20.0f, 20.0f },
			{ "Rock offset jitter", &pcg::OasisParams::rockOffsetJitter, 0.05f, 0.0f, 20.0f },
			{ "Rock yaw jitter (deg)", &pcg::OasisParams::rockYawJitterDeg, 0.5f, 0.0f, 180.0f },
			{ "Rock scale min", &pcg::OasisParams::rockScaleMin, 0.01f, 0.1f, 10.0f },
			{ "Rock scale max", &pcg::OasisParams::rockScaleMax, 0.01f, 0.1f, 10.0f },
			{ "Cliff sink", &pcg::OasisParams::rockSink, 0.05f, 0.0f, 30.0f },
			{ "Shelf sink", &pcg::OasisParams::shelfSink, 0.05f, 0.0f, 30.0f },
			{ "Clump sink", &pcg::OasisParams::clumpSink, 0.05f, 0.0f, 30.0f },
			{ "Cliff weight", &pcg::OasisParams::cliffWeight, 0.01f, 0.0f, 10.0f },
			{ "Shelf weight", &pcg::OasisParams::shelfWeight, 0.01f, 0.0f, 10.0f },
			{ "Clump weight", &pcg::OasisParams::clumpWeight, 0.01f, 0.0f, 10.0f },
		};

		const FloatRow s_vegetationRows[] = {
			{ "Palm cell", &pcg::OasisParams::palmCell, 0.1f, 1.0f, 50.0f },
			{ "Palm density", &pcg::OasisParams::palmDensity, 0.01f, 0.0f, 1.0f },
			{ "Palm near water boost", &pcg::OasisParams::palmNearWaterBoost, 0.01f, 0.0f, 10.0f },
			{ "Palm scale min", &pcg::OasisParams::palmScaleMin, 0.01f, 0.1f, 10.0f },
			{ "Palm scale max", &pcg::OasisParams::palmScaleMax, 0.01f, 0.1f, 10.0f },
			{ "Bush cell", &pcg::OasisParams::bushCell, 0.1f, 1.0f, 50.0f },
			{ "Bush density", &pcg::OasisParams::bushDensity, 0.01f, 0.0f, 1.0f },
			{ "Water plant band", &pcg::OasisParams::waterPlantBand, 0.1f, 0.0f, 50.0f },
			{ "Water plant density", &pcg::OasisParams::waterPlantDensity, 0.01f, 0.0f, 1.0f },
			{ "Max slope (deg)", &pcg::OasisParams::maxSlopeDeg, 0.5f, 0.0f, 90.0f },
		};

		/// @return True if any value changed.
		template<size_t N>
		bool DrawRows(pcg::OasisParams& params, const FloatRow (&rows)[N])
		{
			bool changed = false;
			for (const FloatRow& row : rows)
			{
				changed = ImGui::DragFloat(row.label, &(params.*row.member), row.speed, row.min, row.max, "%.2f") || changed;
			}
			return changed;
		}

		void ComputeBounds(const PathSpline& loop, pcg::WorldRect& rect)
		{
			rect = {};
			if (loop.points.empty())
			{
				return;
			}

			rect.minX = rect.maxX = loop.points.front().x;
			rect.minZ = rect.maxZ = loop.points.front().z;
			for (const SplinePoint& point : loop.points)
			{
				rect.minX = std::min(rect.minX, point.x);
				rect.maxX = std::max(rect.maxX, point.x);
				rect.minZ = std::min(rect.minZ, point.z);
				rect.maxZ = std::max(rect.maxZ, point.z);
			}
		}

		/// Randomized seed for the "Randomize" buttons. Only picks an input; the generated result stays a
		/// pure function of the seed.
		uint64 RandomSeed()
		{
			std::random_device device;
			return (static_cast<uint64>(device()) << 32) | static_cast<uint64>(device());
		}

		/// Makes a spline point a sharp corner: manual tangents with zero handles, so the curve runs straight to
		/// its neighbours (graph instance areas are polygons).
		void MakeSharpCorner(SplinePoint& point)
		{
			point.autoTangents = false;
			point.inX = point.inY = point.inZ = 0.0f;
			point.outX = point.outY = point.outZ = 0.0f;
		}

		/// Draws an influence rect on the ground.
		template<typename Project, typename Height>
		void DrawRect(ImDrawList& drawList, const pcg::WorldRect& rect, const ImU32 color, const Project& project, const Height& height)
		{
			const float corners[4][2] = { { rect.minX, rect.minZ }, { rect.maxX, rect.minZ }, { rect.maxX, rect.maxZ }, { rect.minX, rect.maxZ } };
			ImVec2 screen[4];
			for (int c = 0; c < 4; ++c)
			{
				if (!project(Vector3(corners[c][0], height(corners[c][0], corners[c][1]), corners[c][1]), screen[c]))
				{
					return;
				}
			}

			drawList.AddQuad(screen[0], screen[1], screen[2], screen[3], color, 1.0f);
		}
	}

	ProceduralEditMode::ProceduralEditMode(IWorldEditor& editor, Camera& camera)
		: WorldEditMode(editor)
		, m_camera(camera)
		, m_splineEditor(camera, [this](const float x, const float z) { return GetHandleHeight(x, z); })
	{
	}

	ProceduralEditMode::~ProceduralEditMode()
	{
		CancelBackgroundRebuilds();
	}

	const char* ProceduralEditMode::GetName() const
	{
		static const char* s_name = "Procedural";
		return s_name;
	}

	String ProceduralEditMode::MakeFileName(const std::filesystem::path& folder, const uint64 id)
	{
		return ToAssetPath(folder) + "/" + FormatId(id) + RecordExtension;
	}

	String ProceduralEditMode::MakePlacedFileName(const std::filesystem::path& folder, const uint64 id)
	{
		return ToAssetPath(folder) + "/" + FormatId(id) + PlacedRecordExtension;
	}

	const pcg::OwnedContent& ProceduralEditMode::Control::NoContent()
	{
		static const pcg::OwnedContent s_none;
		return s_none;
	}

	void ProceduralEditMode::MarkChanged(Control& control)
	{
		control.dirty = true;
		if (control.kind == ControlKind::Placed)
		{
			++m_placedRevision;
		}
	}

	void ProceduralEditMode::OnControlsChanged()
	{
		// Any add, removal or reorder may move the placed controls that resolved controls point at.
		++m_placedRevision;
	}

	void ProceduralEditMode::ApplyPendingTags()
	{
		if (!m_tagsEditing)
		{
			return;
		}

		m_tagsEditing = false;
		const int32 index = FindControl(m_tagsControlId);
		if (index < 0 || m_controls[index].kind != ControlKind::Placed)
		{
			return;
		}

		Control& control = m_controls[index];
		std::set<String> tags = SplitTags(m_tagsBuffer);
		if (tags != control.placed.extraTags)
		{
			control.placed.extraTags = std::move(tags);
			MarkChanged(control);
		}
	}

	bool ProceduralEditMode::IsPlacedVolume(const Control& control)
	{
		const pcg::graph::ControlType* type = m_controlLibrary.Find(control.placed.typePath);
		return !type || type->shape != pcg::graph::ControlShape::Point;
	}

	bool ProceduralEditMode::IsPlacedRectangle(const Control& control)
	{
		if (!control.HasTransform())
		{
			return false;
		}

		const pcg::graph::ControlType* type = m_controlLibrary.Find(control.placed.typePath);
		return type && type->shape == pcg::graph::ControlShape::Rectangle;
	}

	const char* ProceduralEditMode::DescribeKind(const Control& control)
	{
		switch (control.kind)
		{
		case ControlKind::Oasis:
			return "An oasis loop";
		case ControlKind::SectionPath:
			return "A section path";
		case ControlKind::Placed:
			return "A placed control";
		case ControlKind::GraphInstance:
			return "A graph instance";
		}
		return "A control";
	}

	uint32 ProceduralEditMode::GetMinPoints(const Control& control)
	{
		return (control.kind == ControlKind::Oasis || control.Spline().closed) ? MinLoopPoints : MinPathPoints;
	}

	void ProceduralEditMode::SetStatus(const String& text, const StatusLevel level)
	{
		m_status = text;
		m_statusLevel = level;
	}

	size_t ProceduralEditMode::GetOasisCount() const
	{
		return static_cast<size_t>(std::count_if(m_controls.begin(), m_controls.end(),
			[](const Control& control) { return control.kind == ControlKind::Oasis; }));
	}

	int32 ProceduralEditMode::FindControl(const uint64 id) const
	{
		for (size_t i = 0; i < m_controls.size(); ++i)
		{
			if (m_controls[i].Id() == id)
			{
				return static_cast<int32>(i);
			}
		}

		return -1;
	}

	void ProceduralEditMode::SelectControl(const int32 index)
	{
		// A tags edit in progress belongs to the control it was typed for.
		ApplyPendingTags();
		m_selected = index;
		m_splineEditor.CancelDrag();
		EndRectangleDrag();
		ClearPointSelection();
		ClearTransformSelection();
		m_splineEditor.Select(index, -1);

		// A volume or point has no spline to click; the gizmo edits it as soon as it is picked.
		if (index >= 0 && index < static_cast<int32>(m_controls.size()) && m_controls[index].HasTransform())
		{
			SelectTransformInEditor(index);
		}
	}

	void ProceduralEditMode::OnDeactivate()
	{
		// Other modes write the terrain: the running rebuild must end first.
		WaitForBackgroundRebuild();
		WorldEditMode::OnDeactivate();
		m_splineEditor.CancelDrag();
		EndRectangleDrag();
		// A point or transform selection is meaningless outside this mode (no handles, no overlay).
		ClearPointSelection();
		ClearTransformSelection();
		m_brushPositionValid = false;
	}

	void ProceduralEditMode::SelectTransformInEditor(const int32 controlIndex)
	{
		// Forget the old handles first: their Deselect() must not reset what was just picked.
		m_pointSelectable = nullptr;
		m_transformSelectable = nullptr;
		m_worldEditor.ClearSelection();
		m_splineEditor.Select(controlIndex, -1);

		if (controlIndex < 0 || controlIndex >= static_cast<int32>(m_controls.size()) || !m_controls[controlIndex].HasTransform())
		{
			return;
		}

		const Control& control = m_controls[controlIndex];
		auto selectable = std::make_unique<SelectedControlTransform>(*this, control.Id(), IsPlacedVolume(control));
		m_transformSelectable = selectable.get();
		m_worldEditor.AddToSelection(std::move(selectable));
	}

	void ProceduralEditMode::ClearTransformSelection()
	{
		if (m_transformSelectable)
		{
			m_transformSelectable = nullptr;
			m_worldEditor.ClearSelection();
		}
	}

	pcg::graph::ControlInstance* ProceduralEditMode::FindTransformControl(const uint64 controlId)
	{
		const int32 index = FindControl(controlId);
		if (index < 0 || !m_controls[index].HasTransform())
		{
			return nullptr;
		}

		return &m_controls[index].placed;
	}

	void ProceduralEditMode::OnControlTransformEdited(const uint64 controlId)
	{
		const int32 index = FindControl(controlId);
		if (index >= 0)
		{
			MarkChanged(m_controls[index]);
		}
	}

	void ProceduralEditMode::OnControlTransformDeselected(const SelectedControlTransform& selectable)
	{
		if (&selectable == m_transformSelectable)
		{
			m_transformSelectable = nullptr;
		}
	}

	void ProceduralEditMode::SelectPointInEditor(const int32 controlIndex, const int32 pointIndex)
	{
		// Forget the old handle first: its Deselect() must not reset the point that was just picked.
		m_pointSelectable = nullptr;
		m_worldEditor.ClearSelection();
		m_splineEditor.Select(controlIndex, pointIndex);

		if (controlIndex < 0 || controlIndex >= static_cast<int32>(m_controls.size()) ||
			pointIndex < 0 || pointIndex >= static_cast<int32>(m_controls[controlIndex].Spline().points.size()))
		{
			return;
		}

		auto selectable = std::make_unique<SelectedSplinePoint>(*this, m_controls[controlIndex].Id(), static_cast<size_t>(pointIndex));
		m_pointSelectable = selectable.get();
		m_worldEditor.AddToSelection(std::move(selectable));
	}

	void ProceduralEditMode::ClearPointSelection()
	{
		if (m_pointSelectable)
		{
			m_pointSelectable = nullptr;
			m_worldEditor.ClearSelection();
		}

		m_splineEditor.Select(m_splineEditor.GetSelectedSpline(), -1);
	}

	bool ProceduralEditMode::EnsureHeights(Control& control, const bool retry)
	{
		if (control.kind != ControlKind::Oasis)
		{
			// Section paths are created with heights.
			return true;
		}

		pcg::OasisRecord& record = control.oasis;
		if (record.heightsValid)
		{
			return true;
		}

		if (!m_worldEditor.HasTerrain())
		{
			// Nothing to stand on: the zero heights are as good as any.
			record.heightsValid = true;
			return true;
		}

		if (retry && m_overlayFrame < control.nextHeightAttemptFrame)
		{
			return false;
		}

		pcg::WorldRect bounds;
		ComputeBounds(record.loop, bounds);
		String error;
		if (!m_worldEditor.EnsurePagesLoaded(bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ, error))
		{
			++control.heightAttempts;
			control.nextHeightAttemptFrame = m_overlayFrame + HeightAttemptIntervalFrames;
			if (!retry || control.heightAttempts >= HeightAttemptLimit)
			{
				FillHeightsFallback(control);
				return true;
			}
			return false;
		}

		// No edit can have happened in between: the loop is not editable until now (IsLoopEditable).
		for (SplinePoint& point : record.loop.points)
		{
			point.y = m_worldEditor.GetTerrain()->GetSmoothHeightAt(point.x, point.z);
			point.inY = 0.0f;
			point.outY = 0.0f;
		}
		record.heightsValid = true;
		return true;
	}

	void ProceduralEditMode::FillHeightsFallback(Control& control)
	{
		pcg::OasisRecord& record = control.oasis;
		size_t missing = 0;
		for (SplinePoint& point : record.loop.points)
		{
			float height = 0.0f;
			if (!m_worldEditor.GetTerrain() || !m_worldEditor.GetTerrain()->TryGetSmoothHeightAt(point.x, point.z, height))
			{
				height = 0.0f;
				++missing;
			}
			point.y = height;
			point.inY = 0.0f;
			point.outY = 0.0f;
		}
		record.heightsValid = true;

		SetStatus("'" + record.name + "': the terrain under its loop could not be loaded; point heights were taken from the loaded terrain ("
			+ std::to_string(missing) + " of " + std::to_string(record.loop.points.size()) + " point(s) without terrain were put at height 0). Check them before saving.",
			StatusLevel::Error);
		WLOG(m_status);
	}

	bool ProceduralEditMode::IsLoopEditable(const int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(m_controls.size()))
		{
			return false;
		}

		const Control& control = m_controls[index];
		return control.kind != ControlKind::Oasis || control.oasis.heightsValid;
	}

	std::vector<PathSpline*> ProceduralEditMode::GetEditableSplines()
	{
		m_lockedSpline.points.clear();
		std::vector<PathSpline*> splines = GetSplines();
		for (size_t i = 0; i < splines.size(); ++i)
		{
			if (!IsLoopEditable(static_cast<int32>(i)))
			{
				splines[i] = &m_lockedSpline;
			}
		}
		return splines;
	}

	PathSpline* ProceduralEditMode::FindSelectableSpline(const uint64 splineId)
	{
		const int32 index = FindControl(splineId);
		return IsLoopEditable(index) ? &m_controls[index].Spline() : nullptr;
	}

	float ProceduralEditMode::GetSplinePointGroundHeight(const float x, const float z) const
	{
		return m_worldEditor.HasTerrain() ? m_worldEditor.GetTerrain()->GetSmoothHeightAt(x, z) : 0.0f;
	}

	void ProceduralEditMode::OnSplinePointEdited(const uint64 splineId)
	{
		const int32 index = FindControl(splineId);
		if (index >= 0)
		{
			MarkChanged(m_controls[index]);
		}
	}

	bool ProceduralEditMode::RemoveSplinePoint(const uint64 splineId, const size_t pointIndex)
	{
		const int32 index = FindControl(splineId);
		if (!IsLoopEditable(index))
		{
			return false;
		}

		Control& control = m_controls[index];
		auto& points = control.Spline().points;
		if (pointIndex >= points.size())
		{
			return false;
		}

		const uint32 minPoints = GetMinPoints(control);
		if (points.size() <= minPoints)
		{
			SetStatus(String(DescribeKind(control)) + " keeps at least " + std::to_string(minPoints) + " points.", StatusLevel::Error);
			return false;
		}

		points.erase(points.begin() + static_cast<std::ptrdiff_t>(pointIndex));
		std::vector<uint64>* pointIds = control.PointIds();
		if (pointIds && pointIndex < pointIds->size())
		{
			// The id leaves with its point and is never handed out again.
			pointIds->erase(pointIds->begin() + static_cast<std::ptrdiff_t>(pointIndex));
		}
		MarkChanged(control);
		m_splineEditor.CancelDrag();
		if (m_splineEditor.GetSelectedSpline() == index)
		{
			// Indices behind the deleted point shifted; nothing stays selected.
			m_splineEditor.Select(index, -1);
		}
		return true;
	}

	void ProceduralEditMode::OnSplinePointDeselected(const SelectedSplinePoint& selectable)
	{
		if (&selectable != m_pointSelectable)
		{
			return;
		}

		m_pointSelectable = nullptr;
		m_splineEditor.CancelDrag();
		m_splineEditor.Select(m_splineEditor.GetSelectedSpline(), -1);
	}

	bool ProceduralEditMode::HasUnsavedChanges() const
	{
		if (!m_deletedIds.empty())
		{
			return true;
		}

		return std::any_of(m_controls.begin(), m_controls.end(), [](const Control& control) { return control.dirty; });
	}

	void ProceduralEditMode::Load(const std::filesystem::path& pcgFolder)
	{
		CancelBackgroundRebuilds();
		ClearPointSelection();
		ClearTransformSelection();
		// Reloading replaces the records; an unfinished tags edit is dropped with them.
		m_tagsEditing = false;
		++m_placedRevision;
		m_controls.clear();
		m_deletedIds.clear();
		m_sectionAssets.clear();
		m_selected = -1;
		m_splineEditor.Select(-1, -1);
		m_splineEditor.CancelDrag();
		m_defaultParams = {};
		m_assets = {};
		m_recipeLoaded = false;

		if (const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(RecipeFileName))
		{
			const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
			if (pcg::ParseOasisRecipe(text, m_defaultParams, m_assets))
			{
				m_recipeLoaded = true;
			}
			else
			{
				WLOG("Oasis recipe '" << RecipeFileName << "' is not valid JSON; rebuilding oases is disabled until it is fixed");
				m_defaultParams = {};
				m_assets = {};
			}
		}
		else
		{
			WLOG("Oasis recipe '" << RecipeFileName << "' not found; rebuilding oases is disabled until it exists");
		}

		const String folder = ToAssetPath(pcgFolder) + "/";
		std::vector<String> files = AssetRegistry::ListFiles(folder, RecordExtension);
		std::sort(files.begin(), files.end());
		for (const String& fileName : files)
		{
			const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(fileName);
			if (!file)
			{
				WLOG("Cannot open procedural record " << fileName);
				continue;
			}

			io::StreamSource source{ *file };
			io::Reader reader{ source };
			Control control;
			switch (pcg::PeekRecordKind(reader))
			{
			case pcg::RecordKind::Oasis:
				control.kind = ControlKind::Oasis;
				if (!pcg::ReadOasisRecord(reader, control.oasis))
				{
					ELOG("Oasis record " << fileName << " is corrupt or from an unsupported version, skipping it");
					continue;
				}
				break;

			case pcg::RecordKind::SectionPath:
				control.kind = ControlKind::SectionPath;
				if (!pcg::ReadSectionPathRecord(reader, control.path))
				{
					ELOG("Section path record " << fileName << " is corrupt or from an unsupported version, skipping it");
					continue;
				}
				break;

			case pcg::RecordKind::GraphInstance:
				control.kind = ControlKind::GraphInstance;
				if (!pcg::ReadGraphInstanceRecord(reader, control.instance))
				{
					ELOG("Graph instance record " << fileName << " is corrupt or from an unsupported version, skipping it");
					continue;
				}
				break;

			default:
				ELOG("Procedural record " << fileName << " is of an unknown kind, skipping it");
				continue;
			}

			m_controls.push_back(std::move(control));
		}

		// Placed controls are kept whatever state their control type is in (resolution skips them then).
		std::vector<String> placedFiles = AssetRegistry::ListFiles(folder, PlacedRecordExtension);
		std::sort(placedFiles.begin(), placedFiles.end());
		for (const String& fileName : placedFiles)
		{
			const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(fileName);
			if (!file)
			{
				WLOG("Cannot open placed control " << fileName);
				continue;
			}

			const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
			Control control;
			control.kind = ControlKind::Placed;
			String error;
			if (!pcg::graph::ParseControlInstance(text, control.placed, error))
			{
				ELOG("Placed control " << fileName << " is invalid, skipping it: " << error);
				continue;
			}
			if (control.placed.id == 0 || FindControl(control.placed.id) >= 0)
			{
				ELOG("Placed control " << fileName << " has a missing or duplicate id, skipping it");
				continue;
			}

			m_controls.push_back(std::move(control));
		}

		std::stable_sort(m_controls.begin(), m_controls.end(), [](const Control& a, const Control& b) { return a.Id() < b.Id(); });
		OnControlsChanged();

		DLOG("Loaded " << m_controls.size() << " procedural record(s) from " << folder);

		// Live rebuild counts the loaded state as built.
		if (m_liveRebuild)
		{
			for (Control& control : m_controls)
			{
				if (control.kind == ControlKind::GraphInstance)
				{
					control.builtInputs = GetGraphInstanceInputs(control);
				}
			}
		}
	}

	bool ProceduralEditMode::Save(const std::filesystem::path& pcgFolder)
	{
		// Saving while the tags field is being edited keeps what was typed.
		ApplyPendingTags();
		bool success = true;

		bool deletionsFailed = false;
		for (const uint64 id : m_deletedIds)
		{
			// Ids are unique across kinds; whichever file the control had goes.
			for (const String& fileName : { MakeFileName(pcgFolder, id), MakePlacedFileName(pcgFolder, id) })
			{
				if (AssetRegistry::HasFile(fileName) && !AssetRegistry::RemoveFile(fileName))
				{
					ELOG("Failed to remove procedural record " << fileName);
					deletionsFailed = true;
					success = false;
				}
			}
		}

		if (!deletionsFailed)
		{
			m_deletedIds.clear();
		}

		for (Control& control : m_controls)
		{
			if (!control.dirty)
			{
				continue;
			}

			if (control.kind == ControlKind::Placed)
			{
				const String placedFileName = MakePlacedFileName(pcgFolder, control.Id());
				const std::unique_ptr<std::ostream> file = AssetRegistry::CreateNewFile(placedFileName);
				if (!file)
				{
					SetStatus("Could not save '" + control.Name() + "': cannot create " + placedFileName + ".", StatusLevel::Error);
					ELOG(m_status);
					success = false;
					continue;
				}

				const String text = pcg::graph::WriteControlInstance(control.placed);
				file->write(text.data(), static_cast<std::streamsize>(text.size()));
				file->flush();
				if (!*file)
				{
					SetStatus("Could not save '" + control.Name() + "': writing " + placedFileName + " failed.", StatusLevel::Error);
					ELOG(m_status);
					success = false;
					continue;
				}
				control.dirty = false;
				continue;
			}

			const String fileName = MakeFileName(pcgFolder, control.Id());

			// A record from before heights existed gets them first (at worst from the loaded terrain, with a
			// warning), so a file never claims zero heights as real ones.
			EnsureHeights(control, false);

			const std::unique_ptr<std::ostream> file = AssetRegistry::CreateNewFile(fileName);
			if (!file)
			{
				SetStatus("Could not save '" + control.Name() + "': cannot create " + fileName + ".", StatusLevel::Error);
				ELOG(m_status);
				success = false;
				continue;
			}

			io::StreamSink sink{ *file };
			io::Writer writer{ sink };
			bool written = false;
			switch (control.kind)
			{
			case ControlKind::Oasis:
				written = pcg::WriteOasisRecord(writer, control.oasis);
				break;
			case ControlKind::SectionPath:
				written = pcg::WriteSectionPathRecord(writer, control.path);
				break;
			case ControlKind::GraphInstance:
				written = pcg::WriteGraphInstanceRecord(writer, control.instance);
				break;
			case ControlKind::Placed:
				// Written above.
				break;
			}
			if (!written)
			{
				SetStatus("Could not save '" + control.Name() + "': writing " + fileName + " failed.", StatusLevel::Error);
				ELOG(m_status);
				success = false;
				continue;
			}
			sink.Flush();
			control.dirty = false;
		}

		return success;
	}

	void ProceduralEditMode::CreateOasisAtViewportCentre()
	{
		if (!m_worldEditor.HasTerrain())
		{
			SetStatus("This world has no terrain.", StatusLevel::Error);
			return;
		}

		// Centre of the viewport, down onto the ground.
		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			SetStatus("No ground under the viewport centre. Look at the terrain and try again.", StatusLevel::Error);
			return;
		}

		String error;
		uint64 id = 0;
		CreateOasisAt(hit.second.position.x, hit.second.position.z, DefaultRadius, 1, "", id, error);
	}

	void ProceduralEditMode::CreateSectionPathAtViewportCentre()
	{
		if (!m_worldEditor.HasTerrain())
		{
			SetStatus("This world has no terrain.", StatusLevel::Error);
			return;
		}

		if (m_newPathAsset.empty())
		{
			SetStatus("Pick the section asset of the new path first.", StatusLevel::Error);
			return;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			SetStatus("No ground under the viewport centre. Look at the terrain and try again.", StatusLevel::Error);
			return;
		}

		// Along the camera's forward direction on the ground (straight down: along +Z).
		const Vector3 direction = m_camera.GetDerivedDirection();
		float forwardX = direction.x;
		float forwardZ = direction.z;
		const float length = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
		if (length > 1e-4f)
		{
			forwardX /= length;
			forwardZ /= length;
		}
		else
		{
			forwardX = 0.0f;
			forwardZ = 1.0f;
		}

		const float x = hit.second.position.x;
		const float z = hit.second.position.z;
		const std::vector<std::pair<float, float>> points = { { x, z }, { x + forwardX * NewPathLength, z + forwardZ * NewPathLength } };

		String error;
		uint64 id = 0;
		CreateSectionPath(points, m_newPathAsset, NewSectionPathOptions(), "", id, error);
	}

	void ProceduralEditMode::CreateSectionLoopAtViewportCentre()
	{
		if (!m_worldEditor.HasTerrain())
		{
			SetStatus("This world has no terrain.", StatusLevel::Error);
			return;
		}

		if (m_newPathAsset.empty())
		{
			SetStatus("Pick the section asset of the new loop first.", StatusLevel::Error);
			return;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			SetStatus("No ground under the viewport centre. Look at the terrain and try again.", StatusLevel::Error);
			return;
		}

		// Same winding as a new oasis loop; floorInside puts the floor side inside either way.
		std::vector<std::pair<float, float>> points;
		for (uint32 i = 0; i < NewLoopPointCount; ++i)
		{
			const float angle = 2.0f * PiValue * static_cast<float>(i) / static_cast<float>(NewLoopPointCount);
			points.emplace_back(hit.second.position.x + NewLoopRadius * std::cos(angle), hit.second.position.z + NewLoopRadius * std::sin(angle));
		}

		// A loop is a wall: mass behind it and facade variety on.
		NewSectionPathOptions options;
		options.closed = true;
		options.mass = true;
		options.mirror = true;
		String error;
		uint64 id = 0;
		CreateSectionPath(points, m_newPathAsset, options, "", id, error);
	}

	bool ProceduralEditMode::CreateOasisAt(const float x, const float z, const float radius, const uint64 seed, const String& name, uint64& id, String& error)
	{
		if (!m_worldEditor.HasTerrain())
		{
			error = "This world has no terrain.";
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		if (!(radius > 0.0f))
		{
			error = "The oasis radius must be positive.";
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		Control control;
		control.kind = ControlKind::Oasis;
		MarkChanged(control);
		pcg::OasisRecord& record = control.oasis;
		record.id = m_worldEditor.GenerateEntityUniqueId();
		record.name = name.empty() ? "Oasis " + std::to_string(GetOasisCount() + 1) : name;
		record.seed = seed;
		record.params = m_defaultParams;
		record.loop.closed = true;
		record.loop.interpolation = SplineInterpolation::CatmullRom;
		for (uint32 i = 0; i < DefaultPointCount; ++i)
		{
			const float angle = 2.0f * PiValue * static_cast<float>(i) / static_cast<float>(DefaultPointCount);
			SplinePoint point;
			point.x = x + radius * std::cos(angle);
			point.z = z + radius * std::sin(angle);
			record.loop.points.push_back(point);
		}
		// Points start on the ground (the loop is a 3D curve; heights are free afterwards).
		record.heightsValid = false;
		EnsureHeights(control, false);

		id = record.id;
		m_controls.push_back(std::move(control));
		OnControlsChanged();
		SelectControl(static_cast<int32>(m_controls.size()) - 1);
		SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + "). Edit the loop, then Rebuild.", StatusLevel::Ok);
		ILOG(m_status);
		return true;
	}

	bool ProceduralEditMode::CreateSectionPath(const std::vector<std::pair<float, float>>& points, const String& sectionAsset,
		const NewSectionPathOptions& options, const String& name, uint64& id, String& error)
	{
		id = 0;
		const bool closed = options.closed;
		if (!m_worldEditor.HasTerrain())
		{
			error = "This world has no terrain.";
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		const uint32 minPoints = closed ? MinLoopPoints : MinPathPoints;
		if (points.size() < minPoints)
		{
			error = String(closed ? "A section loop" : "A section path") + " needs at least " + std::to_string(minPoints) + " points.";
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		// Refused before anything is added: a path without its section cannot be built.
		pcg::SectionAsset asset;
		if (!LoadSectionAsset(sectionAsset, asset, error))
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		Control control;
		control.kind = ControlKind::SectionPath;
		MarkChanged(control);
		pcg::SectionPathRecord& record = control.path;
		record.id = m_worldEditor.GenerateEntityUniqueId();
		record.name = name.empty() ? String(closed ? "Section Loop " : "Section Path ") + std::to_string(std::count_if(m_controls.begin(), m_controls.end(),
			[](const Control& c) { return c.kind == ControlKind::SectionPath; }) + 1) : name;
		record.sectionAsset = sectionAsset;
		// A loop follows the large shape of the hand-made ground and levels its bumps (SmoothedGround unless set).
		if (!ApplyNewSectionPathOptions(options, record.params, error))
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}
		record.spline.closed = closed;
		record.spline.interpolation = SplineInterpolation::CatmullRom;

		// Points on the ground: load the terrain under them first.
		pcg::WorldRect bounds{ points.front().first, points.front().second, points.front().first, points.front().second };
		for (const auto& [x, z] : points)
		{
			bounds.minX = std::min(bounds.minX, x);
			bounds.minZ = std::min(bounds.minZ, z);
			bounds.maxX = std::max(bounds.maxX, x);
			bounds.maxZ = std::max(bounds.maxZ, z);
		}
		String loadError;
		const bool loaded = m_worldEditor.EnsurePagesLoaded(bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ, loadError);

		size_t missing = 0;
		for (const auto& [x, z] : points)
		{
			SplinePoint point;
			point.x = x;
			point.z = z;
			if (!m_worldEditor.GetTerrain()->TryGetSmoothHeightAt(x, z, point.y))
			{
				point.y = 0.0f;
				++missing;
			}
			record.spline.points.push_back(point);
			record.pointIds.push_back(record.nextPointId++);
		}

		pcg::SectionPathShape probe;
		if (!PrepareSectionPathProbe(record, asset, probe, error))
		{
			error = "Invalid section path: " + error;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		m_sectionAssets[sectionAsset] = CachedSectionAsset{ true, asset, String() };

		id = record.id;
		m_controls.push_back(std::move(control));
		OnControlsChanged();
		SelectControl(static_cast<int32>(m_controls.size()) - 1);
		if (missing > 0 || !loaded)
		{
			SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + "), but the terrain under it could not be loaded: "
				+ std::to_string(missing) + " point(s) were put at height 0. Check them before Rebuild.", StatusLevel::Warning);
			WLOG(m_status);
		}
		else
		{
			SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + "). Edit the path, then Rebuild.", StatusLevel::Ok);
			ILOG(m_status);
		}
		return true;
	}

	void ProceduralEditMode::CreatePlacedControlAtViewportCentre(const String& typePath)
	{
		if (!m_worldEditor.HasTerrain())
		{
			SetStatus("This world has no terrain.", StatusLevel::Error);
			return;
		}

		String error;
		const pcg::graph::ControlType* type = m_controlLibrary.Find(typePath, &error);
		if (!type)
		{
			SetStatus(error, StatusLevel::Error);
			return;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			SetStatus("No ground under the viewport centre. Look at the terrain and try again.", StatusLevel::Error);
			return;
		}

		const float x = hit.second.position.x;
		const float z = hit.second.position.z;
		std::vector<std::pair<float, float>> points;
		switch (type->shape)
		{
		case pcg::graph::ControlShape::ClosedLoop:
		{
			const float half = NewControlSize * 0.5f;
			points = { { x - half, z - half }, { x + half, z - half }, { x + half, z + half }, { x - half, z + half } };
			break;
		}

		case pcg::graph::ControlShape::OpenSpline:
		{
			// Along the camera's forward direction on the ground (straight down: along +Z).
			const Vector3 direction = m_camera.GetDerivedDirection();
			float forwardX = direction.x;
			float forwardZ = direction.z;
			const float length = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
			if (length > 1e-4f)
			{
				forwardX /= length;
				forwardZ /= length;
			}
			else
			{
				forwardX = 0.0f;
				forwardZ = 1.0f;
			}
			points = { { x, z }, { x + forwardX * NewControlSize, z + forwardZ * NewControlSize } };
			break;
		}

		case pcg::graph::ControlShape::Volume:
		case pcg::graph::ControlShape::Rectangle:
		case pcg::graph::ControlShape::Point:
			points = { { x, z } };
			break;
		}

		uint64 id = 0;
		CreatePlacedControl(typePath, points, "", id, error);
	}

	bool ProceduralEditMode::CreatePlacedControl(const String& typePath, const std::vector<std::pair<float, float>>& points, const String& name,
		uint64& id, String& error, const NewPlacedControlTransform& transform)
	{
		id = 0;
		const pcg::graph::ControlType* type = m_controlLibrary.Find(typePath, &error);
		if (!type)
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		const bool splineShape = type->shape == pcg::graph::ControlShape::ClosedLoop || type->shape == pcg::graph::ControlShape::OpenSpline;
		const bool closed = type->shape == pcg::graph::ControlShape::ClosedLoop;
		if (splineShape)
		{
			const uint32 minPoints = closed ? MinLoopPoints : MinPathPoints;
			if (points.size() < minPoints)
			{
				error = "A " + String(closed ? "loop" : "spline") + " control needs at least " + std::to_string(minPoints) + " points.";
				SetStatus(error, StatusLevel::Error);
				return false;
			}
		}
		else if (points.size() != 1)
		{
			const char* shapeText = type->shape == pcg::graph::ControlShape::Volume ? "volume"
				: (type->shape == pcg::graph::ControlShape::Rectangle ? "rectangle" : "point");
			error = "A " + String(shapeText) + " control needs exactly one point (its centre).";
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		// An explicit transform fits only shapes that have one; half extents not a point.
		if (transform.halfExtents || transform.yawDegrees)
		{
			const bool sized = type->shape == pcg::graph::ControlShape::Volume || type->shape == pcg::graph::ControlShape::Rectangle;
			// One minimum serves volumes (the gizmo's) and rectangles.
			static_assert(SelectedControlTransform::MinHalfExtent == pcg::graph::MinRectangleHalfExtent,
				"volumes and rectangles share the minimum half extent");
			String transformError;
			if (splineShape)
			{
				transformError = "Half extents and yaw apply only to volume, rectangle and point controls.";
			}
			else if (transform.halfExtents && !sized)
			{
				transformError = "Half extents apply only to volume and rectangle controls.";
			}
			else if (transform.halfExtents && (!std::isfinite(transform.halfExtents->first) || !std::isfinite(transform.halfExtents->second)
				|| transform.halfExtents->first < SelectedControlTransform::MinHalfExtent || transform.halfExtents->second < SelectedControlTransform::MinHalfExtent))
			{
				char text[96];
				std::snprintf(text, sizeof(text), "Half extents must be finite and at least %.2f m.", SelectedControlTransform::MinHalfExtent);
				transformError = text;
			}
			else if (transform.yawDegrees && !std::isfinite(*transform.yawDegrees))
			{
				transformError = "The yaw must be finite.";
			}

			if (!transformError.empty())
			{
				error = transformError;
				SetStatus(error, StatusLevel::Error);
				return false;
			}
		}

		for (const auto& [x, z] : points)
		{
			if (!std::isfinite(x) || !std::isfinite(z))
			{
				error = "Control points must be finite.";
				SetStatus(error, StatusLevel::Error);
				return false;
			}
		}

		Control control;
		control.kind = ControlKind::Placed;
		pcg::graph::ControlInstance& instance = control.placed;
		instance.id = m_worldEditor.GenerateEntityUniqueId();
		instance.typePath = typePath;
		const size_t sameType = static_cast<size_t>(std::count_if(m_controls.begin(), m_controls.end(),
			[&typePath](const Control& c) { return c.kind == ControlKind::Placed && c.placed.typePath == typePath; }));
		instance.name = name.empty() ? (type->name.empty() ? FileStem(typePath) : type->name) + " " + std::to_string(sameType + 1) : name;

		// Points on the ground: load the terrain under them first.
		pcg::WorldRect bounds{ points.front().first, points.front().second, points.front().first, points.front().second };
		for (const auto& [x, z] : points)
		{
			bounds.minX = std::min(bounds.minX, x);
			bounds.minZ = std::min(bounds.minZ, z);
			bounds.maxX = std::max(bounds.maxX, x);
			bounds.maxZ = std::max(bounds.maxZ, z);
		}
		String loadError;
		const bool loaded = !m_worldEditor.HasTerrain() || m_worldEditor.EnsurePagesLoaded(bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ, loadError);

		size_t missing = 0;
		const auto groundAt = [this, &missing](const float x, const float z)
		{
			float height = 0.0f;
			if (!m_worldEditor.HasTerrain() || !m_worldEditor.GetTerrain()->TryGetSmoothHeightAt(x, z, height))
			{
				++missing;
				return 0.0f;
			}
			return height;
		};

		if (splineShape)
		{
			instance.spline.closed = closed;
			instance.spline.interpolation = SplineInterpolation::CatmullRom;
			for (const auto& [x, z] : points)
			{
				SplinePoint point;
				point.x = x;
				point.z = z;
				point.y = groundAt(x, z);
				instance.spline.points.push_back(point);
				instance.pointIds.push_back(instance.nextPointId++);
			}
		}
		else
		{
			instance.position = Vector3(points.front().first, groundAt(points.front().first, points.front().second), points.front().second);
			instance.rotation = Quaternion::Identity;
			if (type->shape == pcg::graph::ControlShape::Volume)
			{
				instance.halfExtents = Vector3(NewVolumeHalfExtent, NewVolumeHalfExtent, NewVolumeHalfExtent);
			}
			else if (type->shape == pcg::graph::ControlShape::Rectangle)
			{
				instance.halfExtents = Vector3(NewRectangleHalfExtent, NewRectangleHalfHeight, NewRectangleHalfExtent);
			}

			if (transform.halfExtents)
			{
				instance.halfExtents.x = transform.halfExtents->first;
				instance.halfExtents.z = transform.halfExtents->second;
			}
			if (transform.yawDegrees)
			{
				instance.rotation = SelectedControlTransform::FromYaw(*transform.yawDegrees * PiValue / 180.0f);
			}
		}

		MarkChanged(control);
		id = instance.id;
		m_controls.push_back(std::move(control));
		OnControlsChanged();
		SelectControl(static_cast<int32>(m_controls.size()) - 1);
		if (missing > 0 || !loaded)
		{
			SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + "), but the terrain under it could not be loaded: "
				+ std::to_string(missing) + " point(s) were put at height 0.", StatusLevel::Warning);
			WLOG(m_status);
		}
		else
		{
			SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + ").", StatusLevel::Ok);
			ILOG(m_status);
		}
		return true;
	}

	bool ProceduralEditMode::CheckPlacedControlSettings(const String& typePath, const std::vector<std::pair<String, String>>& settings, String& error)
	{
		const pcg::graph::ControlType* type = m_controlLibrary.Find(typePath, &error);
		if (!type)
		{
			return false;
		}

		for (const auto& [propertyName, text] : settings)
		{
			const pcg::graph::PropertyDef* property = FindControlProperty(*type, propertyName);
			if (!property)
			{
				error = "control type '" + typePath + "' has no property '" + propertyName + "'";
				return false;
			}

			pcg::graph::AttributeValue value;
			if (!ParseControlPropertyValue(*property, text, value, error))
			{
				return false;
			}
		}

		return true;
	}

	bool ProceduralEditMode::SetPlacedControlProperties(const uint64 id, const std::vector<std::pair<String, String>>& settings, String& error)
	{
		const int32 index = FindControl(id);
		if (index < 0 || m_controls[index].kind != ControlKind::Placed)
		{
			error = "No placed control with id " + FormatId(id);
			return false;
		}

		Control& control = m_controls[index];
		const pcg::graph::ControlType* type = m_controlLibrary.Find(control.placed.typePath, &error);
		if (!type)
		{
			return false;
		}

		// Parsed first, so a bad setting changes nothing.
		pcg::graph::AttributeSet overrides = control.placed.overrides;
		for (const auto& [propertyName, text] : settings)
		{
			const pcg::graph::PropertyDef* property = FindControlProperty(*type, propertyName);
			if (!property)
			{
				error = "control type '" + control.placed.typePath + "' has no property '" + propertyName + "'";
				return false;
			}

			pcg::graph::AttributeValue value;
			if (!ParseControlPropertyValue(*property, text, value, error))
			{
				return false;
			}
			overrides[propertyName] = value;
		}

		if (!settings.empty())
		{
			control.placed.overrides = std::move(overrides);
			MarkChanged(control);
		}
		return true;
	}

	void ProceduralEditMode::ResolvePlacedControls(std::vector<pcg::graph::ResolvedControl>& out, std::vector<String>& warnings,
		std::vector<UnresolvedPlacedControl>* unresolved)
	{
		out.clear();
		if (unresolved)
		{
			unresolved->clear();
		}

		for (const Control& control : m_controls)
		{
			if (control.kind != ControlKind::Placed)
			{
				continue;
			}

			String error;
			const pcg::graph::ControlType* type = m_controlLibrary.Find(control.placed.typePath, &error);
			if (!type)
			{
				warnings.push_back("placed control '" + control.Name() + "' (" + FormatId(control.Id()) + ") skipped: " + error);
				if (unresolved)
				{
					unresolved->push_back({ control.Id(), control.Name(), error, GetPlacedControlBounds(control.placed, nullptr) });
				}
				continue;
			}

			pcg::graph::ResolvedControl resolved;
			const size_t warningCount = warnings.size();
			if (pcg::graph::ResolveControl(*type, control.placed, resolved, warnings))
			{
				out.push_back(std::move(resolved));
			}
			else if (unresolved)
			{
				// ResolveControl's last warning is its refusal reason.
				const String reason = warnings.size() > warningCount ? warnings.back() : "its geometry does not fit its control type";
				unresolved->push_back({ control.Id(), control.Name(), reason, GetPlacedControlBounds(control.placed, type) });
			}
		}
	}

	void ProceduralEditMode::CreateGraphInstanceAtViewportCentre()
	{
		if (!m_worldEditor.HasTerrain())
		{
			SetStatus("This world has no terrain.", StatusLevel::Error);
			return;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			SetStatus("No ground under the viewport centre. Look at the terrain and try again.", StatusLevel::Error);
			return;
		}

		const float x = hit.second.position.x;
		const float z = hit.second.position.z;
		const float half = NewGraphInstanceSize * 0.5f;
		const std::vector<std::pair<float, float>> points = { { x - half, z - half }, { x + half, z - half }, { x + half, z + half }, { x - half, z + half } };

		String error;
		uint64 id = 0;
		// Seed 0: derived from the new instance's id, so instances created this way do not all share one seed.
		CreateGraphInstance("", points, 0, "", {}, id, error);
	}

	bool ProceduralEditMode::CreateGraphInstance(const String& graphPath, const std::vector<std::pair<float, float>>& points, const uint64 seed, const String& name,
		const std::vector<std::pair<String, String>>& parameterSettings, uint64& id, String& error)
	{
		id = 0;
		const auto fail = [this, &error](const String& reason)
		{
			error = reason;
			SetStatus(error, StatusLevel::Error);
			return false;
		};

		if (points.size() < MinLoopPoints)
		{
			return fail("A graph instance area needs at least " + std::to_string(MinLoopPoints) + " points.");
		}

		for (const auto& [x, z] : points)
		{
			if (!std::isfinite(x) || !std::isfinite(z))
			{
				return fail("Area points must be finite.");
			}
		}

		pcg::GraphInstanceRecord record;
		record.graphPath = graphPath;
		record.seed = seed;

		// borderFade is a field of the instance, not a graph parameter.
		std::vector<std::pair<String, String>> graphSettings;
		for (const auto& [settingName, text] : parameterSettings)
		{
			if (settingName != GraphInstanceBorderFadeSetting)
			{
				graphSettings.emplace_back(settingName, text);
				continue;
			}

			float borderFade = 0.0f;
			if (!ParseFloatText(text, borderFade) || borderFade < 0.0f)
			{
				return fail(String(GraphInstanceBorderFadeSetting) + " needs a finite number >= 0, got '" + text + "'");
			}
			record.borderFade = borderFade;
		}

		// Overrides are typed by the graph's parameters, so the graph must be readable when any are given.
		if (!graphPath.empty())
		{
			pcg::graph::Graph graph;
			String graphError;
			if (!LoadGraphAsset(graphPath, graph, graphError))
			{
				return fail(graphError);
			}

			for (const auto& [parameterName, text] : graphSettings)
			{
				const auto parameter = std::find_if(graph.parameters.begin(), graph.parameters.end(),
					[&parameterName](const pcg::graph::ParameterDef& def) { return def.name == parameterName; });
				if (parameter == graph.parameters.end())
				{
					return fail("graph '" + graphPath + "' has no parameter '" + parameterName + "'");
				}

				pcg::graph::AttributeValue value;
				String parseError;
				if (!ParseGraphParameterValue(*parameter, text, value, parseError))
				{
					return fail(parseError);
				}
				record.parameterOverrides[parameterName] = value;
			}
		}
		else if (!graphSettings.empty())
		{
			return fail("Parameter overrides need a graph.");
		}

		// Sharp corners (manual, zero tangents): the area is the polygon through the points, so neighbouring
		// instances can share an edge exactly. Points on the ground: load the terrain under them first.
		record.area.closed = true;
		record.area.interpolation = SplineInterpolation::CatmullRom;
		pcg::WorldRect bounds{ points.front().first, points.front().second, points.front().first, points.front().second };
		for (const auto& [x, z] : points)
		{
			bounds.minX = std::min(bounds.minX, x);
			bounds.minZ = std::min(bounds.minZ, z);
			bounds.maxX = std::max(bounds.maxX, x);
			bounds.maxZ = std::max(bounds.maxZ, z);
		}
		String loadError;
		if (m_worldEditor.HasTerrain() && !m_worldEditor.EnsurePagesLoaded(bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ, loadError))
		{
			// Only the drawn heights of the area points suffer (the evaluation ignores them).
			WLOG("New graph instance: the terrain under its area could not be loaded (" << loadError
				<< "); points without terrain are put at height 0");
		}

		for (const auto& [x, z] : points)
		{
			SplinePoint point;
			point.x = x;
			point.z = z;
			MakeSharpCorner(point);
			if (!m_worldEditor.HasTerrain() || !m_worldEditor.GetTerrain()->TryGetSmoothHeightAt(x, z, point.y))
			{
				point.y = 0.0f;
			}
			record.area.points.push_back(point);
			record.pointIds.push_back(record.nextPointId++);
		}

		for (const Control& other : m_controls)
		{
			if (other.kind == ControlKind::GraphInstance && pcg::AreasOverlap(record.area, other.instance.area))
			{
				return fail("The area overlaps the area of graph instance '" + other.Name() + "' (" + FormatId(other.Id()) + ").");
			}
		}

		record.id = m_worldEditor.GenerateEntityUniqueId();
		if (record.seed == 0)
		{
			// Deterministic per instance and never 0.
			record.seed = pcg::Hash(record.id, 0, 0, 0x5EEDu);
			record.seed = record.seed == 0 ? 1 : record.seed;
		}
		const size_t instanceCount = static_cast<size_t>(std::count_if(m_controls.begin(), m_controls.end(),
			[](const Control& c) { return c.kind == ControlKind::GraphInstance; }));
		record.name = name.empty() ? "Graph Instance " + std::to_string(instanceCount + 1) : name;

		Control control;
		control.kind = ControlKind::GraphInstance;
		control.instance = std::move(record);
		MarkChanged(control);
		id = control.instance.id;
		m_controls.push_back(std::move(control));
		OnControlsChanged();
		SelectControl(static_cast<int32>(m_controls.size()) - 1);
		SetStatus("Created '" + m_controls.back().Name() + "' (" + FormatId(id) + ")."
			+ (graphPath.empty() ? " Pick its graph, edit the area, then Rebuild." : " Edit the area, then Rebuild."), StatusLevel::Ok);
		ILOG(m_status);
		return true;
	}

	bool ProceduralEditMode::EvaluateInstance(const uint64 id, GraphInstanceEvaluation& out, String& error)
	{
		const int32 index = FindControl(id);
		if (index < 0 || m_controls[index].kind != ControlKind::GraphInstance)
		{
			error = "No graph instance with id " + FormatId(id);
			return false;
		}

		// On the base terrain, like the rebuild (and the graph editor's debug view).
		return EvaluateGraphInstance(m_worldEditor, m_controls[index].instance, *this, out, error, true);
	}

	const pcg::GraphInstanceRecord* ProceduralEditMode::FindGraphInstance(const uint64 id) const
	{
		const int32 index = FindControl(id);
		return index >= 0 && m_controls[index].kind == ControlKind::GraphInstance ? &m_controls[index].instance : nullptr;
	}

	std::vector<const pcg::GraphInstanceRecord*> ProceduralEditMode::GetGraphInstances() const
	{
		std::vector<const pcg::GraphInstanceRecord*> instances;
		for (const Control& control : m_controls)
		{
			if (control.kind == ControlKind::GraphInstance)
			{
				instances.push_back(&control.instance);
			}
		}
		return instances;
	}

	std::vector<terrain_io::water_flow::FlowPath> ProceduralEditMode::CollectGeneratedFlowPaths() const
	{
		return CollectFlowPathsReplacing(0, nullptr);
	}

	std::vector<terrain_io::water_flow::FlowPath> ProceduralEditMode::CollectGeneratedFlowPaths(const uint64 replaceId,
		const std::vector<terrain_io::water_flow::FlowPath>& replacement) const
	{
		return CollectFlowPathsReplacing(replaceId, &replacement);
	}

	std::vector<terrain_io::water_flow::FlowPath> ProceduralEditMode::CollectFlowPathsReplacing(const uint64 replaceId,
		const std::vector<terrain_io::water_flow::FlowPath>* replacement) const
	{
		std::vector<const pcg::GraphInstanceRecord*> instances = GetGraphInstances();
		std::sort(instances.begin(), instances.end(), [](const pcg::GraphInstanceRecord* a, const pcg::GraphInstanceRecord* b)
		{
			return a->id < b->id;
		});

		std::vector<terrain_io::water_flow::FlowPath> paths;
		for (const pcg::GraphInstanceRecord* instance : instances)
		{
			const std::vector<terrain_io::water_flow::FlowPath>& own =
				replacement != nullptr && instance->id == replaceId ? *replacement : instance->flowPaths;
			paths.insert(paths.end(), own.begin(), own.end());
		}
		return paths;
	}

	std::vector<const pcg::GraphInstanceRecord*> ProceduralEditMode::GetOtherGraphInstances(const uint64 id) const
	{
		std::vector<const pcg::GraphInstanceRecord*> others;
		for (const Control& control : m_controls)
		{
			if (control.kind == ControlKind::GraphInstance && control.Id() != id)
			{
				others.push_back(&control.instance);
			}
		}
		return others;
	}

	const ProceduralEditMode::CachedGraph& ProceduralEditMode::GetCachedGraph(const String& graphPath)
	{
		const auto found = m_graphs.find(graphPath);
		if (found != m_graphs.end())
		{
			return found->second;
		}

		CachedGraph cached;
		cached.valid = LoadGraphAsset(graphPath, cached.graph, cached.error);
		return m_graphs.emplace(graphPath, std::move(cached)).first->second;
	}

	void ProceduralEditMode::InvalidateGraph(const String& graphPath)
	{
		const auto normalize = [](String path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		};

		const String normalized = normalize(graphPath);
		++m_graphRevisions[normalized];
		std::erase_if(m_graphs, [&normalize, &normalized](const auto& entry)
		{
			return normalize(entry.first) == normalized;
		});
	}

	bool ProceduralEditMode::RebuildGraphInstanceControl(Control& control, String& error)
	{
		// The rebuild reads the saved graph file; the panel follows what it read.
		m_graphs.erase(control.instance.graphPath);
		control.builtInputs = GetGraphInstanceInputs(control);
		const GeneratorBuildReport report = RebuildGraphInstance(m_worldEditor, control.instance, GetOtherGraphInstances(control.Id()), *this,
			MakeTerrainOverlapCheck(control.Id()));
		return ReportGraphInstanceBuild(control, report, error);
	}

	std::function<bool(const pcg::WorldRect& areaRect, String& error)> ProceduralEditMode::MakeTerrainOverlapCheck(const uint64 controlId)
	{
		// An instance whose graph writes terrain may write anywhere inside its area: no oasis or section path may
		// have built there (the rebuild asks once it knows the graph has terrain ops).
		return [this, controlId](const pcg::WorldRect& areaRect, String& overlapError)
		{
			const int32 index = FindControl(controlId);
			if (index < 0 || CheckOverlap(m_controls[index], { areaRect }, overlapError))
			{
				return true;
			}

			// The rebuild's refusal message says that already.
			const String refused = "Rebuild refused: ";
			if (overlapError.compare(0, refused.size(), refused) == 0)
			{
				overlapError.erase(0, refused.size());
			}
			return false;
		};
	}

	bool ProceduralEditMode::ReportGraphInstanceBuild(Control& control, const GeneratorBuildReport& report, String& error)
	{
		if (!report.ok)
		{
			error = report.error;
			control.lastReport = error;
			control.lastReportOk = false;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		MarkChanged(control);

		// Node warnings can be many: the status line shows the first few, the log all of them.
		constexpr size_t maxShownWarnings = 8;
		String text = DescribeReport(report, "Rebuilt '" + control.Name() + "'");
		for (size_t i = 0; i < report.warnings.size() && i < maxShownWarnings; ++i)
		{
			text += "\nWarning: " + report.warnings[i];
		}
		if (report.warnings.size() > maxShownWarnings)
		{
			text += "\n... and " + std::to_string(report.warnings.size() - maxShownWarnings) + " more warning(s) (see the log).";
		}
		control.lastReport = text;
		control.lastReportOk = true;
		SetStatus(text, report.warnings.empty() ? StatusLevel::Ok : StatusLevel::Warning);
		ILOG(DescribeReport(report, "Rebuilt '" + control.Name() + "'"));
		for (const String& warning : report.warnings)
		{
			WLOG("'" << control.Name() << "': " << warning);
		}
		return true;
	}

	uint64 ProceduralEditMode::GetGraphInstanceInputs(const Control& control)
	{
		// Everything a rebuild reads that the editor changes: the record (without what the last build wrote), the graph
		// as the details panel last read it, the control types and every placed control that may reach the area. A
		// placed control moved out of the area changes the set as well.
		const pcg::GraphInstanceRecord& record = control.instance;
		pcg::graph::Hasher hasher;
		hasher.Str(record.graphPath);
		hasher.U64(record.seed);
		hasher.U64(record.pointLimit);
		hasher.F32(record.borderFade);
		hasher.Spline(record.area);
		hasher.Attributes(record.parameterOverrides);
		String graphKey = record.graphPath;
		std::replace(graphKey.begin(), graphKey.end(), '\\', '/');
		const auto graphRevision = m_graphRevisions.find(graphKey);
		hasher.U64(graphRevision != m_graphRevisions.end() ? graphRevision->second : 0);
		hasher.U64(m_controlLibrary.Revision());

		const pcg::graph::Bounds2D area = pcg::graph::SplineBounds(record.area);
		for (const Control& other : m_controls)
		{
			if (other.kind != ControlKind::Placed)
			{
				continue;
			}
			const pcg::graph::Bounds2D bounds = GetPlacedControlBounds(other.placed, m_controlLibrary.Find(other.placed.typePath));
			if (bounds.valid && pcg::graph::SaneBounds(bounds) && !bounds.Intersects(area))
			{
				continue;
			}
			hasher.Str(pcg::graph::WriteControlInstance(other.placed));
		}
		return hasher.Result();
	}

	bool ProceduralEditMode::IsEditIdle() const
	{
		// A rebuild starts when an edit ends: no button held, no point, handle or gizmo dragged, no field being typed in.
		if (m_splineEditor.IsDragging() || m_rectangleDrag.active || m_worldEditor.IsTransforming())
		{
			return false;
		}
		return !ImGui::GetCurrentContext() || (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive());
	}

	bool ProceduralEditMode::RebuildInBackgroundAndWait(const uint64 controlId, String& error)
	{
		RequestBackgroundRebuild(controlId);
		while (HasBackgroundRebuilds())
		{
			UpdateBackgroundRebuild();
			// Unattended: there is no edit to wait for.
			StartQueuedRebuild();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		const int32 index = FindControl(controlId);
		if (index < 0 || !m_controls[index].lastReportOk)
		{
			error = index < 0 ? "No procedural control with id " + FormatId(controlId) : m_controls[index].lastReport;
			return false;
		}
		return true;
	}

	void ProceduralEditMode::RequestBackgroundRebuild(const uint64 controlId)
	{
		if (std::find(m_rebuildQueue.begin(), m_rebuildQueue.end(), controlId) == m_rebuildQueue.end())
		{
			m_rebuildQueue.push_back(controlId);
		}

		// A newer request supersedes the running rebuild of the same instance.
		if (m_rebuildJob && m_rebuildJob->GetInstanceId() == controlId)
		{
			m_rebuildJob->Cancel();
		}
	}

	void ProceduralEditMode::LaunchRebuildThread()
	{
		m_rebuildRunning = true;
		GraphRebuildJob* job = m_rebuildJob.get();
		m_rebuildThread = std::thread([this, job]()
		{
			job->Run();
			m_rebuildRunning = false;
		});
	}

	void ProceduralEditMode::StartQueuedRebuild()
	{
		while (!m_rebuildJob && !m_rebuildQueue.empty())
		{
			const uint64 id = m_rebuildQueue.front();
			m_rebuildQueue.erase(m_rebuildQueue.begin());
			const int32 index = FindControl(id);
			if (index < 0 || m_controls[index].kind != ControlKind::GraphInstance)
			{
				continue;
			}

			Control& control = m_controls[index];
			// The rebuild reads the saved graph file; the panel follows what it read.
			m_graphs.erase(control.instance.graphPath);
			const uint64 inputs = GetGraphInstanceInputs(control);
			String error;
			m_rebuildJob = GraphRebuildJob::Start(m_worldEditor, control.instance, GetOtherGraphInstances(id), *this, error);
			if (!m_rebuildJob)
			{
				GeneratorBuildReport report;
				report.error = "Rebuild of '" + control.Name() + "' refused, the world was not changed: " + error;
				ELOG("PCG: " << report.error);
				ReportGraphInstanceBuild(control, report, error);
				// Not again until the inputs change.
				control.builtInputs = inputs;
				continue;
			}
			m_rebuildJobInputs = inputs;
			SetStatus("Rebuilding '" + control.Name() + "' in the background...", StatusLevel::Ok);
			LaunchRebuildThread();
		}
	}

	void ProceduralEditMode::FinishBackgroundRebuild()
	{
		if (m_rebuildThread.joinable())
		{
			m_rebuildThread.join();
		}
		if (!m_rebuildJob)
		{
			return;
		}

		const int32 index = FindControl(m_rebuildJob->GetInstanceId());
		if (index < 0)
		{
			// Deleting an instance waits for its rebuild, so this does not happen; the old contribution has no record left.
			ELOG("PCG: the instance of a background rebuild is gone");
			m_rebuildJob.reset();
			return;
		}

		Control& control = m_controls[index];
		if (m_rebuildJob->IsCanceled())
		{
			// Superseded (a newer request is queued) or dropped: the old build goes back.
			m_rebuildJob->Abort(control.instance);
			m_rebuildJob.reset();
			return;
		}

		const GeneratorBuildReport report = m_rebuildJob->Commit(m_worldEditor, control.instance, GetOtherGraphInstances(control.Id()), *this,
			MakeTerrainOverlapCheck(control.Id()));
		String error;
		ReportGraphInstanceBuild(control, report, error);
		// A refused build is not retried until the inputs change either.
		control.builtInputs = m_rebuildJobInputs;
		m_rebuildJob.reset();
	}

	void ProceduralEditMode::UpdateBackgroundRebuild()
	{
		if (m_rebuildJob && !m_rebuildRunning)
		{
			if (m_rebuildThread.joinable())
			{
				m_rebuildThread.join();
			}
			if (m_rebuildJob->NeedsSectionAssets() && !m_rebuildJob->IsCanceled())
			{
				// Section assets are read on the main thread; the job then runs again.
				m_rebuildJob->LoadSectionAssets();
				LaunchRebuildThread();
				return;
			}
			FinishBackgroundRebuild();
		}

		if (m_liveRebuild && IsEditIdle())
		{
			for (Control& control : m_controls)
			{
				if (control.kind != ControlKind::GraphInstance || control.instance.graphPath.empty())
				{
					continue;
				}
				const uint64 inputs = GetGraphInstanceInputs(control);
				const bool running = m_rebuildJob && m_rebuildJob->GetInstanceId() == control.Id() && !m_rebuildJob->IsCanceled();
				if (inputs != (running ? m_rebuildJobInputs : control.builtInputs))
				{
					RequestBackgroundRebuild(control.Id());
				}
			}
		}

		if (IsEditIdle())
		{
			StartQueuedRebuild();
		}
	}

	void ProceduralEditMode::WaitForBackgroundRebuild()
	{
		if (!m_rebuildJob)
		{
			return;
		}

		if (m_rebuildThread.joinable())
		{
			m_rebuildThread.join();
		}
		m_rebuildRunning = false;
		while (m_rebuildJob->NeedsSectionAssets() && !m_rebuildJob->IsCanceled())
		{
			m_rebuildJob->LoadSectionAssets();
			m_rebuildJob->Run();
		}
		FinishBackgroundRebuild();
	}

	void ProceduralEditMode::CancelBackgroundRebuilds()
	{
		m_rebuildQueue.clear();
		if (m_rebuildJob)
		{
			m_rebuildJob->Cancel();
		}
		WaitForBackgroundRebuild();
	}

	void ProceduralEditMode::OnUpdate(float)
	{
		// A release outside the viewport sends no OnMouseUp: end a rectangle handle drag once the button is up.
		if (m_rectangleDrag.active && ImGui::GetCurrentContext() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			EndRectangleDrag();
		}

		UpdateBackgroundRebuild();
	}

	void ProceduralEditMode::EndRectangleDrag()
	{
		if (!m_rectangleDrag.active)
		{
			return;
		}

		const RectangleDrag drag = m_rectangleDrag;
		m_rectangleDrag = RectangleDrag();
		const int32 index = FindControl(drag.controlId);
		if (drag.moved && index >= 0)
		{
			MarkChanged(m_controls[index]);
		}
	}

	void ProceduralEditMode::SetLiveRebuild(const bool enabled)
	{
		if (enabled && !m_liveRebuild)
		{
			// What is in the world now counts as built: only edits from here on rebuild.
			for (Control& control : m_controls)
			{
				if (control.kind == ControlKind::GraphInstance)
				{
					control.builtInputs = GetGraphInstanceInputs(control);
				}
			}
		}
		m_liveRebuild = enabled;
	}

	std::vector<pcg::WorldRect> ProceduralEditMode::GetBuiltRects(const Control& control)
	{
		// A graph instance that wrote terrain may have written anywhere inside its area: it covers its area's
		// bounds (the recorded influence). One that only placed objects is not in the way of a terrain writer.
		// Between graph instances the areas themselves are compared (AreasOverlap, sharing an edge is fine), see
		// CheckOverlap.
		if (control.Content().IsEmpty() || (control.kind == ControlKind::GraphInstance && control.Content().terrain.IsEmpty()))
		{
			return {};
		}

		// What the last build covered, as recorded with it (not recomputed from the current spline or
		// asset, which may have changed since).
		if (control.kind == ControlKind::SectionPath && !control.path.builtRects.empty())
		{
			return control.path.builtRects;
		}

		return { control.Content().influence };
	}

	bool ProceduralEditMode::CheckOverlap(const Control& control, const std::vector<pcg::WorldRect>& rects, String& error) const
	{
		for (const Control& other : m_controls)
		{
			if (other.Id() == control.Id())
			{
				continue;
			}

			// Graph instance areas may touch each other; RebuildGraphInstance compares them exactly (AreasOverlap).
			if (control.kind == ControlKind::GraphInstance && other.kind == ControlKind::GraphInstance)
			{
				continue;
			}

			// Only controls that put something into the world can be in the way.
			const std::vector<pcg::WorldRect> otherRects = GetBuiltRects(other);
			for (const pcg::WorldRect& rect : rects)
			{
				for (const pcg::WorldRect& otherRect : otherRects)
				{
					if (pcg::RectsOverlap(rect, otherRect))
					{
						error = "Rebuild refused: the influence area of '" + control.Name() + "' overlaps the one of '"
							+ other.Name() + "' (" + FormatId(other.Id()) + "). Move them apart first.";
						return false;
					}
				}
			}
		}

		return true;
	}

	String ProceduralEditMode::DescribeReport(const OasisBuildReport& report, const String& what)
	{
		std::ostringstream stream;
		stream << what << " in " << static_cast<int>(report.milliseconds + 0.5) << " ms: "
			<< report.terrainSamples << " terrain samples, placements created " << report.created
			<< ", updated " << report.updated << ", removed " << report.removed << ", kept " << report.kept << ".";
		if (!report.touchedPages.empty())
		{
			stream << " The far-terrain bake of " << report.touchedPages.size()
				<< " page(s) is stale (run mmo_edit --bake-terrain-lod).";
		}
		return stream.str();
	}

	bool ProceduralEditMode::CheckRecipe(String& error)
	{
		if (m_recipeLoaded)
		{
			return true;
		}

		// Without the recipe's asset lists a rebuild would reconcile every owned object away.
		error = String("Rebuild refused: the oasis recipe '") + RecipeFileName
			+ "' is missing or not valid JSON, so there are no asset lists. Fix it and reopen the world.";
		SetStatus(error, StatusLevel::Error);
		ELOG(error);
		return false;
	}

	bool ProceduralEditMode::Rebuild(const uint64 controlId, String& error)
	{
		// One terrain writer at a time.
		WaitForBackgroundRebuild();
		m_rebuildQueue.erase(std::remove(m_rebuildQueue.begin(), m_rebuildQueue.end(), controlId), m_rebuildQueue.end());

		const int32 index = FindControl(controlId);
		if (index < 0)
		{
			error = "No procedural control with id " + FormatId(controlId);
			return false;
		}

		Control& control = m_controls[index];
		if (control.kind == ControlKind::Placed)
		{
			// Placed controls generate nothing themselves; graph instances read them.
			SetStatus("'" + control.Name() + "' is a placed control; graph instances over it build its content.", StatusLevel::Ok);
			return true;
		}

		switch (control.kind)
		{
		case ControlKind::Oasis:
			return RebuildOasisControl(control, error);
		case ControlKind::SectionPath:
			return RebuildSectionPathControl(control, error);
		case ControlKind::GraphInstance:
			return RebuildGraphInstanceControl(control, error);
		case ControlKind::Placed:
			break;
		}
		return true;
	}

	bool ProceduralEditMode::RebuildOasisControl(Control& control, String& error)
	{
		if (!CheckRecipe(error))
		{
			return false;
		}

		pcg::OasisRecord& record = control.oasis;
		// What the previous build covered: the rebuild restores the terrain there too.
		const std::vector<pcg::WorldRect> previousRects = GetBuiltRects(control);

		// Before the build changes the terrain; the build itself only uses the loop's x/z.
		EnsureHeights(control, false);

		// The influence of the loop as it is now (does not depend on the heights). An invalid loop is
		// reported by the rebuild itself.
		pcg::OasisShape shape;
		if (pcg::PrepareOasis(record.loop, record.seed, record.params, [](float, float) { return 0.0f; }, shape) == pcg::OasisError::None &&
			!CheckOverlap(control, { pcg::GetInfluenceRect(shape) }, error))
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		const OasisBuildReport report = RebuildOasis(m_worldEditor, record, m_assets);
		if (!report.ok)
		{
			error = "Rebuild of '" + record.name + "' failed: " + report.error;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		MarkChanged(control);
		String text = DescribeReport(report, "Rebuilt '" + record.name + "'");
		const std::vector<String> warnings = GetRegroundWarnings(control, previousRects);
		for (const String& warning : warnings)
		{
			text += "\nWarning: " + warning;
		}
		SetStatus(text, warnings.empty() ? StatusLevel::Ok : StatusLevel::Warning);
		if (warnings.empty())
		{
			ILOG(m_status);
		}
		else
		{
			WLOG(m_status);
		}
		return true;
	}

	std::vector<String> ProceduralEditMode::GetRegroundWarnings(const Control& control, std::vector<pcg::WorldRect> rects) const
	{
		std::vector<String> warnings;
		if (m_rebuildingAll)
		{
			return warnings;
		}

		const std::vector<pcg::WorldRect> builtRects = GetBuiltRects(control);
		rects.insert(rects.end(), builtRects.begin(), builtRects.end());
		for (const Control& other : m_controls)
		{
			if (other.kind != ControlKind::GraphInstance || other.instance.area.points.empty())
			{
				continue;
			}

			const pcg::graph::Bounds2D area = pcg::graph::SplineBounds(other.instance.area);
			const pcg::WorldRect areaRect{ area.minX, area.minZ, area.maxX, area.maxZ };
			const bool overlaps = std::any_of(rects.begin(), rects.end(), [&areaRect](const pcg::WorldRect& rect)
			{
				return pcg::RectsOverlap(rect, areaRect);
			});
			if (overlaps)
			{
				warnings.push_back("rebuild graph instance " + other.Name() + " (" + FormatId(other.Id()) + ") to re-ground its placements");
			}
		}
		return warnings;
	}

	bool ProceduralEditMode::RebuildSectionPathControl(Control& control, String& error)
	{
		pcg::SectionPathRecord& record = control.path;
		// What the previous build covered: the rebuild restores the terrain there too.
		const std::vector<pcg::WorldRect> previousRects = GetBuiltRects(control);

		// Always read from disk: the panel's copy may be older than the last bake.
		pcg::SectionAsset asset;
		String assetError;
		if (!LoadSectionAsset(record.sectionAsset, asset, assetError))
		{
			m_sectionAssets[record.sectionAsset] = CachedSectionAsset{ false, pcg::SectionAsset(), assetError };
			error = "Rebuild of '" + record.name + "' refused: " + assetError;
			SetStatus(error, StatusLevel::Error);
			ELOG(error);
			return false;
		}
		m_sectionAssets[record.sectionAsset] = CachedSectionAsset{ true, asset, String() };

		std::vector<String> warnings;
		String staleWarning;
		if (IsSectionAssetStale(record.sectionAsset, asset, staleWarning))
		{
			warnings.push_back(staleWarning);
			WLOG("'" << record.name << "': " << staleWarning);
		}

		pcg::SectionPathShape probe;
		String probeError;
		if (!PrepareSectionPathProbe(record, asset, probe, probeError))
		{
			error = "Rebuild of '" + record.name + "' failed: Invalid section path: " + probeError;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		std::vector<pcg::WorldRect> rects;
		pcg::GetSectionCopyRects(probe, rects);
		if (!CheckOverlap(control, rects, error))
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		const OasisBuildReport report = RebuildSectionPath(m_worldEditor, record, asset);
		if (!report.ok)
		{
			error = "Rebuild of '" + record.name + "' failed: " + report.error;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		// Persisted with the record: the overlap check of other controls uses what this build covers.
		record.builtRects = record.content.IsEmpty() ? std::vector<pcg::WorldRect>() : rects;
		MarkChanged(control);
		warnings.insert(warnings.end(), report.warnings.begin(), report.warnings.end());
		const std::vector<String> regroundWarnings = GetRegroundWarnings(control, previousRects);
		warnings.insert(warnings.end(), regroundWarnings.begin(), regroundWarnings.end());

		String text = DescribeReport(report, "Rebuilt '" + record.name + "'") + " Layout: " + DescribeSectionLayout(probe) + ".";
		for (const String& warning : warnings)
		{
			text += "\nWarning: " + warning;
		}
		SetStatus(text, warnings.empty() ? StatusLevel::Ok : StatusLevel::Warning);
		if (warnings.empty())
		{
			ILOG(m_status);
		}
		else
		{
			WLOG(m_status);
		}
		return true;
	}

	bool ProceduralEditMode::RebuildAll(String& error)
	{
		error.clear();
		WaitForBackgroundRebuild();
		m_rebuildQueue.clear();

		// Placed controls build nothing themselves (graph instances read them). Graph instances sample the terrain
		// the oases and section paths write, so they go last (list order within each group).
		std::vector<uint64> ids;
		std::vector<uint64> graphInstanceIds;
		for (const Control& control : m_controls)
		{
			if (control.kind == ControlKind::GraphInstance)
			{
				graphInstanceIds.push_back(control.Id());
			}
			else if (control.kind != ControlKind::Placed)
			{
				ids.push_back(control.Id());
			}
		}
		ids.insert(ids.end(), graphInstanceIds.begin(), graphInstanceIds.end());

		uint32 rebuilt = 0;
		bool warned = false;
		// The graph instances are rebuilt below anyway: no "rebuild graph instance" hints from the terrain writers.
		m_rebuildingAll = true;
		for (const uint64 id : ids)
		{
			String controlError;
			if (Rebuild(id, controlError))
			{
				++rebuilt;
				warned = warned || m_statusLevel == StatusLevel::Warning;
			}
			else
			{
				error += (error.empty() ? "" : "\n") + controlError;
			}
		}
		m_rebuildingAll = false;

		if (!error.empty())
		{
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		SetStatus("Rebuilt " + std::to_string(rebuilt) + " procedural control(s)."
			+ (warned ? " Some builds reported warnings (see the log)." : ""), warned ? StatusLevel::Warning : StatusLevel::Ok);
		return true;
	}

	bool ProceduralEditMode::DeleteControl(const uint64 id, String& error)
	{
		// One terrain writer at a time; a deleted instance is not rebuilt any more.
		WaitForBackgroundRebuild();
		m_rebuildQueue.erase(std::remove(m_rebuildQueue.begin(), m_rebuildQueue.end(), id), m_rebuildQueue.end());

		const int32 index = FindControl(id);
		if (index < 0)
		{
			error = "No procedural control with id " + FormatId(id);
			return false;
		}

		Control& control = m_controls[index];
		if (control.kind == ControlKind::Placed)
		{
			// Nothing in the world belongs to a placed control.
			SetStatus("Deleted '" + control.Name() + "'.", StatusLevel::Ok);
			ILOG(m_status);
		}
		else
		{
			OasisBuildReport report;
			switch (control.kind)
			{
			case ControlKind::Oasis:
				report = DeleteOasisContent(m_worldEditor, control.oasis);
				break;
			case ControlKind::SectionPath:
				report = DeleteSectionPathContent(m_worldEditor, control.path);
				break;
			case ControlKind::GraphInstance:
				report = DeleteGraphInstanceContent(m_worldEditor, control.instance, *this);
				break;
			case ControlKind::Placed:
				break;
			}
			if (!report.ok)
			{
				error = "Delete of '" + control.Name() + "' failed: " + report.error;
				control.lastReport = error;
				control.lastReportOk = false;
				SetStatus(error, StatusLevel::Error);
				return false;
			}

			SetStatus(DescribeReport(report, "Deleted '" + control.Name() + "'"), StatusLevel::Ok);
			ILOG(m_status);
		}

		m_deletedIds.push_back(id);
		m_controls.erase(m_controls.begin() + index);
		OnControlsChanged();
		SelectControl(m_controls.empty() ? -1 : std::min(index, static_cast<int32>(m_controls.size()) - 1));
		return true;
	}

	const ProceduralEditMode::CachedSectionAsset& ProceduralEditMode::GetCachedSectionAsset(const String& assetPath)
	{
		const auto found = m_sectionAssets.find(assetPath);
		if (found != m_sectionAssets.end())
		{
			return found->second;
		}

		CachedSectionAsset cached;
		cached.valid = LoadSectionAsset(assetPath, cached.asset, cached.error);
		return m_sectionAssets.emplace(assetPath, std::move(cached)).first->second;
	}

	bool ProceduralEditMode::DrawSectionAssetCombo(const char* label, String& assetPath)
	{
		bool changed = false;
		if (ImGui::BeginCombo(label, assetPath.empty() ? "(none)" : assetPath.c_str()))
		{
			// Listed only while the combo is open (the registry is scanned).
			const std::vector<String> assets = ListSectionAssets();
			if (assets.empty())
			{
				ImGui::TextDisabled("No baked sections in %s", SectionAssetFolder);
			}
			for (const String& asset : assets)
			{
				const bool selected = asset == assetPath;
				if (ImGui::Selectable(asset.c_str(), selected) && !selected)
				{
					assetPath = asset;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	void ProceduralEditMode::DrawDetails()
	{
		ImGui::Separator();
		ImGui::Text("Procedural Controls");

		if (ImGui::Button("New Oasis"))
		{
			CreateOasisAtViewportCentre();
		}
		ImGui::SameLine();
		if (ImGui::Button("New Graph Instance"))
		{
			CreateGraphInstanceAtViewportCentre();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("A %.0f m square area around the viewport centre (refused if it overlaps another graph instance).\n"
				"Pick its graph in the details, edit the area like a spline, then Rebuild.", NewGraphInstanceSize);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_controls.empty());
		if (ImGui::Button("Rebuild All"))
		{
			String error;
			RebuildAll(error);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		bool live = m_liveRebuild;
		if (ImGui::Checkbox("Live Rebuild", &live))
		{
			SetLiveRebuild(live);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Rebuilds a graph instance in the background as soon as an edit of it ends: a spline point or\n"
				"gizmo released, a field left. Edits of its area, graph, seed, parameters and of the placed\n"
				"controls reaching into it count. The world shows the old build until the new one is ready.");
		}

		ImGui::BeginDisabled(m_newPathAsset.empty());
		if (ImGui::Button("New Section Path"))
		{
			CreateSectionPathAtViewportCentre();
		}
		ImGui::SameLine();
		if (ImGui::Button("New Section Loop"))
		{
			CreateSectionLoopAtViewportCentre();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Closed 8 point loop of radius %.0f m around the viewport centre, height mode smoothed ground,\n"
				"floor side inside. Sharp points get the section's corner variants; so do tight smooth curves\n"
				"(radius below about section length / (2 x corner angle in radians)).", NewLoopRadius);
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		DrawSectionAssetCombo("##newPathAsset", m_newPathAsset);

		DrawNewControlMenu();

		if (m_rebuildJob)
		{
			const int32 building = FindControl(m_rebuildJob->GetInstanceId());
			ImGui::TextColored(StatusOkColor, "Rebuilding '%s' in the background (%.1f s)%s", building >= 0 ? m_controls[building].Name().c_str() : "?",
				m_rebuildJob->GetElapsedSeconds(), m_rebuildJob->IsCanceled() ? ", canceled" : "");
		}
		if (!m_rebuildQueue.empty())
		{
			ImGui::TextDisabled("%zu rebuild(s) queued", m_rebuildQueue.size());
		}

		if (!m_status.empty())
		{
			const ImVec4& color = m_statusLevel == StatusLevel::Error ? StatusErrorColor
				: (m_statusLevel == StatusLevel::Warning ? StatusWarningColor : StatusOkColor);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", m_status.c_str());
			ImGui::PopStyleColor();
		}

		if (HasUnsavedChanges())
		{
			ImGui::TextDisabled("Procedural records changed. Save the world to keep them.");
		}

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable("##controls", 3, tableFlags, ImVec2(-FLT_MIN, 7 * ImGui::GetTextLineHeightWithSpacing())))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < m_controls.size(); ++i)
			{
				const Control& control = m_controls[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// A placed control whose type is missing or invalid is listed in red (kept, skipped by resolution).
				const pcg::graph::ControlType* placedType = control.kind == ControlKind::Placed ? m_controlLibrary.Find(control.placed.typePath) : nullptr;
				const bool typeMissing = control.kind == ControlKind::Placed && !placedType;
				if (typeMissing)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
				}
				const String label = control.Name() + (control.dirty ? " *" : "");
				if (ImGui::Selectable(label.c_str(), static_cast<int32>(i) == m_selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					SelectControl(static_cast<int32>(i));
				}
				ImGui::TableNextColumn();
				if (control.kind == ControlKind::Placed)
				{
					ImGui::TextUnformatted(placedType ? (placedType->name.empty() ? FileStem(control.placed.typePath).c_str() : placedType->name.c_str())
						: (FileStem(control.placed.typePath) + " (missing)").c_str());
				}
				else if (control.kind == ControlKind::GraphInstance)
				{
					ImGui::TextUnformatted("Graph Instance");
				}
				else
				{
					ImGui::TextUnformatted(control.kind == ControlKind::Oasis ? "Oasis" : "Section Path");
				}
				if (typeMissing)
				{
					ImGui::PopStyleColor();
				}
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", FormatId(control.Id()).c_str());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (m_selected < 0 || m_selected >= static_cast<int32>(m_controls.size()))
		{
			return;
		}

		ImGui::Separator();

		// Placed controls build nothing themselves.
		if (m_controls[m_selected].kind != ControlKind::Placed)
		{
			if (ImGui::Button("Rebuild"))
			{
				// Graph instances rebuild in the background; oases and section paths are quick.
				if (m_controls[m_selected].kind == ControlKind::GraphInstance)
				{
					RequestBackgroundRebuild(m_controls[m_selected].Id());
				}
				else
				{
					String error;
					Rebuild(m_controls[m_selected].Id(), error);
				}
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Delete"))
		{
			m_pendingDeleteId = m_controls[m_selected].Id();
			ImGui::OpenPopup("Delete Control?");
		}

		if (ImGui::BeginPopupModal("Delete Control?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const int32 pending = FindControl(m_pendingDeleteId);
			if (pending >= 0 && m_controls[pending].kind == ControlKind::Placed)
			{
				ImGui::Text("Remove the placed control '%s'?\n(Graph instances over it change on their next rebuild.)", m_controls[pending].Name().c_str());
			}
			else if (pending >= 0 && m_controls[pending].kind == ControlKind::GraphInstance)
			{
				ImGui::Text("Remove the graph instance '%s' and every object it spawned?", m_controls[pending].Name().c_str());
			}
			else
			{
				ImGui::Text("Remove '%s' and everything it generated\n(terrain changes, paint, water, objects)?",
					pending >= 0 ? m_controls[pending].Name().c_str() : "?");
			}
			if (ImGui::Button("Delete"))
			{
				String error;
				DeleteControl(m_pendingDeleteId, error);
				m_pendingDeleteId = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_pendingDeleteId = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Rebuild/Delete may have changed the list.
		if (m_selected < 0 || m_selected >= static_cast<int32>(m_controls.size()))
		{
			return;
		}
		Control& current = m_controls[m_selected];

		ImGui::Text("Id: %s", FormatId(current.Id()).c_str());

		char nameBuffer[128];
		std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", current.Name().c_str());
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
		{
			current.Name() = nameBuffer;
			MarkChanged(current);
		}

		if (current.kind == ControlKind::Placed)
		{
			DrawPlacedControlDetails(current);
			return;
		}

		if (current.kind == ControlKind::GraphInstance)
		{
			DrawGraphInstanceDetails(current);
			return;
		}

		uint64& seedRef = current.kind == ControlKind::Oasis ? current.oasis.seed : current.path.params.seed;
		uint64 seed = seedRef;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed))
		{
			seedRef = seed;
			MarkChanged(current);
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
		{
			seedRef = RandomSeed();
			MarkChanged(current);
		}

		if (current.kind == ControlKind::Oasis)
		{
			DrawOasisDetails(current);
		}
		else
		{
			DrawSectionPathDetails(current);
		}
	}

	void ProceduralEditMode::DrawNewControlMenu()
	{
		if (m_activeBiome.empty())
		{
			const std::vector<String> biomes = m_controlLibrary.ListBiomes();
			if (!biomes.empty())
			{
				m_activeBiome = biomes.front();
			}
		}

		ImGui::BeginDisabled(m_activeBiome.empty());
		if (ImGui::Button("New Control"))
		{
			ImGui::OpenPopup("##newControlMenu");
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Places a control of one of the active biome's types at the viewport centre:\n"
				"a loop as a %.0f m square, a spline as %.0f m along the view, a volume (half extents %.0f m),\n"
				"a rectangle (%.0f m x %.0f m) or a point.",
				NewControlSize, NewControlSize, NewVolumeHalfExtent, NewRectangleHalfExtent * 2.0f, NewRectangleHalfExtent * 2.0f);
		}

		if (ImGui::BeginPopup("##newControlMenu"))
		{
			// Listed only while the menu is open (the registry is scanned).
			const std::vector<String> types = m_controlLibrary.ListControlTypes(m_activeBiome);
			if (types.empty())
			{
				ImGui::TextDisabled("No control types in %s%s/Controls", ControlLibrary::BiomesFolder, m_activeBiome.c_str());
			}
			for (const String& typePath : types)
			{
				String error;
				const pcg::graph::ControlType* type = m_controlLibrary.Find(typePath, &error);
				if (!type)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
					ImGui::BeginDisabled();
					ImGui::MenuItem(FileStem(typePath).c_str());
					ImGui::EndDisabled();
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					{
						ImGui::SetTooltip("%s", error.c_str());
					}
					continue;
				}

				const String label = (type->name.empty() ? FileStem(typePath) : type->name) + "##" + typePath;
				if (ImGui::MenuItem(label.c_str(), ShapeName(type->shape)))
				{
					CreatePlacedControlAtViewportCentre(typePath);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", typePath.c_str());
				}
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("Biome##activeBiome", m_activeBiome.empty() ? "(none)" : m_activeBiome.c_str()))
		{
			const std::vector<String> biomes = m_controlLibrary.ListBiomes();
			if (biomes.empty())
			{
				ImGui::TextDisabled("No biomes in %s", ControlLibrary::BiomesFolder);
			}
			for (const String& biome : biomes)
			{
				if (ImGui::Selectable(biome.c_str(), biome == m_activeBiome))
				{
					m_activeBiome = biome;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Reload Types"))
		{
			m_controlLibrary.InvalidateAll();
			SetStatus("Control types will be read again.", StatusLevel::Ok);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Reads every control type again (after editing .hctl files outside the editor).");
		}
	}

	void ProceduralEditMode::DrawPlacedControlDetails(Control& control)
	{
		pcg::graph::ControlInstance& instance = control.placed;

		String typeError;
		const pcg::graph::ControlType* type = m_controlLibrary.Find(instance.typePath, &typeError);
		ImGui::Text("Type: %s", instance.typePath.c_str());
		if (type)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%s, %s)", type->name.c_str(), ShapeName(type->shape));
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
			ImGui::TextWrapped("%s. The control is kept and skipped by graph instances until its type exists again.", typeError.c_str());
			ImGui::PopStyleColor();
		}

		// Tags: applied when the field loses focus, so typing "a, " is not reformatted on the way.
		if (m_tagsEditing && m_tagsControlId != instance.id)
		{
			// The edit was typed for another control (the selection changed without SelectControl).
			ApplyPendingTags();
		}
		if (!m_tagsEditing)
		{
			m_tagsBuffer = JoinTags(instance.extraTags);
		}
		ImGui::InputText("Extra tags", &m_tagsBuffer);
		m_tagsEditing = ImGui::IsItemActive();
		m_tagsControlId = instance.id;
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			std::set<String> tags = SplitTags(m_tagsBuffer);
			if (tags != instance.extraTags)
			{
				instance.extraTags = std::move(tags);
				MarkChanged(control);
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Comma-separated tags added to the type's default tags. Graphs find controls by tag.");
		}
		if (type && !type->defaultTags.empty())
		{
			ImGui::TextDisabled("Default tags: %s", JoinTags(type->defaultTags).c_str());
		}

		if (control.HasTransform())
		{
			const bool volume = IsPlacedVolume(control);
			const bool rectangle = IsPlacedRectangle(control);
			ImGui::TextWrapped(rectangle
				? "Rectangle: drag a side or corner handle to resize it (the opposite side or corner stays); the gizmo moves it, Rotate turns it around up, Scale sets its half extents. Click its centre marker in the viewport to select it."
				: (volume
					? "Volume: the gizmo moves it, Rotate turns it around up, Scale sets its half extents. Click its centre marker in the viewport to select it."
					: "Point: the gizmo moves it and Rotate turns it around up. Click its marker in the viewport to select it."));

			bool changed = false;
			float position[3] = { instance.position.x, instance.position.y, instance.position.z };
			if (ImGui::DragFloat3("Position", position, 0.1f, 0.0f, 0.0f, "%.2f"))
			{
				instance.position = Vector3(position[0], position[1], position[2]);
				changed = true;
			}

			float yawDegrees = SelectedControlTransform::ExtractYaw(instance.rotation) * 180.0f / PiValue;
			if (ImGui::DragFloat("Yaw (deg)", &yawDegrees, 0.5f, -360.0f, 360.0f, "%.1f"))
			{
				instance.rotation = SelectedControlTransform::FromYaw(yawDegrees * PiValue / 180.0f);
				changed = true;
			}

			if (rectangle)
			{
				// A rectangle has no height: only x and z.
				float extents[2] = { instance.halfExtents.x, instance.halfExtents.z };
				if (ImGui::DragFloat2("Half extents", extents, 0.1f, pcg::graph::MinRectangleHalfExtent, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					instance.halfExtents.x = extents[0];
					instance.halfExtents.z = extents[1];
					changed = true;
				}
			}
			else if (volume)
			{
				float extents[3] = { instance.halfExtents.x, instance.halfExtents.y, instance.halfExtents.z };
				if (ImGui::DragFloat3("Half extents", extents, 0.1f, SelectedControlTransform::MinHalfExtent, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					instance.halfExtents = Vector3(extents[0], extents[1], extents[2]);
					changed = true;
				}
			}

			if (changed)
			{
				MarkChanged(control);
				if (m_transformSelectable && m_transformSelectable->GetControlId() == instance.id)
				{
					m_transformSelectable->NotifyTransformChanged();
				}
			}
		}
		else
		{
			ImGui::TextWrapped("%s: %d points. Click a point to select it (the gizmo moves it, height included; Rotate tilts and turns its tangents) or drag it at its height, Alt+click the curve to add a point, Delete or Ctrl+click removes a point (minimum %u).",
				instance.spline.closed ? "Loop" : "Spline", static_cast<int>(instance.spline.points.size()), GetMinPoints(control));
			DrawPointDetails(control);
		}

		if (type)
		{
			if (type->groups.empty())
			{
				ImGui::TextDisabled("The type has no properties.");
			}
			else if (DrawControlProperties(*type, instance))
			{
				MarkChanged(control);
			}
		}
	}

	void ProceduralEditMode::DrawGraphInstanceDetails(Control& control)
	{
		pcg::GraphInstanceRecord& record = control.instance;

		// Graph picker: the active biome's graphs (combo), or every graph through the asset picker.
		String graphPath = record.graphPath;
		bool graphChanged = false;
		if (m_showAllGraphs)
		{
			static const std::set<String> s_graphExtensions = { GraphAssetExtension };
			std::string path = graphPath;
			if (AssetPickerWidget::Draw("Graph", path, s_graphExtensions, nullptr, nullptr, 0.0f) && path != graphPath)
			{
				graphPath = path;
				graphChanged = true;
			}
		}
		else if (ImGui::BeginCombo("Graph", graphPath.empty() ? "(none)" : graphPath.c_str()))
		{
			// Listed only while the combo is open (the registry is scanned).
			const String folder = String(ControlLibrary::BiomesFolder) + m_activeBiome + "/Graphs/";
			std::vector<String> graphs = m_activeBiome.empty() ? std::vector<String>() : AssetRegistry::ListFiles(folder, GraphAssetExtension);
			std::sort(graphs.begin(), graphs.end());
			if (graphs.empty())
			{
				ImGui::TextDisabled("No graphs in %s", m_activeBiome.empty() ? ControlLibrary::BiomesFolder : folder.c_str());
			}
			for (const String& graph : graphs)
			{
				const bool selected = graph == graphPath;
				if (ImGui::Selectable(graph.c_str(), selected) && !selected)
				{
					graphPath = graph;
					graphChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		if (graphChanged)
		{
			record.graphPath = graphPath;
			MarkChanged(control);
		}

		ImGui::Checkbox("All biomes##graphPicker", &m_showAllGraphs);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Off: the graphs of the active biome (Biome combo above). On: every graph.");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(record.graphPath.empty());
		if (ImGui::Button("Reload Graph"))
		{
			m_graphs.erase(record.graphPath);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Reads the graph file again for this panel. Rebuild always uses the saved file.");
		}

		const CachedGraph* cached = record.graphPath.empty() ? nullptr : &GetCachedGraph(record.graphPath);
		if (record.graphPath.empty())
		{
			ImGui::TextColored(StatusWarningColor, "Pick a graph before rebuilding.");
		}
		else if (!cached->valid)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
			ImGui::TextWrapped("%s", cached->error.c_str());
			ImGui::PopStyleColor();
		}

		uint64 seed = record.seed;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed))
		{
			record.seed = seed;
			MarkChanged(control);
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
		{
			record.seed = RandomSeed();
			MarkChanged(control);
		}

		uint64 pointLimit = static_cast<uint64>(record.pointLimit);
		if (ImGui::InputScalar("Point limit", ImGuiDataType_U64, &pointLimit))
		{
			record.pointLimit = static_cast<size_t>(std::max<uint64>(pointLimit, 1));
			MarkChanged(control);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Most points one node may produce; a node over it stops with a warning.");
		}

		if (ImGui::DragFloat("Border fade (m)", &record.borderFade, 0.25f, 0.0f, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
		{
			MarkChanged(control);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Terrain writes fade to nothing over this distance inside the area border (0 = hard edge).");
		}

		if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			std::set<String> declared;
			if (cached && cached->valid)
			{
				if (cached->graph.parameters.empty())
				{
					ImGui::TextDisabled("The graph has no parameters.");
				}

				for (const pcg::graph::ParameterDef& parameter : cached->graph.parameters)
				{
					declared.insert(parameter.name);
					ImGui::PushID(parameter.name.c_str());
					const auto found = record.parameterOverrides.find(parameter.name);
					const bool overridden = found != record.parameterOverrides.end()
						&& pcg::graph::TypeOf(found->second) == pcg::graph::TypeOf(parameter.defaultValue);
					pcg::graph::AttributeValue value = overridden ? found->second : parameter.defaultValue;

					if (overridden)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, StatusWarningColor);
					}
					bool edited = false;
					const char* label = parameter.name.c_str();
					switch (pcg::graph::TypeOf(value))
					{
					case pcg::graph::AttributeType::Float:
						edited = ImGui::DragFloat(label, &std::get<float>(value), 0.01f, 0.0f, 0.0f, "%.3f");
						break;
					case pcg::graph::AttributeType::Int:
						edited = ImGui::InputScalar(label, ImGuiDataType_S64, &std::get<int64>(value));
						break;
					case pcg::graph::AttributeType::Bool:
						edited = ImGui::Checkbox(label, &std::get<bool>(value));
						break;
					case pcg::graph::AttributeType::Vector3:
					{
						Vector3& vector = std::get<Vector3>(value);
						float components[3] = { vector.x, vector.y, vector.z };
						edited = ImGui::DragFloat3(label, components, 0.01f, 0.0f, 0.0f, "%.3f");
						vector = Vector3(components[0], components[1], components[2]);
						break;
					}
					case pcg::graph::AttributeType::String:
						edited = ImGui::InputText(label, &std::get<String>(value));
						break;
					}
					if (overridden)
					{
						ImGui::PopStyleColor();
					}
					if (ImGui::IsItemHovered() && !parameter.description.empty())
					{
						ImGui::SetTooltip("%s", parameter.description.c_str());
					}

					if (edited)
					{
						record.parameterOverrides[parameter.name] = value;
						MarkChanged(control);
					}
					else if (overridden)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton("Reset"))
						{
							record.parameterOverrides.erase(parameter.name);
							MarkChanged(control);
						}
					}
					ImGui::PopID();
				}
			}
			else
			{
				ImGui::TextDisabled("The parameters are shown once the graph can be read.");
			}

			// Overrides the graph does not declare (or with another type than its default) are kept but unused.
			for (auto it = record.parameterOverrides.begin(); it != record.parameterOverrides.end();)
			{
				const bool known = declared.count(it->first) > 0;
				bool typeMatches = true;
				if (known && cached && cached->valid)
				{
					for (const pcg::graph::ParameterDef& parameter : cached->graph.parameters)
					{
						if (parameter.name == it->first)
						{
							typeMatches = pcg::graph::TypeOf(parameter.defaultValue) == pcg::graph::TypeOf(it->second);
						}
					}
				}
				if ((known && typeMatches) || !cached || !cached->valid)
				{
					++it;
					continue;
				}

				ImGui::PushID(it->first.c_str());
				ImGui::TextColored(StatusWarningColor, known ? "Override '%s' has the wrong type (unused)" : "Override '%s' is not a parameter of the graph (unused)", it->first.c_str());
				ImGui::SameLine();
				const bool remove = ImGui::SmallButton("Remove");
				ImGui::PopID();
				if (remove)
				{
					it = record.parameterOverrides.erase(it);
					MarkChanged(control);
				}
				else
				{
					++it;
				}
			}
		}

		ImGui::TextWrapped("Area: %d points (sharp corners unless their tangents are reset to auto). Click a point to select it (the gizmo moves it) or drag it, Alt+click the outline to add a point, Delete or Ctrl+click removes a point (minimum %u). Areas of graph instances must not overlap; sharing an edge is fine.",
			static_cast<int>(record.area.points.size()), GetMinPoints(control));
		DrawPointDetails(control);

		ImGui::Text("Owned objects: %d", static_cast<int>(record.content.placements.size()));
		if (!control.lastReport.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, control.lastReportOk ? StatusOkColor : StatusErrorColor);
			ImGui::TextWrapped("Last build: %s", control.lastReport.c_str());
			ImGui::PopStyleColor();
		}
	}

	void ProceduralEditMode::DrawOasisDetails(Control& control)
	{
		pcg::OasisRecord& rec = control.oasis;

		ImGui::TextWrapped("Loop: %d points. The loop is a 3D curve. Click a point to select it (the gizmo moves it, height included; Rotate tilts and turns its tangents) or drag it at its height, Alt+click the curve to add a point, Delete or Ctrl+click removes a point (minimum %u).",
			static_cast<int>(rec.loop.points.size()), MinLoopPoints);

		DrawPointDetails(control);

		if (ImGui::Button("Reset parameters from recipe"))
		{
			// Only the parameters; the world is not touched until Rebuild.
			rec.params = m_defaultParams;
			MarkChanged(control);
		}

		if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;
			if (ImGui::TreeNodeEx("Basin", ImGuiTreeNodeFlags_DefaultOpen))
			{
				changed = DrawRows(rec.params, s_basinRows) || changed;
				int steps = static_cast<int>(rec.params.terraceSteps);
				if (ImGui::DragInt("Terrace steps", &steps, 0.1f, 0, 16))
				{
					rec.params.terraceSteps = static_cast<uint32>(std::max(steps, 0));
					changed = true;
				}
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Water"))
			{
				changed = DrawRows(rec.params, s_waterRows) || changed;
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Ground paint"))
			{
				changed = DrawRows(rec.params, s_paintRows) || changed;
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Rocks"))
			{
				changed = DrawRows(rec.params, s_rockRows) || changed;
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Vegetation"))
			{
				changed = DrawRows(rec.params, s_vegetationRows) || changed;
				ImGui::TreePop();
			}

			if (changed)
			{
				MarkChanged(control);
			}
		}
	}

	void ProceduralEditMode::DrawSectionPathDetails(Control& control)
	{
		pcg::SectionPathRecord& rec = control.path;

		if (DrawSectionAssetCombo("Section", rec.sectionAsset))
		{
			MarkChanged(control);
		}

		const CachedSectionAsset& cached = GetCachedSectionAsset(rec.sectionAsset);
		if (!cached.valid)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
			ImGui::TextWrapped("%s", cached.error.c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			const pcg::SectionAsset& asset = cached.asset;
			ImGui::TextDisabled("Section '%s': length %.1f m, half width %.1f m, side falloff %.1f m, %d variant(s).",
				asset.name.c_str(), asset.length, asset.halfWidth, asset.sideFalloff, static_cast<int>(asset.variants.size()));

			// Layout of the path as it is now, with its corner pieces, laid like a build lays it (flat ground; cheap for
			// editor-sized paths).
			pcg::SectionPathShape shape;
			String layoutError;
			if (rec.spline.points.size() >= GetMinPoints(control) && PrepareSectionPathProbe(rec, asset, shape, layoutError))
			{
				const pcg::SectionLayout& layout = shape.layout;
				const float reach = asset.halfWidth + asset.sideFalloff;
				const pcg::SectionLayoutCounts counts = pcg::CountSectionCopies(layout);
				const size_t inner = counts.inner;
				const size_t outer = counts.outer;
				const int straight = static_cast<int>(counts.straight);
				if (layout.minimumBendRadius >= std::numeric_limits<float>::max())
				{
					ImGui::TextDisabled("Length %.1f m, %d straight copies, %d inner / %d outer corners, no bends.", shape.path->GetLength(),
						straight, static_cast<int>(inner), static_cast<int>(outer));
				}
				else
				{
					ImGui::TextDisabled("Length %.1f m, %d straight copies, %d inner / %d outer corners, tightest bend radius %.1f m.",
						shape.path->GetLength(), straight, static_cast<int>(inner), static_cast<int>(outer), layout.minimumBendRadius);
				}

				const bool hasCornerVariants = std::any_of(asset.variants.begin(), asset.variants.end(),
					[](const pcg::SectionVariant& v) { return v.role != pcg::SectionVariantRole::Straight; });
				if (hasCornerVariants)
				{
					ImGui::TextDisabled("Corners at control points turning by %.1f deg or more (chords over half a section).", asset.cornerAngleDeg);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Tight smooth curves get corner pieces too: a curve with a radius below about\n"
							"length / (2 x corner angle in radians) = %.0f m turns by the corner angle between the chords.\n"
							"Overlay markers: green I = inner corner (towards the floor side), magenta O = outer corner.",
							asset.cornerAngleDeg > 0.0f ? asset.length / (2.0f * asset.cornerAngleDeg * 0.01745329252f) : 0.0f);
					}
				}
				if (shape.reversed)
				{
					ImGui::TextDisabled("The loop runs clockwise; it is laid against the point order to keep the floor side inside.");
				}

				if (layout.minimumBendRadius < reach)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, StatusWarningColor);
					ImGui::TextWrapped("Warning: the path bends tighter (radius %.1f m) than the section's half width + side falloff (%.1f m). On the inner side of that bend the terrain comes from the nearest part of the path. Widen the bend to avoid a fold.",
						layout.minimumBendRadius, reach);
					ImGui::PopStyleColor();
				}
			}
			else if (!layoutError.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, StatusErrorColor);
				ImGui::TextWrapped("Invalid section path: %s", layoutError.c_str());
				ImGui::PopStyleColor();
			}
		}

		// Opening keeps the loop's last span; closing needs at least as many points as a loop.
		bool closed = rec.spline.closed;
		ImGui::BeginDisabled(!closed && rec.spline.points.size() < MinLoopPoints);
		if (ImGui::Checkbox("Closed loop##sectionClosed", &closed))
		{
			rec.spline.closed = closed;
			MarkChanged(control);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("A closed loop joins the last point to the first and gets corner pieces at every sharp point.\n"
				"Needs at least %u points.", MinLoopPoints);
		}

		ImGui::TextWrapped("Path: %d points. The path is %s 3D curve. Click a point to select it (the gizmo moves it, height included; Rotate tilts and turns its tangents) or drag it at its height, Alt+click the curve to add a point, Delete or Ctrl+click removes a point (minimum %u).",
			static_cast<int>(rec.spline.points.size()), rec.spline.closed ? "a closed" : "an open", GetMinPoints(control));

		DrawPointDetails(control);

		if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			const char* variantModes[] = { "Random (seeded per copy)", "Sequence" };
			int variantMode = rec.params.sequentialVariants ? 1 : 0;
			if (ImGui::Combo("Variant mode", &variantMode, variantModes, IM_ARRAYSIZE(variantModes)))
			{
				rec.params.sequentialVariants = variantMode == 1;
				changed = true;
			}

			changed = ImGui::DragFloat("Join blend (m)", &rec.params.joinBlend, 0.05f, 0.0f, 50.0f, "%.2f") || changed;
			changed = ImGui::SliderFloat("Prop density", &rec.params.propDensity, 0.0f, 1.0f, "%.2f") || changed;

			// Combo index = SectionHeightMode value.
			const char* heightModes[] = { "Follow spline", "Follow ground", "Smoothed ground", "Keep ground" };
			int heightMode = std::clamp(static_cast<int>(rec.params.heightMode), 0, IM_ARRAYSIZE(heightModes) - 1);
			if (ImGui::Combo("Height mode", &heightMode, heightModes, IM_ARRAYSIZE(heightModes)))
			{
				rec.params.heightMode = static_cast<pcg::SectionHeightMode>(heightMode);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Follow spline: the centre line takes the curve's height.\n"
					"Follow ground: the base ground under the centre line.\n"
					"Smoothed ground: that ground averaged along the path over the smoothing window (keeps the\n"
					"large shape of the hand-made ground, levels its bumps and steps).\n"
					"Keep ground: the terrain height is not touched; only paint and props are placed.");
			}

			if (rec.params.heightMode == pcg::SectionHeightMode::SmoothedGround)
			{
				changed = ImGui::DragFloat("Smoothing window (m)##groundSmoothing", &rec.params.groundSmoothing, 0.25f, 0.0f, 500.0f, "%.1f",
					ImGuiSliderFlags_AlwaysClamp) || changed;
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Length of path the base ground is averaged over, centred on each point (0 = follow ground).");
				}
			}

			if (rec.spline.closed)
			{
				changed = ImGui::Checkbox("Floor inside##floorInside", &rec.params.floorInside) || changed;
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Lays the section with its floor side (-X) towards the loop's interior, whatever the winding.\n"
						"Off: laid in point order, the floor on the left looking along it (inside or outside depending on the winding).");
				}
			}

			changed = DrawSectionMassParams(rec.params) || changed;
			changed = DrawSectionFacadeParams(rec.params) || changed;

			if (changed)
			{
				MarkChanged(control);
			}
		}
	}

	void ProceduralEditMode::DrawPointDetails(Control& control)
	{
		PathSpline& loop = control.Spline();
		if (control.kind == ControlKind::Oasis && !control.oasis.heightsValid)
		{
			ImGui::TextColored(StatusWarningColor, "Waiting for the terrain to load before the loop can be edited.");
			return;
		}

		if (!SplineEditController::HasTangents(loop))
		{
			return;
		}

		const int32 point = m_splineEditor.GetSelectedSpline() == m_selected ? m_splineEditor.GetSelectedPoint() : -1;
		if (point >= 0 && point < static_cast<int32>(loop.points.size()))
		{
			const bool isAuto = loop.points[point].autoTangents;
			ImGui::Text("Selected point: %d, tangents %s", point, isAuto ? "automatic" : "manual");
			ImGui::TextDisabled("Drag the orange (in) or green (out) handle (it keeps its height); hollow = automatic, filled = manual.");
			ImGui::BeginDisabled(isAuto);
			if (ImGui::Button("Reset tangents to auto") && SplineEditController::ResetTangents(loop, static_cast<size_t>(point)))
			{
				MarkChanged(control);
				NotifyPointRotated();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
		}

		const bool anyManual = std::any_of(loop.points.begin(), loop.points.end(), [](const SplinePoint& p) { return !p.autoTangents; });
		ImGui::BeginDisabled(!anyManual);
		if (ImGui::Button("Reset all tangents") && SplineEditController::ResetAllTangents(loop))
		{
			MarkChanged(control);
			NotifyPointRotated();
		}
		ImGui::EndDisabled();
	}

	void ProceduralEditMode::NotifyPointRotated()
	{
		if (m_pointSelectable)
		{
			m_pointSelectable->rotationChanged(*m_pointSelectable);
		}
	}

	void ProceduralEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
		m_brushPositionValid = true;
	}

	void ProceduralEditMode::SetCursorRay(const Ray& ray)
	{
		m_cursorRay = ray;
		m_cursorRayValid = true;
	}

	void ProceduralEditMode::BeginPlaneDrag()
	{
		const int32 spline = m_splineEditor.GetSelectedSpline();
		const int32 point = m_splineEditor.GetSelectedPoint();
		if (spline < 0 || spline >= static_cast<int32>(m_controls.size()))
		{
			return;
		}

		const auto& points = m_controls[spline].Spline().points;
		if (point < 0 || point >= static_cast<int32>(points.size()))
		{
			return;
		}

		// Keep the grab offset, so the point (or handle) does not snap its centre to the cursor. The drag
		// runs on the horizontal plane through the grabbed point's or handle's current height, so it
		// keeps that height (the gizmo's Y arrow or a tangent pitch changes it).
		const SplinePoint& grabbed = points[point];
		float anchorX = grabbed.x;
		float anchorY = grabbed.y;
		float anchorZ = grabbed.z;
		m_splineEditor.GetDragAnchor(GetSplines(), anchorX, anchorY, anchorZ);
		m_dragPlaneY = anchorY;
		m_dragOffsetX = 0.0f;
		m_dragOffsetZ = 0.0f;

		Vector3 hit;
		if (m_cursorRayValid && IntersectHorizontalPlane(m_cursorRay, m_dragPlaneY, hit))
		{
			m_dragOffsetX = anchorX - hit.x;
			m_dragOffsetZ = anchorZ - hit.z;
		}
	}

	std::vector<PathSpline*> ProceduralEditMode::GetSplines()
	{
		std::vector<PathSpline*> splines;
		splines.reserve(m_controls.size());
		for (Control& control : m_controls)
		{
			splines.push_back(&control.Spline());
		}
		return splines;
	}

	float ProceduralEditMode::GetHandleHeight(const float x, const float z) const
	{
		if (!m_worldEditor.HasTerrain())
		{
			return 0.0f;
		}

		return m_worldEditor.GetTerrain()->GetSmoothHeightAt(x, z) + 0.3f;
	}

	void ProceduralEditMode::OnMouseDown(const float x, const float y)
	{
		WorldEditMode::OnMouseDown(x, y);

		// A drag whose release was missed (outside the viewport) ends before this click is handled, also when the gizmo
		// takes the click, so nothing can pull the stale side or corner.
		EndRectangleDrag();

		// The transform widget took the click (it sees the button first).
		if (m_worldEditor.IsTransforming())
		{
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		auto splines = GetEditableSplines();

		// Ctrl+click on a point deletes it.
		int32 controlIndex, pointIndex;
		if (io.KeyCtrl && m_splineEditor.HitTestPoint(io.MousePos.x, io.MousePos.y, splines, controlIndex, pointIndex))
		{
			m_splineEditor.Select(controlIndex, pointIndex);
			m_selected = controlIndex;
			DeleteSelectedPoint();
			return;
		}

		// A click on a side or corner handle of the selected rectangle starts dragging that handle.
		pcg::graph::RectangleHandle rectangleHandle = pcg::graph::RectangleHandle::SidePosX;
		if (!io.KeyCtrl && !io.KeyAlt && HitTestRectangleHandle(io.MousePos.x, io.MousePos.y, rectangleHandle))
		{
			const pcg::graph::ControlInstance& instance = m_controls[m_selected].placed;
			m_splineEditor.CancelDrag();
			m_rectangleDrag.active = true;
			m_rectangleDrag.controlId = instance.id;
			m_rectangleDrag.handle = rectangleHandle;

			// Keep the grab offset, so the handle does not jump to the cursor.
			const Vector3 anchor = pcg::graph::RectangleHandlePosition(instance, rectangleHandle);
			m_dragPlaneY = instance.position.y;
			m_dragOffsetX = 0.0f;
			m_dragOffsetZ = 0.0f;
			Vector3 hit;
			if (m_cursorRayValid && IntersectHorizontalPlane(m_cursorRay, m_dragPlaneY, hit))
			{
				m_dragOffsetX = anchor.x - hit.x;
				m_dragOffsetZ = anchor.z - hit.z;
			}
			return;
		}

		// A click on the marker of a placed volume or point selects it for the gizmo.
		if (!io.KeyCtrl && !io.KeyAlt)
		{
			const int32 transformIndex = HitTestTransformControl(io.MousePos.x, io.MousePos.y);
			if (transformIndex >= 0)
			{
				m_splineEditor.CancelDrag();
				m_selected = transformIndex;
				SelectTransformInEditor(transformIndex);
				return;
			}
		}

		// Point counts before the click, to find a point the click inserts (it needs an id on a section path
		// or placed control).
		std::vector<size_t> pointCounts;
		pointCounts.reserve(m_controls.size());
		for (const Control& control : m_controls)
		{
			pointCounts.push_back(control.Spline().points.size());
		}

		const Vector3* worldHit = m_brushPositionValid ? &m_brushPosition : nullptr;
		if (m_splineEditor.OnMouseDown(io.MousePos.x, io.MousePos.y, splines, io.KeyAlt, worldHit))
		{
			m_selected = m_splineEditor.GetSelectedSpline();
			if (m_selected >= 0 && m_selected < static_cast<int32>(m_controls.size()))
			{
				Control& control = m_controls[m_selected];
				const int32 inserted = m_splineEditor.GetSelectedPoint();
				std::vector<uint64>* pointIds = control.PointIds();
				uint64* nextPointId = control.NextPointId();
				if (pointIds && nextPointId && control.Spline().points.size() == pointCounts[m_selected] + 1 &&
					inserted >= 0 && static_cast<size_t>(inserted) <= pointIds->size())
				{
					// A new point gets a new id; ids are never reused.
					pointIds->insert(pointIds->begin() + inserted, (*nextPointId)++);

					// Graph instance areas are polygons: a new point is a sharp corner like the others.
					if (control.kind == ControlKind::GraphInstance)
					{
						MakeSharpCorner(control.Spline().points[inserted]);
					}
				}

				if (m_splineEditor.DragMoved())
				{
					// An inserted point is a change right away.
					MarkChanged(control);
				}
			}

			// A grabbed tangent handle belongs to the point that is already selected.
			if (m_splineEditor.GetDraggedHandle() == SplineEditController::TangentHandle::None)
			{
				SelectPointInEditor(m_selected, m_splineEditor.GetSelectedPoint());
			}

			if (m_splineEditor.IsDragging())
			{
				BeginPlaneDrag();
			}
		}
		else if (m_splineEditor.GetSelectedPoint() >= 0 || m_pointSelectable)
		{
			// A click next to the spline deselects the point (the control stays selected).
			ClearPointSelection();
		}
		else if (m_transformSelectable)
		{
			// A click elsewhere drops the gizmo of a volume or point (the control stays selected).
			ClearTransformSelection();
		}
	}

	void ProceduralEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		// The button may have been released outside the viewport (no OnMouseUp): such a drag is over.
		if (m_rectangleDrag.active && ImGui::GetCurrentContext() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			EndRectangleDrag();
		}

		if (m_rectangleDrag.active)
		{
			const int32 index = FindControl(m_rectangleDrag.controlId);
			if (index < 0 || !m_controls[index].HasTransform())
			{
				// The control went away during the drag.
				EndRectangleDrag();
				return;
			}

			Vector3 rectangleHit;
			if (!m_cursorRayValid || !IntersectHorizontalPlane(m_cursorRay, m_dragPlaneY, rectangleHit))
			{
				return;
			}

			Control& control = m_controls[index];
			const Vector3 oldPosition = control.placed.position;
			const Vector3 oldExtents = control.placed.halfExtents;
			pcg::graph::DragRectangleHandle(control.placed, m_rectangleDrag.handle, rectangleHit.x + m_dragOffsetX, rectangleHit.z + m_dragOffsetZ);
			if (control.placed.position != oldPosition || control.placed.halfExtents != oldExtents)
			{
				m_rectangleDrag.moved = true;
				MarkChanged(control);
				if (m_transformSelectable && m_transformSelectable->GetControlId() == control.placed.id)
				{
					m_transformSelectable->NotifyTransformChanged();
				}
			}
			return;
		}

		Vector3 hit;
		if (!m_splineEditor.IsDragging() || !m_cursorRayValid || !IntersectHorizontalPlane(m_cursorRay, m_dragPlaneY, hit))
		{
			return;
		}

		auto splines = GetEditableSplines();
		if (m_splineEditor.OnMouseMoved(hit.x + m_dragOffsetX, hit.z + m_dragOffsetZ, splines))
		{
			const int32 selected = m_splineEditor.GetSelectedSpline();
			if (selected >= 0 && selected < static_cast<int32>(m_controls.size()))
			{
				MarkChanged(m_controls[selected]);
			}

			// The transform widget follows a dragged point, and (in local space) the out tangent's yaw.
			if (m_pointSelectable)
			{
				if (m_splineEditor.GetDraggedHandle() == SplineEditController::TangentHandle::None)
				{
					m_pointSelectable->positionChanged(*m_pointSelectable);
				}
				else
				{
					m_pointSelectable->rotationChanged(*m_pointSelectable);
				}
			}
		}
	}

	void ProceduralEditMode::OnMouseMoved(const float x, const float y)
	{
		WorldEditMode::OnMouseMoved(x, y);
		m_brushPositionValid = false;
		m_cursorRayValid = false;
	}

	void ProceduralEditMode::OnMouseUp(const float x, const float y)
	{
		WorldEditMode::OnMouseUp(x, y);

		// The live rebuild follows now that the edit is idle.
		EndRectangleDrag();

		if (m_splineEditor.IsDragging())
		{
			const bool moved = m_splineEditor.DragMoved();
			m_splineEditor.OnMouseUp();
			const int32 selected = m_splineEditor.GetSelectedSpline();
			if (moved && selected >= 0 && selected < static_cast<int32>(m_controls.size()))
			{
				MarkChanged(m_controls[selected]);
			}
		}
	}

	void ProceduralEditMode::DeleteSelectedPoint()
	{
		auto splines = GetEditableSplines();
		const int32 selected = m_splineEditor.GetSelectedSpline();
		const int32 point = m_splineEditor.GetSelectedPoint();
		if (selected < 0 || selected >= static_cast<int32>(m_controls.size()) || point < 0)
		{
			return;
		}

		Control& control = m_controls[selected];
		const uint32 minPoints = GetMinPoints(control);
		if (!m_splineEditor.DeleteSelectedPoint(splines, minPoints))
		{
			SetStatus(String(DescribeKind(control)) + " keeps at least " + std::to_string(minPoints) + " points.", StatusLevel::Error);
			return;
		}

		std::vector<uint64>* pointIds = control.PointIds();
		if (pointIds && static_cast<size_t>(point) < pointIds->size())
		{
			// The id leaves with its point and is never handed out again.
			pointIds->erase(pointIds->begin() + point);
		}

		MarkChanged(control);
		// Indices behind the deleted point shifted; nothing stays selected. This also keeps the same
		// Delete key press, which the viewport applies to the editor selection, from deleting again.
		ClearPointSelection();
	}

	bool ProceduralEditMode::ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const
	{
		const Vector4 clip = m_overlayViewProj * Vector4(worldPosition, 1.0f);
		if (clip.w <= 0.0f)
		{
			return false;
		}

		screenPosition.x = m_overlayViewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * m_overlayViewportSize.x;
		screenPosition.y = m_overlayViewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * m_overlayViewportSize.y;
		return true;
	}

	int32 ProceduralEditMode::HitTestTransformControl(const float screenX, const float screenY) const
	{
		int32 best = -1;
		float bestDistanceSq = TransformHandleRadius * TransformHandleRadius;
		for (size_t i = 0; i < m_controls.size(); ++i)
		{
			const Control& control = m_controls[i];
			ImVec2 screen;
			if (!control.HasTransform() || !ProjectToScreen(control.placed.position, screen))
			{
				continue;
			}

			const float dx = screen.x - screenX;
			const float dy = screen.y - screenY;
			const float distanceSq = dx * dx + dy * dy;
			if (distanceSq <= bestDistanceSq)
			{
				best = static_cast<int32>(i);
				bestDistanceSq = distanceSq;
			}
		}
		return best;
	}

	int32 ProceduralEditMode::GetSelectedRectangle()
	{
		if (m_selected < 0 || m_selected >= static_cast<int32>(m_controls.size()) || !IsPlacedRectangle(m_controls[m_selected]))
		{
			return -1;
		}
		return m_selected;
	}

	bool ProceduralEditMode::ProjectRectangleHandle(const pcg::graph::ControlInstance& instance, const pcg::graph::RectangleHandle handle, ImVec2& screen) const
	{
		Vector3 position = pcg::graph::RectangleHandlePosition(instance, handle);
		position.y = GetHandleHeight(position.x, position.z);
		return ProjectToScreen(position, screen);
	}

	bool ProceduralEditMode::HitTestRectangleHandle(const float screenX, const float screenY, pcg::graph::RectangleHandle& handle)
	{
		const int32 index = GetSelectedRectangle();
		if (index < 0)
		{
			return false;
		}

		const pcg::graph::ControlInstance& instance = m_controls[index].placed;
		bool found = false;
		float bestDistanceSq = TransformHandleRadius * TransformHandleRadius;
		for (const pcg::graph::RectangleHandle candidate : RectangleHandles)
		{
			ImVec2 screen;
			if (!ProjectRectangleHandle(instance, candidate, screen))
			{
				continue;
			}

			const float dx = screen.x - screenX;
			const float dy = screen.y - screenY;
			const float distanceSq = dx * dx + dy * dy;
			if (distanceSq < bestDistanceSq || (!found && distanceSq <= bestDistanceSq))
			{
				handle = candidate;
				bestDistanceSq = distanceSq;
				found = true;
			}
		}
		return found;
	}

	void ProceduralEditMode::DrawPlacedTransforms(ImDrawList& drawList)
	{
		for (size_t i = 0; i < m_controls.size(); ++i)
		{
			const Control& control = m_controls[i];
			if (!control.HasTransform())
			{
				continue;
			}

			const pcg::graph::ControlInstance& instance = control.placed;
			const bool selected = static_cast<int32>(i) == m_selected;
			const bool missingType = m_controlLibrary.Find(instance.typePath) == nullptr;
			const ImU32 color = missingType ? IM_COL32(255, 90, 80, 220) : (selected ? IM_COL32(190, 255, 160, 255) : IM_COL32(130, 230, 110, 200));
			const float yaw = SelectedControlTransform::ExtractYaw(instance.rotation);
			const Quaternion rotation = SelectedControlTransform::FromYaw(yaw);

			if (IsPlacedRectangle(control))
			{
				// The rectangle's outline on the ground (like a loop), its sides split so the line follows the terrain.
				const std::array<SplinePoint, 4> corners = pcg::graph::RectangleCorners(instance);
				for (size_t side = 0; side < corners.size(); ++side)
				{
					const SplinePoint& from = corners[side];
					const SplinePoint& to = corners[(side + 1) % corners.size()];
					const float length = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.z - from.z) * (to.z - from.z));
					const int32 segments = std::clamp(static_cast<int32>(std::ceil(length / RectangleOutlineStep)), 1, RectangleOutlineMaxSegments);
					ImVec2 previous;
					bool previousValid = false;
					for (int32 s = 0; s <= segments; ++s)
					{
						const float t = static_cast<float>(s) / static_cast<float>(segments);
						const float px = from.x + (to.x - from.x) * t;
						const float pz = from.z + (to.z - from.z) * t;
						ImVec2 current;
						const bool currentValid = ProjectToScreen(Vector3(px, GetHandleHeight(px, pz), pz), current);
						if (previousValid && currentValid)
						{
							drawList.AddLine(previous, current, color, selected ? 2.0f : 1.0f);
						}
						previous = current;
						previousValid = currentValid;
					}
				}

				// The selected rectangle's side and corner handles.
				if (selected)
				{
					for (const pcg::graph::RectangleHandle handle : RectangleHandles)
					{
						ImVec2 screen;
						if (!ProjectRectangleHandle(instance, handle, screen))
						{
							continue;
						}

						const bool dragged = m_rectangleDrag.active && m_rectangleDrag.controlId == instance.id && m_rectangleDrag.handle == handle;
						// Corners are squares, sides are circles.
						const float half = TransformHandleRadius * 0.5f;
						const ImU32 fill = dragged ? IM_COL32(255, 255, 255, 255) : color;
						const bool corner = handle == pcg::graph::RectangleHandle::CornerNegNeg || handle == pcg::graph::RectangleHandle::CornerPosNeg
							|| handle == pcg::graph::RectangleHandle::CornerPosPos || handle == pcg::graph::RectangleHandle::CornerNegPos;
						if (corner)
						{
							drawList.AddRectFilled(ImVec2(screen.x - half, screen.y - half), ImVec2(screen.x + half, screen.y + half), fill);
							drawList.AddRect(ImVec2(screen.x - half, screen.y - half), ImVec2(screen.x + half, screen.y + half), IM_COL32(20, 20, 20, 255));
						}
						else
						{
							drawList.AddCircleFilled(screen, half, fill);
							drawList.AddCircle(screen, half, IM_COL32(20, 20, 20, 255));
						}
					}
				}
			}
			// The volume's box: rotated around up, half extents around the position.
			else if (IsPlacedVolume(control))
			{
				const Vector3& e = instance.halfExtents;
				Vector3 corners[8];
				for (int c = 0; c < 8; ++c)
				{
					const Vector3 local((c & 1) ? e.x : -e.x, (c & 2) ? e.y : -e.y, (c & 4) ? e.z : -e.z);
					corners[c] = instance.position + rotation * local;
				}

				static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
				for (const auto& edge : edges)
				{
					ImVec2 a, b;
					if (ProjectToScreen(corners[edge[0]], a) && ProjectToScreen(corners[edge[1]], b))
					{
						drawList.AddLine(a, b, color, selected ? 2.0f : 1.0f);
					}
				}
			}

			// Centre marker with a yaw arrow (+X of the control), and the name.
			ImVec2 centre;
			if (!ProjectToScreen(instance.position, centre))
			{
				continue;
			}

			ImVec2 arrowTip;
			const float arrowLength = IsPlacedVolume(control) ? std::max(instance.halfExtents.x, 2.0f) : 4.0f;
			if (ProjectToScreen(instance.position + rotation * Vector3(arrowLength, 0.0f, 0.0f), arrowTip))
			{
				drawList.AddLine(centre, arrowTip, color, 2.0f);
			}
			drawList.AddCircleFilled(centre, TransformHandleRadius * 0.7f, color);
			drawList.AddCircle(centre, TransformHandleRadius * 0.7f, IM_COL32(20, 20, 20, 255));
			drawList.AddText(ImVec2(centre.x + TransformHandleRadius, centre.y - TransformHandleRadius * 1.5f), IM_COL32(255, 255, 255, 230), control.Name().c_str());
		}
	}

	void ProceduralEditMode::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		if (!drawList)
		{
			return;
		}

		m_overlayViewProj = m_camera.GetProjectionMatrix() * m_camera.GetViewMatrix();
		m_overlayViewportMin = viewportMin;
		m_overlayViewportSize = viewportSize;

		const ImGuiIO& io = ImGui::GetIO();
		// Only while the cursor is over the viewport, so the key cannot also act on something the user
		// is editing elsewhere (the viewport itself deletes the selected entities on the same key).
		if (!io.WantTextInput && ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			DeleteSelectedPoint();
		}

		// Records from before heights existed get their points put on the ground once it is loaded
		// (attempts spaced out, then the fallback).
		++m_overlayFrame;
		for (Control& control : m_controls)
		{
			EnsureHeights(control, true);
		}

		// The selection must not outlive its point (e.g. after a control was deleted).
		if (m_pointSelectable && !m_pointSelectable->IsValid())
		{
			ClearPointSelection();
		}
		if (m_transformSelectable && !m_transformSelectable->IsValid())
		{
			ClearTransformSelection();
		}

		drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

		const auto project = [this](const Vector3& world, ImVec2& screen) { return ProjectToScreen(world, screen); };
		const auto height = [this](const float x, const float z) { return GetHandleHeight(x, z); };

		// Influence rects, faintly: the last built ones, else (oases) the area the loop's bounds would reach.
		for (size_t i = 0; i < m_controls.size(); ++i)
		{
			const Control& control = m_controls[i];
			const bool selected = static_cast<int32>(i) == m_selected;
			if (control.kind == ControlKind::Oasis)
			{
				const pcg::OasisRecord& record = control.oasis;
				pcg::WorldRect rect = record.content.influence;
				if (record.content.IsEmpty())
				{
					ComputeBounds(record.loop, rect);
					const float margin = record.params.outerFalloff + 2.0f;
					rect.minX -= margin;
					rect.minZ -= margin;
					rect.maxX += margin;
					rect.maxZ += margin;
				}

				DrawRect(*drawList, rect, selected ? IM_COL32(120, 200, 255, 90) : IM_COL32(120, 200, 255, 45), project, height);
			}
			else if (control.kind == ControlKind::SectionPath && !control.path.content.IsEmpty())
			{
				const ImU32 color = selected ? IM_COL32(255, 190, 110, 90) : IM_COL32(255, 190, 110, 45);
				if (control.path.builtRects.empty())
				{
					DrawRect(*drawList, control.path.content.influence, color, project, height);
				}
				for (const pcg::WorldRect& rect : control.path.builtRects)
				{
					DrawRect(*drawList, rect, color, project, height);
				}
			}
		}

		std::vector<const PathSpline*> splines;
		std::vector<SplineEditController::Style> styles;
		for (const Control& control : m_controls)
		{
			splines.push_back(&control.Spline());

			SplineEditController::Style style;
			if (control.kind == ControlKind::Oasis)
			{
				style.lineColor = IM_COL32(80, 200, 255, 200);
				style.selectedColor = IM_COL32(120, 240, 255, 255);
			}
			else if (control.kind == ControlKind::Placed)
			{
				style.lineColor = IM_COL32(130, 230, 110, 200);
				style.selectedColor = IM_COL32(190, 255, 160, 255);
			}
			else if (control.kind == ControlKind::GraphInstance)
			{
				style.lineColor = IM_COL32(200, 130, 255, 200);
				style.selectedColor = IM_COL32(230, 180, 255, 255);
			}
			else
			{
				style.lineColor = IM_COL32(255, 170, 70, 200);
				style.selectedColor = IM_COL32(255, 210, 120, 255);
			}
			style.pointColor = IM_COL32(255, 255, 255, 255);
			style.drawWidthRibbon = false;
			style.drawDirection = false;
			style.label = control.Name();
			styles.push_back(std::move(style));
		}
		m_splineEditor.Draw(*drawList, viewportMin, viewportSize, splines, styles);
		DrawPlacedTransforms(*drawList);

		// The corner piece chosen at each control point of the selected section path (laid like a build, flat ground).
		if (m_selected >= 0 && m_selected < static_cast<int32>(m_controls.size()) && m_controls[m_selected].kind == ControlKind::SectionPath)
		{
			const Control& control = m_controls[m_selected];
			const CachedSectionAsset& cached = GetCachedSectionAsset(control.path.sectionAsset);
			pcg::SectionPathShape shape;
			String error;
			if (cached.valid && control.path.spline.points.size() >= GetMinPoints(control) && PrepareSectionPathProbe(control.path, cached.asset, shape, error))
			{
				const std::vector<SplinePoint>& points = control.path.spline.points;
				for (size_t i = 0; i < points.size() && i < shape.layout.pointRoles.size(); ++i)
				{
					const pcg::SectionVariantRole role = shape.layout.pointRoles[i];
					ImVec2 screen;
					if (role == pcg::SectionVariantRole::Straight || !ProjectToScreen(Vector3(points[i].x, points[i].y, points[i].z), screen))
					{
						continue;
					}

					// A small diamond above the point handle, lettered I (inner) or O (outer).
					const bool inner = role == pcg::SectionVariantRole::InnerCorner;
					const ImU32 color = inner ? IM_COL32(110, 230, 120, 230) : IM_COL32(235, 110, 235, 230);
					const ImVec2 centre(screen.x, screen.y - 18.0f);
					constexpr float size = 7.0f;
					drawList->AddQuadFilled(ImVec2(centre.x, centre.y - size), ImVec2(centre.x + size, centre.y), ImVec2(centre.x, centre.y + size),
						ImVec2(centre.x - size, centre.y), color);
					const char* letter = inner ? "I" : "O";
					const ImVec2 textSize = ImGui::CalcTextSize(letter);
					drawList->AddText(ImVec2(centre.x - textSize.x * 0.5f, centre.y - textSize.y * 0.5f), IM_COL32(20, 20, 20, 255), letter);
				}
			}
		}

		drawList->PopClipRect();
	}
}
