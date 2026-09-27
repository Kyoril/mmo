// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "atlas_edit_mode.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

#include "log/default_log_levels.h"
#include "math/matrix4.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "proto_data/project.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

namespace mmo
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		/// Place kinds, in the order worldkit/atlas.py lists them.
		constexpr const char* s_poiKinds[] = { "hub", "camp", "ruin", "lair", "landmark", "resource", "dungeon_entrance", "note" };

		/// Maximum distance in pixels between the cursor and a handle for the handle to be picked.
		constexpr float s_pickRadiusPixels = 10.0f;

		/// Number of segments of a place's radius ring.
		constexpr int s_ringSegments = 48;

		const Json& EmptyArray()
		{
			static const Json empty = Json::array();
			return empty;
		}

		/// The atlas stores every coordinate rounded to 0.1 (see worldkit/atlas.py), so an untouched
		/// value survives a load/save cycle byte for byte.
		double Round1(const double value)
		{
			return std::round(value * 10.0) / 10.0;
		}

		ImU32 StatusColor(const Json& entry)
		{
			const String status = entry.value("status", String());
			if (status == "canon")
			{
				return IM_COL32(255, 255, 255, 255);
			}

			if (status == "note")
			{
				return IM_COL32(255, 64, 255, 255);
			}

			return IM_COL32(255, 165, 0, 255);
		}

		bool ReadPoint(const Json& value, float& outX, float& outZ)
		{
			if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
			{
				return false;
			}

			outX = value[0].get<float>();
			outZ = value[1].get<float>();
			return true;
		}

		Json MakePoint(const float x, const float z)
		{
			return Json::array({ Round1(x), Round1(z) });
		}

		bool EditText(Json& entry, const char* key, const char* label, const bool multiline, const bool required)
		{
			String value = entry.contains(key) && entry[key].is_string() ? entry[key].get<String>() : String();
			const bool changed = multiline ? ImGui::InputTextMultiline(label, &value, ImVec2(-1.0f, 60.0f)) : ImGui::InputText(label, &value);
			if (!changed)
			{
				return false;
			}

			if (!value.empty())
			{
				entry[key] = value;
			}
			else if (!required)
			{
				entry.erase(key);
			}

			return true;
		}

		bool EditLevels(Json& entry)
		{
			bool hasBand = entry.contains("levelMin") && entry.contains("levelMax") && entry["levelMin"].is_number() && entry["levelMax"].is_number();
			bool changed = false;
			if (ImGui::Checkbox("Level band", &hasBand))
			{
				changed = true;
				if (hasBand)
				{
					entry["levelMin"] = 1;
					entry["levelMax"] = 1;
				}
				else
				{
					entry.erase("levelMin");
					entry.erase("levelMax");
				}
			}

			if (hasBand)
			{
				int band[2] = { entry["levelMin"].get<int>(), entry["levelMax"].get<int>() };
				if (ImGui::InputInt2("Levels (min, max)", band))
				{
					band[0] = std::max(1, band[0]);
					band[1] = std::max(band[0], band[1]);
					entry["levelMin"] = band[0];
					entry["levelMax"] = band[1];
					changed = true;
				}
			}

			return changed;
		}

		bool DrawConfirmAndStatus(Json& entry)
		{
			const String status = entry.value("status", String());
			ImGui::Text("Status: %s", status.c_str());
			if (entry.contains("ask") && entry["ask"].is_string())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255));
				ImGui::TextWrapped("Question: %s", entry["ask"].get<String>().c_str());
				ImGui::PopStyleColor();
			}

			if (status == "placeholder")
			{
				if (ImGui::Button("Confirm (make canon)"))
				{
					entry["status"] = "canon";
					entry.erase("ask");
					return true;
				}

				ImGui::SameLine();
				ImGui::TextDisabled("Name, position and facts are right.");
			}

			return false;
		}
	}

	AtlasEditMode::AtlasEditMode(IWorldEditor& worldEditor, std::filesystem::path projectPath, std::function<const proto::MapEntry*()> mapEntryProvider)
		: WorldEditMode(worldEditor)
		, m_projectPath(std::move(projectPath))
		, m_mapEntryProvider(std::move(mapEntryProvider))
	{
	}

	const char* AtlasEditMode::GetName() const
	{
		return "Atlas";
	}

	void AtlasEditMode::OnActivate()
	{
		WorldEditMode::OnActivate();

		if (!m_dirty)
		{
			Load();
		}
	}

	std::filesystem::path AtlasEditMode::GetAtlasPath(const uint32 mapId) const
	{
		return (m_projectPath / ".." / ".." / "world" / "atlas" / ("map_" + std::to_string(mapId) + ".json")).lexically_normal();
	}

	bool AtlasEditMode::Load()
	{
		m_atlas = Json();
		m_loadError.clear();
		m_dirty = false;
		m_selected = Handle();
		m_drag = Handle();

		const proto::MapEntry* mapEntry = m_mapEntryProvider ? m_mapEntryProvider() : nullptr;
		if (!mapEntry)
		{
			m_loadError = "Select the map in the World Settings panel first.";
			return false;
		}

		m_loadedMapId = mapEntry->id();
		const std::filesystem::path path = GetAtlasPath(m_loadedMapId);
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			// No atlas yet: start an empty one, written on the first save.
			m_atlas = Json::object();
			m_atlas["map"] = m_loadedMapId;
			m_atlas["version"] = 1;
			m_atlas["zones"] = Json::array();
			m_atlas["pois"] = Json::array();
			m_atlas["roads"] = Json::array();
			return true;
		}

		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		m_atlas = Json::parse(text, nullptr, false);
		if (m_atlas.is_discarded() || !m_atlas.is_object() || !m_atlas.contains("pois") || !m_atlas["pois"].is_array() ||
			!m_atlas.contains("roads") || !m_atlas["roads"].is_array())
		{
			m_atlas = Json();
			m_loadError = "Could not read " + path.string() + ". Fix the file by hand; the editor will not overwrite it.";
			ELOG(m_loadError);
			return false;
		}

		return true;
	}

	bool AtlasEditMode::Save()
	{
		if (!m_loadError.empty() || !m_atlas.is_object())
		{
			return false;
		}

		const std::filesystem::path path = GetAtlasPath(m_loadedMapId);
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			ELOG("Failed to write atlas file " << path.string());
			return false;
		}

		// Same layout as worldkit/atlas.py dumps(): 2-space indent, raw UTF-8, trailing newline.
		file << m_atlas.dump(2) << "\n";
		m_dirty = false;
		ILOG("Saved atlas " << path.string());
		return true;
	}

	bool AtlasEditMode::RaycastTerrain(const float viewportX, const float viewportY, Vector3& outPosition) const
	{
		terrain::Terrain* terrain = m_worldEditor.GetTerrain();
		if (!m_worldEditor.HasTerrain() || !terrain)
		{
			return false;
		}

		// Long enough to span the whole world from any camera distance (see terrain_raycast.h).
		const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(viewportX, viewportY, 100000.0f);
		const auto hit = terrain->RayIntersects(ray);
		if (!hit.first)
		{
			return false;
		}

		outPosition = hit.second.position;
		return true;
	}

	float AtlasEditMode::GroundHeight(const float worldX, const float worldZ) const
	{
		terrain::Terrain* terrain = m_worldEditor.GetTerrain();
		float height = 0.0f;
		if (m_worldEditor.HasTerrain() && terrain && terrain->TryGetSmoothHeightAt(worldX, worldZ, height))
		{
			return height;
		}

		return 0.0f;
	}

	AtlasEditMode::Handle AtlasEditMode::PickHandle(const float viewportX, const float viewportY) const
	{
		const float screenX = m_viewportMinX + viewportX * m_viewportWidth;
		const float screenY = m_viewportMinY + viewportY * m_viewportHeight;

		Handle best;
		float bestDistance = s_pickRadiusPixels;
		for (const ScreenHandle& candidate : m_screenHandles)
		{
			const float distance = std::hypot(candidate.x - screenX, candidate.y - screenY);
			if (distance <= bestDistance)
			{
				bestDistance = distance;
				best = candidate.handle;
			}
		}

		return best;
	}

	void AtlasEditMode::MoveHandle(const Handle& handle, const float worldX, const float worldZ)
	{
		if (handle.type == HandleType::Poi)
		{
			Json& poi = m_atlas["pois"][handle.index];
			float oldX = 0.0f;
			float oldZ = 0.0f;
			if (ReadPoint(poi.contains("center") ? poi["center"] : EmptyArray(), oldX, oldZ) && poi.contains("polygon") && poi["polygon"].is_array())
			{
				for (Json& point : poi["polygon"])
				{
					float px = 0.0f;
					float pz = 0.0f;
					if (ReadPoint(point, px, pz))
					{
						point = MakePoint(px + (worldX - oldX), pz + (worldZ - oldZ));
					}
				}
			}

			poi["center"] = MakePoint(worldX, worldZ);
			poi["source"] = "user";
		}
		else if (handle.type == HandleType::RoadPoint)
		{
			Json& road = m_atlas["roads"][handle.index];
			road["points"][handle.pointIndex] = MakePoint(worldX, worldZ);
			road["source"] = "user";
		}

		m_dirty = true;
	}

	String AtlasEditMode::UniquePoiId(const String& name) const
	{
		String base;
		for (const char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
			{
				base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			else if (!base.empty() && base.back() != '_')
			{
				base += '_';
			}
		}

		while (!base.empty() && base.back() == '_')
		{
			base.pop_back();
		}

		if (base.empty())
		{
			base = "place";
		}

		const Json& pois = m_atlas.contains("pois") ? m_atlas["pois"] : EmptyArray();
		auto taken = [&pois](const String& id)
		{
			return std::any_of(pois.begin(), pois.end(), [&id](const Json& poi)
			{
				return poi.value("id", String()) == id;
			});
		};

		String candidate = base;
		for (int suffix = 2; taken(candidate); ++suffix)
		{
			candidate = base + "_" + std::to_string(suffix);
		}

		return candidate;
	}

	void AtlasEditMode::AddPoi(const float worldX, const float worldZ)
	{
		const String kind = s_poiKinds[m_placeKind];
		const bool isNote = kind == "note";
		const String name = isNote ? "Note" : "New place";

		Json poi = Json::object();
		poi["id"] = UniquePoiId(name);
		poi["name"] = name;
		poi["kind"] = kind;
		poi["center"] = MakePoint(worldX, worldZ);
		poi["radius"] = isNote ? 10.0 : 25.0;

		// A place the user drops is their own decision, so it is canon (or a note) right away.
		poi["status"] = isNote ? "note" : "canon";
		poi["source"] = "user";

		m_atlas["pois"].push_back(std::move(poi));
		m_selected = Handle{ HandleType::Poi, static_cast<int>(m_atlas["pois"].size()) - 1, -1 };
		m_dirty = true;
	}

	void AtlasEditMode::OnMouseDown(const float x, const float y)
	{
		if (!m_atlas.is_object())
		{
			return;
		}

		if (m_placing)
		{
			Vector3 hit;
			if (RaycastTerrain(x, y, hit))
			{
				AddPoi(hit.x, hit.z);
			}

			m_placing = false;
			return;
		}

		const Handle picked = PickHandle(x, y);
		m_selected = picked;
		m_drag = picked;
	}

	void AtlasEditMode::OnMouseMoved(const float x, const float y)
	{
		if (m_drag.type == HandleType::None)
		{
			return;
		}

		Vector3 hit;
		if (RaycastTerrain(x, y, hit))
		{
			MoveHandle(m_drag, hit.x, hit.z);
		}
	}

	void AtlasEditMode::OnMouseUp(float, float)
	{
		m_drag = Handle();
	}

	void AtlasEditMode::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		m_viewportMinX = viewportMin.x;
		m_viewportMinY = viewportMin.y;
		m_viewportWidth = std::max(1.0f, viewportSize.x);
		m_viewportHeight = std::max(1.0f, viewportSize.y);
		m_screenHandles.clear();

		if (!drawList || !m_atlas.is_object())
		{
			return;
		}

		Camera& camera = m_worldEditor.GetCamera();
		const Matrix4 viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		auto project = [&](const float x, const float y, const float z, ImVec2& out) -> bool
		{
			const Vector4 clip = viewProj * Vector4(Vector3(x, y, z), 1.0f);
			if (clip.w <= 0.0f)
			{
				return false;
			}

			out.x = viewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewportSize.x;
			out.y = viewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportSize.y;
			return true;
		};
		auto label = [&](const ImVec2& at, const ImU32 color, const String& text)
		{
			drawList->AddText(ImVec2(at.x + 9.0f, at.y - 7.0f), IM_COL32(0, 0, 0, 255), text.c_str());
			drawList->AddText(ImVec2(at.x + 8.0f, at.y - 8.0f), color, text.c_str());
		};

		drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

		const Json& roads = m_atlas.contains("roads") ? m_atlas["roads"] : EmptyArray();
		for (size_t roadIndex = 0; roadIndex < roads.size(); ++roadIndex)
		{
			const Json& road = roads[roadIndex];
			const bool selected = m_selected.type == HandleType::RoadPoint && m_selected.index == static_cast<int>(roadIndex);
			const ImU32 color = selected ? IM_COL32(255, 255, 0, 255) : StatusColor(road);
			const Json& points = road.contains("points") ? road["points"] : EmptyArray();

			ImVec2 previous;
			bool hasPrevious = false;
			for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
			{
				float x = 0.0f;
				float z = 0.0f;
				ImVec2 screen;
				if (!ReadPoint(points[pointIndex], x, z) || !project(x, GroundHeight(x, z) + 0.5f, z, screen))
				{
					hasPrevious = false;
					continue;
				}

				if (hasPrevious)
				{
					drawList->AddLine(previous, screen, color, 3.0f);
				}

				drawList->AddCircleFilled(screen, 5.0f, color);
				if (pointIndex == 0)
				{
					label(screen, color, road.value("name", String("road")));
				}

				m_screenHandles.push_back({ Handle{ HandleType::RoadPoint, static_cast<int>(roadIndex), static_cast<int>(pointIndex) }, screen.x, screen.y });
				previous = screen;
				hasPrevious = true;
			}
		}

		const Json& pois = m_atlas.contains("pois") ? m_atlas["pois"] : EmptyArray();
		for (size_t poiIndex = 0; poiIndex < pois.size(); ++poiIndex)
		{
			const Json& poi = pois[poiIndex];
			float cx = 0.0f;
			float cz = 0.0f;
			if (!ReadPoint(poi.contains("center") ? poi["center"] : EmptyArray(), cx, cz))
			{
				continue;
			}

			const bool selected = m_selected.type == HandleType::Poi && m_selected.index == static_cast<int>(poiIndex);
			const ImU32 color = selected ? IM_COL32(255, 255, 0, 255) : StatusColor(poi);

			// Outline on the ground: a sampled circle for radius places, the polygon otherwise.
			std::vector<ImVec2> outline;
			if (poi.contains("radius") && poi["radius"].is_number())
			{
				const float radius = poi["radius"].get<float>();
				for (int i = 0; i <= s_ringSegments; ++i)
				{
					const float angle = static_cast<float>(i) / static_cast<float>(s_ringSegments) * 6.2831853f;
					const float x = cx + std::cos(angle) * radius;
					const float z = cz + std::sin(angle) * radius;
					ImVec2 screen;
					if (project(x, GroundHeight(x, z) + 0.3f, z, screen))
					{
						outline.push_back(screen);
					}
				}
			}
			else if (poi.contains("polygon") && poi["polygon"].is_array())
			{
				for (const Json& point : poi["polygon"])
				{
					float x = 0.0f;
					float z = 0.0f;
					ImVec2 screen;
					if (ReadPoint(point, x, z) && project(x, GroundHeight(x, z) + 0.3f, z, screen))
					{
						outline.push_back(screen);
					}
				}

				if (!outline.empty())
				{
					outline.push_back(outline.front());
				}
			}

			if (outline.size() > 1)
			{
				drawList->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_None, selected ? 3.0f : 2.0f);
			}

			ImVec2 pin;
			if (project(cx, GroundHeight(cx, cz) + 2.0f, cz, pin))
			{
				const float pinRadius = selected ? 8.0f : 6.0f;
				drawList->AddCircleFilled(pin, pinRadius, color);
				drawList->AddCircle(pin, pinRadius, IM_COL32(0, 0, 0, 255), 0, 1.5f);

				String text = poi.value("name", String("?"));
				if (poi.contains("ask"))
				{
					text += " (?)";
				}

				label(pin, color, text);
				m_screenHandles.push_back({ Handle{ HandleType::Poi, static_cast<int>(poiIndex), -1 }, pin.x, pin.y });
			}
		}

		drawList->PopClipRect();
	}

	void AtlasEditMode::DrawPoiProperties(Json& poi)
	{
		bool changed = false;
		ImGui::TextDisabled("id: %s", poi.value("id", String()).c_str());
		changed |= EditText(poi, "name", "Name", false, true);

		const String kind = poi.value("kind", String());
		int kindIndex = 0;
		for (int i = 0; i < static_cast<int>(std::size(s_poiKinds)); ++i)
		{
			if (kind == s_poiKinds[i])
			{
				kindIndex = i;
			}
		}

		if (ImGui::Combo("Kind", &kindIndex, s_poiKinds, static_cast<int>(std::size(s_poiKinds))))
		{
			const bool wasNote = kind == "note";
			const bool isNote = String(s_poiKinds[kindIndex]) == "note";
			poi["kind"] = s_poiKinds[kindIndex];

			// Kind 'note' and status 'note' always go together (worldkit/atlas.py validates this).
			if (isNote)
			{
				poi["status"] = "note";
				poi.erase("ask");
			}
			else if (wasNote)
			{
				poi["status"] = "canon";
			}

			changed = true;
		}

		if (poi.contains("radius") && poi["radius"].is_number())
		{
			float radius = poi["radius"].get<float>();
			if (ImGui::DragFloat("Radius (m)", &radius, 0.5f, 1.0f, 2000.0f, "%.1f"))
			{
				poi["radius"] = Round1(radius);
				changed = true;
			}
		}

		changed |= EditLevels(poi);
		changed |= EditText(poi, "faction", "Faction", false, false);
		changed |= EditText(poi, "description", "Description", true, false);
		changed |= EditText(poi, "notes", "Notes", true, false);
		changed |= DrawConfirmAndStatus(poi);

		if (ImGui::Button("Delete place"))
		{
			m_atlas["pois"].erase(static_cast<size_t>(m_selected.index));
			m_selected = Handle();
			changed = true;
		}

		m_dirty |= changed;
	}

	void AtlasEditMode::DrawRoadProperties(Json& road)
	{
		bool changed = false;
		ImGui::TextDisabled("id: %s", road.value("id", String()).c_str());
		changed |= EditText(road, "name", "Name", false, true);

		if (!road.contains("points") || !road["points"].is_array())
		{
			road["points"] = Json::array();
		}

		Json& points = road["points"];
		ImGui::Text("Points: %d (drag them in the viewport)", static_cast<int>(points.size()));

		if (ImGui::Button("Add point at end") && !points.empty())
		{
			float x = 0.0f;
			float z = 0.0f;
			float px = 0.0f;
			float pz = 0.0f;
			ReadPoint(points.back(), x, z);
			if (points.size() > 1 && ReadPoint(points[points.size() - 2], px, pz))
			{
				points.push_back(MakePoint(x + (x - px) * 0.5f, z + (z - pz) * 0.5f));
			}
			else
			{
				points.push_back(MakePoint(x + 20.0f, z));
			}

			changed = true;
		}

		ImGui::SameLine();
		if (ImGui::Button("Remove last point") && points.size() > 2)
		{
			points.erase(points.size() - 1);
			changed = true;
		}

		changed |= EditText(road, "notes", "Notes", true, false);
		changed |= DrawConfirmAndStatus(road);

		if (ImGui::Button("Delete road"))
		{
			m_atlas["roads"].erase(static_cast<size_t>(m_selected.index));
			m_selected = Handle();
			changed = true;
		}

		m_dirty |= changed;
	}

	void AtlasEditMode::DrawNeedsInput()
	{
		int open = 0;
		for (const char* section : { "pois", "roads", "zones" })
		{
			if (!m_atlas.contains(section) || !m_atlas[section].is_array())
			{
				continue;
			}

			const Json& entries = m_atlas[section];
			for (size_t i = 0; i < entries.size(); ++i)
			{
				const Json& entry = entries[i];
				if (entry.value("status", String()) != "placeholder" || !entry.contains("ask"))
				{
					continue;
				}

				++open;
				ImGui::PushID(section);
				ImGui::PushID(static_cast<int>(i));

				const String caption = entry.value("name", String("?"));
				if (ImGui::Selectable(caption.c_str()))
				{
					const String sectionName = section;
					float x = 0.0f;
					float z = 0.0f;
					if (sectionName == "pois" && ReadPoint(entry.contains("center") ? entry["center"] : EmptyArray(), x, z))
					{
						m_selected = Handle{ HandleType::Poi, static_cast<int>(i), -1 };
						m_worldEditor.FocusWorldPosition(Vector3(x, GroundHeight(x, z), z));
					}
					else if (sectionName == "roads" && entry.contains("points") && !entry["points"].empty() && ReadPoint(entry["points"][0], x, z))
					{
						m_selected = Handle{ HandleType::RoadPoint, static_cast<int>(i), 0 };
						m_worldEditor.FocusWorldPosition(Vector3(x, GroundHeight(x, z), z));
					}
				}

				if (ImGui::IsItemHovered() && entry["ask"].is_string())
				{
					ImGui::SetTooltip("%s", entry["ask"].get<String>().c_str());
				}

				ImGui::PopID();
				ImGui::PopID();
			}
		}

		if (open == 0)
		{
			ImGui::TextDisabled("Nothing open - every placeholder has been answered.");
		}
	}

	void AtlasEditMode::DrawDetails()
	{
		ImGui::PushID("AtlasEditMode");

		const proto::MapEntry* mapEntry = m_mapEntryProvider ? m_mapEntryProvider() : nullptr;
		if (mapEntry && mapEntry->id() != m_loadedMapId && !m_dirty)
		{
			Load();
		}

		if (!m_loadError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
			ImGui::TextWrapped("%s", m_loadError.c_str());
			ImGui::PopStyleColor();
			if (ImGui::Button("Reload"))
			{
				Load();
			}

			ImGui::PopID();
			return;
		}

		if (!m_atlas.is_object())
		{
			ImGui::TextDisabled("No atlas loaded.");
			ImGui::PopID();
			return;
		}

		ImGui::TextWrapped("%s", GetAtlasPath(m_loadedMapId).string().c_str());
		ImGui::TextDisabled("Editor-only data; never exported to the game.");
		if (ImGui::Button(m_dirty ? "Save atlas *" : "Save atlas"))
		{
			Save();
		}

		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			Load();
		}

		if (ImGui::CollapsingHeader("Needs your input", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawNeedsInput();
		}

		if (ImGui::CollapsingHeader("Add place", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Combo("Kind##add", &m_placeKind, s_poiKinds, static_cast<int>(std::size(s_poiKinds)));
			if (ImGui::Button(m_placing ? "Click the terrain..." : "Place on terrain"))
			{
				m_placing = !m_placing;
			}

			ImGui::TextDisabled("Pick 'note' to leave a design note for the agent.");
		}

		if (ImGui::CollapsingHeader("Places"))
		{
			Json& pois = m_atlas["pois"];
			for (size_t i = 0; i < pois.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				const bool selected = m_selected.type == HandleType::Poi && m_selected.index == static_cast<int>(i);
				if (ImGui::Selectable(pois[i].value("name", String("?")).c_str(), selected))
				{
					m_selected = Handle{ HandleType::Poi, static_cast<int>(i), -1 };
				}

				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Roads"))
		{
			Json& roads = m_atlas["roads"];
			for (size_t i = 0; i < roads.size(); ++i)
			{
				ImGui::PushID(10000 + static_cast<int>(i));
				const bool selected = m_selected.type == HandleType::RoadPoint && m_selected.index == static_cast<int>(i);
				if (ImGui::Selectable(roads[i].value("name", String("?")).c_str(), selected))
				{
					m_selected = Handle{ HandleType::RoadPoint, static_cast<int>(i), 0 };
				}

				ImGui::PopID();
			}
		}

		if (m_selected.type == HandleType::Poi && m_selected.index >= 0 && m_selected.index < static_cast<int>(m_atlas["pois"].size()))
		{
			if (ImGui::CollapsingHeader("Selected place", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawPoiProperties(m_atlas["pois"][m_selected.index]);
			}
		}
		else if (m_selected.type == HandleType::RoadPoint && m_selected.index >= 0 && m_selected.index < static_cast<int>(m_atlas["roads"].size()))
		{
			if (ImGui::CollapsingHeader("Selected road", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawRoadProperties(m_atlas["roads"][m_selected.index]);
			}
		}

		ImGui::PopID();
	}
}
