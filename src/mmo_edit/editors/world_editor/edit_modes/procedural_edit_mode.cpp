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
#include "pcg/oasis_recipe.h"
#include "pcg/section_path.h"
#include "pcg/terrain_contribution.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

#include <algorithm>
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

	const char* ProceduralEditMode::GetName() const
	{
		static const char* s_name = "Procedural";
		return s_name;
	}

	String ProceduralEditMode::MakeFileName(const std::filesystem::path& folder, const uint64 id)
	{
		return ToAssetPath(folder) + "/" + FormatId(id) + RecordExtension;
	}

	uint32 ProceduralEditMode::GetMinPoints(const Control& control)
	{
		return (control.kind == ControlKind::Oasis || control.path.spline.closed) ? MinLoopPoints : MinPathPoints;
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
		m_selected = index;
		m_splineEditor.CancelDrag();
		ClearPointSelection();
		m_splineEditor.Select(index, -1);
	}

	void ProceduralEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();
		m_splineEditor.CancelDrag();
		// A point selection is meaningless outside this mode (no handles, no overlay).
		ClearPointSelection();
		m_brushPositionValid = false;
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
			m_controls[index].dirty = true;
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
			SetStatus(String(control.kind == ControlKind::Oasis ? "An oasis loop" : "A section path") + " keeps at least "
				+ std::to_string(minPoints) + " points.", StatusLevel::Error);
			return false;
		}

		points.erase(points.begin() + static_cast<std::ptrdiff_t>(pointIndex));
		if (control.kind == ControlKind::SectionPath && pointIndex < control.path.pointIds.size())
		{
			// The id leaves with its point and is never handed out again.
			control.path.pointIds.erase(control.path.pointIds.begin() + static_cast<std::ptrdiff_t>(pointIndex));
		}
		control.dirty = true;
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
		ClearPointSelection();
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

			default:
				ELOG("Procedural record " << fileName << " is of an unknown kind, skipping it");
				continue;
			}

			m_controls.push_back(std::move(control));
		}

		std::stable_sort(m_controls.begin(), m_controls.end(), [](const Control& a, const Control& b) { return a.Id() < b.Id(); });

		DLOG("Loaded " << m_controls.size() << " procedural record(s) from " << folder);
	}

	bool ProceduralEditMode::Save(const std::filesystem::path& pcgFolder)
	{
		bool success = true;

		bool deletionsFailed = false;
		for (const uint64 id : m_deletedIds)
		{
			const String fileName = MakeFileName(pcgFolder, id);
			if (AssetRegistry::HasFile(fileName) && !AssetRegistry::RemoveFile(fileName))
			{
				ELOG("Failed to remove procedural record " << fileName);
				deletionsFailed = true;
				success = false;
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
			const bool written = control.kind == ControlKind::Oasis
				? pcg::WriteOasisRecord(writer, control.oasis)
				: pcg::WriteSectionPathRecord(writer, control.path);
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
		control.dirty = true;
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
		control.dirty = true;
		pcg::SectionPathRecord& record = control.path;
		record.id = m_worldEditor.GenerateEntityUniqueId();
		record.name = name.empty() ? String(closed ? "Section Loop " : "Section Path ") + std::to_string(m_controls.size() - GetOasisCount() + 1) : name;
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

	std::vector<pcg::WorldRect> ProceduralEditMode::GetBuiltRects(const Control& control)
	{
		if (control.Content().IsEmpty())
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
		const int32 index = FindControl(controlId);
		if (index < 0)
		{
			error = "No procedural control with id " + FormatId(controlId);
			return false;
		}

		Control& control = m_controls[index];
		return control.kind == ControlKind::Oasis ? RebuildOasisControl(control, error) : RebuildSectionPathControl(control, error);
	}

	bool ProceduralEditMode::RebuildOasisControl(Control& control, String& error)
	{
		if (!CheckRecipe(error))
		{
			return false;
		}

		pcg::OasisRecord& record = control.oasis;

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

		control.dirty = true;
		SetStatus(DescribeReport(report, "Rebuilt '" + record.name + "'"), StatusLevel::Ok);
		ILOG(m_status);
		return true;
	}

	bool ProceduralEditMode::RebuildSectionPathControl(Control& control, String& error)
	{
		pcg::SectionPathRecord& record = control.path;

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
		control.dirty = true;
		warnings.insert(warnings.end(), report.warnings.begin(), report.warnings.end());

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

		std::vector<uint64> ids;
		for (const Control& control : m_controls)
		{
			ids.push_back(control.Id());
		}

		uint32 rebuilt = 0;
		bool warned = false;
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
		const int32 index = FindControl(id);
		if (index < 0)
		{
			error = "No procedural control with id " + FormatId(id);
			return false;
		}

		Control& control = m_controls[index];
		const OasisBuildReport report = control.kind == ControlKind::Oasis
			? DeleteOasisContent(m_worldEditor, control.oasis)
			: DeleteSectionPathContent(m_worldEditor, control.path);
		if (!report.ok)
		{
			error = "Delete of '" + control.Name() + "' failed: " + report.error;
			SetStatus(error, StatusLevel::Error);
			return false;
		}

		SetStatus(DescribeReport(report, "Deleted '" + control.Name() + "'"), StatusLevel::Ok);
		ILOG(m_status);

		m_deletedIds.push_back(id);
		m_controls.erase(m_controls.begin() + index);
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
		ImGui::BeginDisabled(m_controls.empty());
		if (ImGui::Button("Rebuild All"))
		{
			String error;
			RebuildAll(error);
		}
		ImGui::EndDisabled();

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
				const String label = control.Name() + (control.dirty ? " *" : "");
				if (ImGui::Selectable(label.c_str(), static_cast<int32>(i) == m_selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					SelectControl(static_cast<int32>(i));
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(control.kind == ControlKind::Oasis ? "Oasis" : "Section Path");
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

		if (ImGui::Button("Rebuild"))
		{
			String error;
			Rebuild(m_controls[m_selected].Id(), error);
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
		{
			m_pendingDeleteId = m_controls[m_selected].Id();
			ImGui::OpenPopup("Delete Control?");
		}

		if (ImGui::BeginPopupModal("Delete Control?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const int32 pending = FindControl(m_pendingDeleteId);
			ImGui::Text("Remove '%s' and everything it generated\n(terrain changes, paint, water, objects)?",
				pending >= 0 ? m_controls[pending].Name().c_str() : "?");
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
			current.dirty = true;
		}

		uint64& seedRef = current.kind == ControlKind::Oasis ? current.oasis.seed : current.path.params.seed;
		uint64 seed = seedRef;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed))
		{
			seedRef = seed;
			current.dirty = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
		{
			seedRef = RandomSeed();
			current.dirty = true;
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
			control.dirty = true;
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
				control.dirty = true;
			}
		}
	}

	void ProceduralEditMode::DrawSectionPathDetails(Control& control)
	{
		pcg::SectionPathRecord& rec = control.path;

		if (DrawSectionAssetCombo("Section", rec.sectionAsset))
		{
			control.dirty = true;
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
			control.dirty = true;
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
			const char* heightModes[] = { "Follow spline", "Follow ground", "Smoothed ground" };
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
					"large shape of the hand-made ground, levels its bumps and steps).");
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
				control.dirty = true;
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
				control.dirty = true;
				NotifyPointRotated();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
		}

		const bool anyManual = std::any_of(loop.points.begin(), loop.points.end(), [](const SplinePoint& p) { return !p.autoTangents; });
		ImGui::BeginDisabled(!anyManual);
		if (ImGui::Button("Reset all tangents") && SplineEditController::ResetAllTangents(loop))
		{
			control.dirty = true;
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

		// Point counts before the click, to find a point the click inserts (it needs an id on a section path).
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
				if (control.kind == ControlKind::SectionPath && control.Spline().points.size() == pointCounts[m_selected] + 1 &&
					inserted >= 0 && static_cast<size_t>(inserted) <= control.path.pointIds.size())
				{
					// A new point gets a new id; ids are never reused.
					control.path.pointIds.insert(control.path.pointIds.begin() + inserted, control.path.nextPointId++);
				}

				if (m_splineEditor.DragMoved())
				{
					// An inserted point is a change right away.
					control.dirty = true;
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
	}

	void ProceduralEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

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
				m_controls[selected].dirty = true;
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

		if (m_splineEditor.IsDragging())
		{
			const bool moved = m_splineEditor.DragMoved();
			m_splineEditor.OnMouseUp();
			const int32 selected = m_splineEditor.GetSelectedSpline();
			if (moved && selected >= 0 && selected < static_cast<int32>(m_controls.size()))
			{
				m_controls[selected].dirty = true;
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
			SetStatus(String(control.kind == ControlKind::Oasis ? "An oasis loop" : "A section path") + " keeps at least "
				+ std::to_string(minPoints) + " points.", StatusLevel::Error);
			return;
		}

		if (control.kind == ControlKind::SectionPath && static_cast<size_t>(point) < control.path.pointIds.size())
		{
			// The id leaves with its point and is never handed out again.
			control.path.pointIds.erase(control.path.pointIds.begin() + point);
		}

		control.dirty = true;
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
			else if (!control.path.content.IsEmpty())
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
