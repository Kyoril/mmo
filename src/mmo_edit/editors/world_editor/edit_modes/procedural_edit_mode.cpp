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
#include "pcg/terrain_contribution.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <random>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Asset path of the shared recipe.
		const char* const RecipeFileName = "Editor/Pcg/OasisRecipe.json";

		/// Extension of oasis record files.
		const char* const RecordExtension = ".hpcg";

		constexpr float PiValue = 3.14159265358979f;
		constexpr float DefaultRadius = 40.0f;
		constexpr uint32 DefaultPointCount = 8;

		/// An oasis keeps at least this many loop points.
		constexpr uint32 MinLoopPoints = 3;

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
			{ "Water inset", &pcg::OasisParams::waterInset, 0.1f, 0.0f, 100.0f },
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
			{ "Rock sink", &pcg::OasisParams::rockSink, 0.05f, 0.0f, 10.0f },
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

	int32 ProceduralEditMode::FindOasis(const uint64 id) const
	{
		for (size_t i = 0; i < m_oases.size(); ++i)
		{
			if (m_oases[i].record.id == id)
			{
				return static_cast<int32>(i);
			}
		}

		return -1;
	}

	void ProceduralEditMode::SelectOasis(const int32 index)
	{
		m_selected = index;
		m_splineEditor.CancelDrag();
		m_splineEditor.Select(index, -1);
	}

	void ProceduralEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();
		m_splineEditor.CancelDrag();
		m_brushPositionValid = false;
	}

	bool ProceduralEditMode::HasUnsavedChanges() const
	{
		if (!m_deletedIds.empty())
		{
			return true;
		}

		return std::any_of(m_oases.begin(), m_oases.end(), [](const Oasis& oasis) { return oasis.dirty; });
	}

	void ProceduralEditMode::Load(const std::filesystem::path& pcgFolder)
	{
		m_oases.clear();
		m_deletedIds.clear();
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
				WLOG("Cannot open oasis record " << fileName);
				continue;
			}

			io::StreamSource source{ *file };
			io::Reader reader{ source };
			Oasis oasis;
			if (!pcg::ReadOasisRecord(reader, oasis.record))
			{
				ELOG("Oasis record " << fileName << " is corrupt or from an unsupported version, skipping it");
				continue;
			}

			m_oases.push_back(std::move(oasis));
		}

		std::stable_sort(m_oases.begin(), m_oases.end(), [](const Oasis& a, const Oasis& b) { return a.record.id < b.record.id; });
		DLOG("Loaded " << m_oases.size() << " oasis record(s) from " << folder);
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
				ELOG("Failed to remove oasis record " << fileName);
				deletionsFailed = true;
				success = false;
			}
		}

		if (!deletionsFailed)
		{
			m_deletedIds.clear();
		}

		for (Oasis& oasis : m_oases)
		{
			if (!oasis.dirty)
			{
				continue;
			}

			const String fileName = MakeFileName(pcgFolder, oasis.record.id);
			const std::unique_ptr<std::ostream> file = AssetRegistry::CreateNewFile(fileName);
			if (!file)
			{
				ELOG("Failed to write oasis record " << fileName);
				success = false;
				continue;
			}

			io::StreamSink sink{ *file };
			io::Writer writer{ sink };
			if (!pcg::WriteOasisRecord(writer, oasis.record))
			{
				ELOG("Failed to serialize oasis record " << fileName);
				success = false;
				continue;
			}
			sink.Flush();
			oasis.dirty = false;
		}

		return success;
	}

	void ProceduralEditMode::CreateOasisAtViewportCentre()
	{
		if (!m_worldEditor.HasTerrain())
		{
			m_status = "This world has no terrain.";
			m_statusIsError = true;
			return;
		}

		// Centre of the viewport, down onto the ground.
		const Ray ray = m_camera.GetCameraToViewportRay(0.5f, 0.5f, 50000.0f);
		const auto hit = m_worldEditor.GetTerrain()->RayIntersects(ray);
		if (!hit.first)
		{
			m_status = "No ground under the viewport centre. Look at the terrain and try again.";
			m_statusIsError = true;
			return;
		}

		String error;
		uint64 id = 0;
		CreateOasisAt(hit.second.position.x, hit.second.position.z, DefaultRadius, 1, "", id, error);
	}

	bool ProceduralEditMode::CreateOasisAt(const float x, const float z, const float radius, const uint64 seed, const String& name, uint64& id, String& error)
	{
		if (!m_worldEditor.HasTerrain())
		{
			error = "This world has no terrain.";
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		if (!(radius > 0.0f))
		{
			error = "The oasis radius must be positive.";
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		Oasis oasis;
		oasis.dirty = true;
		oasis.record.id = m_worldEditor.GenerateEntityUniqueId();
		oasis.record.name = name.empty() ? "Oasis " + std::to_string(m_oases.size() + 1) : name;
		oasis.record.seed = seed;
		oasis.record.params = m_defaultParams;
		oasis.record.loop.closed = true;
		oasis.record.loop.interpolation = SplineInterpolation::CatmullRom;
		for (uint32 i = 0; i < DefaultPointCount; ++i)
		{
			const float angle = 2.0f * PiValue * static_cast<float>(i) / static_cast<float>(DefaultPointCount);
			SplinePoint point;
			point.x = x + radius * std::cos(angle);
			point.z = z + radius * std::sin(angle);
			oasis.record.loop.points.push_back(point);
		}

		id = oasis.record.id;
		m_oases.push_back(std::move(oasis));
		SelectOasis(static_cast<int32>(m_oases.size()) - 1);
		m_status = "Created '" + m_oases.back().record.name + "' (" + FormatId(id) + "). Edit the loop, then Rebuild.";
		m_statusIsError = false;
		ILOG(m_status);
		return true;
	}

	bool ProceduralEditMode::CheckOverlap(const Oasis& oasis, String& error) const
	{
		// The influence of the loop as it is now (does not depend on the heights).
		pcg::OasisShape shape;
		const pcg::OasisError loopError = pcg::PrepareOasis(oasis.record.loop, oasis.record.seed, oasis.record.params,
			[](float, float) { return 0.0f; }, shape);
		if (loopError != pcg::OasisError::None)
		{
			// The rebuild reports the invalid loop itself.
			return true;
		}

		const pcg::WorldRect influence = pcg::GetInfluenceRect(shape);
		for (const Oasis& other : m_oases)
		{
			if (other.record.id == oasis.record.id)
			{
				continue;
			}

			// Only oases that put something into the world can be in the way.
			if (other.record.terrain.IsEmpty() && other.record.placements.empty())
			{
				continue;
			}

			if (pcg::RectsOverlap(influence, other.record.influence))
			{
				error = "Rebuild refused: the influence area of '" + oasis.record.name + "' overlaps the one of '"
					+ other.record.name + "' (" + FormatId(other.record.id) + "). Move the loops apart first.";
				return false;
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
		m_status = error;
		m_statusIsError = true;
		ELOG(error);
		return false;
	}

	bool ProceduralEditMode::Rebuild(const uint64 oasisId, String& error)
	{
		if (!CheckRecipe(error))
		{
			return false;
		}

		const int32 index = FindOasis(oasisId);
		if (index < 0)
		{
			error = "No oasis with id " + FormatId(oasisId);
			return false;
		}

		Oasis& oasis = m_oases[index];
		if (!CheckOverlap(oasis, error))
		{
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		const OasisBuildReport report = RebuildOasis(m_worldEditor, oasis.record, m_assets);
		if (!report.ok)
		{
			error = "Rebuild of '" + oasis.record.name + "' failed: " + report.error;
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		oasis.dirty = true;
		m_status = DescribeReport(report, "Rebuilt '" + oasis.record.name + "'");
		m_statusIsError = false;
		ILOG(m_status);
		return true;
	}

	bool ProceduralEditMode::RebuildAll(String& error)
	{
		error.clear();
		if (!CheckRecipe(error))
		{
			return false;
		}

		std::vector<uint64> ids;
		for (const Oasis& oasis : m_oases)
		{
			ids.push_back(oasis.record.id);
		}

		uint32 rebuilt = 0;
		for (const uint64 id : ids)
		{
			String oasisError;
			if (Rebuild(id, oasisError))
			{
				++rebuilt;
			}
			else
			{
				error += (error.empty() ? "" : "\n") + oasisError;
			}
		}

		if (!error.empty())
		{
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		m_status = "Rebuilt " + std::to_string(rebuilt) + " oasis/oases.";
		m_statusIsError = false;
		return true;
	}

	bool ProceduralEditMode::DeleteOasis(const uint64 id, String& error)
	{
		const int32 index = FindOasis(id);
		if (index < 0)
		{
			error = "No oasis with id " + FormatId(id);
			return false;
		}

		Oasis& oasis = m_oases[index];
		const OasisBuildReport report = DeleteOasisContent(m_worldEditor, oasis.record);
		if (!report.ok)
		{
			error = "Delete of '" + oasis.record.name + "' failed: " + report.error;
			m_status = error;
			m_statusIsError = true;
			return false;
		}

		m_status = DescribeReport(report, "Deleted '" + oasis.record.name + "'");
		m_statusIsError = false;
		ILOG(m_status);

		m_deletedIds.push_back(id);
		m_oases.erase(m_oases.begin() + index);
		SelectOasis(m_oases.empty() ? -1 : std::min(index, static_cast<int32>(m_oases.size()) - 1));
		return true;
	}

	void ProceduralEditMode::DrawDetails()
	{
		ImGui::Separator();
		ImGui::Text("Procedural Oases");

		if (ImGui::Button("New Oasis"))
		{
			CreateOasisAtViewportCentre();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_oases.empty());
		if (ImGui::Button("Rebuild All"))
		{
			String error;
			RebuildAll(error);
		}
		ImGui::EndDisabled();

		if (!m_status.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, m_statusIsError ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
			ImGui::TextWrapped("%s", m_status.c_str());
			ImGui::PopStyleColor();
		}

		if (HasUnsavedChanges())
		{
			ImGui::TextDisabled("Oasis records changed. Save the world to keep them.");
		}

		if (ImGui::BeginListBox("##oases", ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing())))
		{
			for (size_t i = 0; i < m_oases.size(); ++i)
			{
				const Oasis& oasis = m_oases[i];
				ImGui::PushID(static_cast<int>(i));
				const String label = oasis.record.name + (oasis.dirty ? " *" : "") + "  [" + FormatId(oasis.record.id) + "]";
				if (ImGui::Selectable(label.c_str(), static_cast<int32>(i) == m_selected))
				{
					SelectOasis(static_cast<int32>(i));
				}
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		if (m_selected < 0 || m_selected >= static_cast<int32>(m_oases.size()))
		{
			return;
		}

		ImGui::Separator();

		if (ImGui::Button("Rebuild"))
		{
			String error;
			Rebuild(m_oases[m_selected].record.id, error);
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
		{
			m_pendingDeleteId = m_oases[m_selected].record.id;
			ImGui::OpenPopup("Delete Oasis?");
		}

		if (ImGui::BeginPopupModal("Delete Oasis?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const int32 pending = FindOasis(m_pendingDeleteId);
			ImGui::Text("Remove '%s' and everything it generated\n(terrain changes, paint, water, objects)?",
				pending >= 0 ? m_oases[pending].record.name.c_str() : "?");
			if (ImGui::Button("Delete"))
			{
				String error;
				DeleteOasis(m_pendingDeleteId, error);
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
		if (m_selected < 0 || m_selected >= static_cast<int32>(m_oases.size()))
		{
			return;
		}
		Oasis& current = m_oases[m_selected];
		pcg::OasisRecord& rec = current.record;

		ImGui::Text("Id: %s", FormatId(rec.id).c_str());

		char nameBuffer[128];
		std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", rec.name.c_str());
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
		{
			rec.name = nameBuffer;
			current.dirty = true;
		}

		uint64 seed = rec.seed;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed))
		{
			rec.seed = seed;
			current.dirty = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
		{
			// Only picks an input; the generated result stays a pure function of the seed.
			std::random_device device;
			rec.seed = (static_cast<uint64>(device()) << 32) | static_cast<uint64>(device());
			current.dirty = true;
		}

		ImGui::TextWrapped("Loop: %d points. Drag a handle to move it, Alt+click a segment to add a point, Delete or Ctrl+click removes a point (minimum %u).",
			static_cast<int>(rec.loop.points.size()), MinLoopPoints);

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
				current.dirty = true;
			}
		}
	}

	void ProceduralEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
		m_brushPositionValid = true;
	}

	std::vector<PathSpline*> ProceduralEditMode::GetSplines()
	{
		std::vector<PathSpline*> splines;
		splines.reserve(m_oases.size());
		for (Oasis& oasis : m_oases)
		{
			splines.push_back(&oasis.record.loop);
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

		const ImGuiIO& io = ImGui::GetIO();
		auto splines = GetSplines();

		// Ctrl+click on a point deletes it.
		int32 oasisIndex, pointIndex;
		if (io.KeyCtrl && m_splineEditor.HitTestPoint(io.MousePos.x, io.MousePos.y, splines, oasisIndex, pointIndex))
		{
			m_splineEditor.Select(oasisIndex, pointIndex);
			m_selected = oasisIndex;
			DeleteSelectedPoint();
			return;
		}

		const Vector3* worldHit = m_brushPositionValid ? &m_brushPosition : nullptr;
		if (m_splineEditor.OnMouseDown(io.MousePos.x, io.MousePos.y, splines, io.KeyAlt, worldHit))
		{
			m_selected = m_splineEditor.GetSelectedSpline();
			if (m_selected >= 0 && m_selected < static_cast<int32>(m_oases.size()) && io.KeyAlt)
			{
				// An inserted point is a change right away.
				m_oases[m_selected].dirty = true;
			}
		}
	}

	void ProceduralEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		if (!m_brushPositionValid || !m_splineEditor.IsDragging())
		{
			return;
		}

		auto splines = GetSplines();
		if (m_splineEditor.OnMouseMoved(m_brushPosition.x, m_brushPosition.z, splines))
		{
			const int32 selected = m_splineEditor.GetSelectedSpline();
			if (selected >= 0 && selected < static_cast<int32>(m_oases.size()))
			{
				m_oases[selected].dirty = true;
			}
		}
	}

	void ProceduralEditMode::OnMouseMoved(const float x, const float y)
	{
		WorldEditMode::OnMouseMoved(x, y);
		m_brushPositionValid = false;
	}

	void ProceduralEditMode::OnMouseUp(const float x, const float y)
	{
		WorldEditMode::OnMouseUp(x, y);

		if (m_splineEditor.IsDragging())
		{
			const bool moved = m_splineEditor.DragMoved();
			m_splineEditor.OnMouseUp();
			const int32 selected = m_splineEditor.GetSelectedSpline();
			if (moved && selected >= 0 && selected < static_cast<int32>(m_oases.size()))
			{
				m_oases[selected].dirty = true;
			}
		}
	}

	void ProceduralEditMode::DeleteSelectedPoint()
	{
		auto splines = GetSplines();
		const int32 selected = m_splineEditor.GetSelectedSpline();
		if (m_splineEditor.DeleteSelectedPoint(splines, MinLoopPoints) && selected >= 0 && selected < static_cast<int32>(m_oases.size()))
		{
			m_oases[selected].dirty = true;
		}
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
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			DeleteSelectedPoint();
		}

		drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

		// Influence rects, faintly: the last built one, else the area the loop's bounds would reach.
		for (size_t i = 0; i < m_oases.size(); ++i)
		{
			const pcg::OasisRecord& record = m_oases[i].record;
			pcg::WorldRect rect = record.influence;
			if (record.terrain.IsEmpty() && record.placements.empty())
			{
				ComputeBounds(record.loop, rect);
				const float margin = record.params.outerFalloff + 2.0f;
				rect.minX -= margin;
				rect.minZ -= margin;
				rect.maxX += margin;
				rect.maxZ += margin;
			}

			const float corners[4][2] = { { rect.minX, rect.minZ }, { rect.maxX, rect.minZ }, { rect.maxX, rect.maxZ }, { rect.minX, rect.maxZ } };
			ImVec2 screen[4];
			bool visible = true;
			for (int c = 0; c < 4 && visible; ++c)
			{
				visible = ProjectToScreen(Vector3(corners[c][0], GetHandleHeight(corners[c][0], corners[c][1]), corners[c][1]), screen[c]);
			}

			if (visible)
			{
				const ImU32 color = static_cast<int32>(i) == m_selected ? IM_COL32(120, 200, 255, 90) : IM_COL32(120, 200, 255, 45);
				drawList->AddQuad(screen[0], screen[1], screen[2], screen[3], color, 1.0f);
			}
		}

		std::vector<const PathSpline*> splines;
		std::vector<SplineEditController::Style> styles;
		for (const Oasis& oasis : m_oases)
		{
			splines.push_back(&oasis.record.loop);

			SplineEditController::Style style;
			style.lineColor = IM_COL32(80, 200, 255, 200);
			style.selectedColor = IM_COL32(120, 240, 255, 255);
			style.pointColor = IM_COL32(255, 255, 255, 255);
			style.drawWidthRibbon = false;
			style.drawDirection = false;
			style.minPoints = MinLoopPoints;
			style.label = oasis.record.name;
			styles.push_back(std::move(style));
		}
		m_splineEditor.Draw(*drawList, viewportMin, viewportSize, splines, styles);

		drawList->PopClipRect();
	}
}
