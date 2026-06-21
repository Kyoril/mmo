// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "talent_editor_window.h"

#include "editor_imgui_helpers.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr float NodeRadius = 34.0f;

		bool ContainsInsensitive(std::string value, std::string search)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(search.begin(), search.end(), search.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value.find(search) != std::string::npos;
		}

		float SnapTo(const float value, const int step)
		{
			if (step <= 0)
			{
				return value;
			}
			return std::round(value / static_cast<float>(step)) * static_cast<float>(step);
		}
	}

	TalentEditorWindow::TalentEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.talentTabs, name)
		, m_host(host)
	{
		m_visible = false;
		m_hasToolbarButton = false;
	}

	void TalentEditorWindow::OnNewEntry(proto::TalentTabEntry& entry)
	{
		EditorEntryWindowBase::OnNewEntry(entry);
		entry.set_name("New Talent Tab");
		entry.set_class_id(0);
		entry.set_canvas_width(1600);
		entry.set_canvas_height(1200);
		entry.set_initial_zoom(0.75f);
	}

	void TalentEditorWindow::DrawDetailsImpl(proto::TalentTabEntry& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Talent Tab Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("ID: %u", currentEntry.id());
			std::string name = currentEntry.name();
			if (ImGui::InputText("Name", &name))
			{
				currentEntry.set_name(name);
			}

			const auto* currentClass = m_project.classes.getById(currentEntry.class_id());
			const char* className = currentClass ? currentClass->name().c_str() : "Select a class";
			if (ImGui::BeginCombo("Class", className))
			{
				for (const auto& classEntry : m_project.classes.getTemplates().entry())
				{
					const bool selected = classEntry.id() == currentEntry.class_id();
					if (ImGui::Selectable(classEntry.name().c_str(), selected))
					{
						currentEntry.set_class_id(classEntry.id());
					}
				}
				ImGui::EndCombo();
			}

			std::string icon = currentEntry.icon();
			if (ImGui::InputText("Tab icon", &icon))
			{
				currentEntry.set_icon(icon);
			}
			std::string background = currentEntry.background();
			if (ImGui::InputText("Canvas background", &background))
			{
				currentEntry.set_background(background);
			}

			int canvasWidth = static_cast<int>(currentEntry.canvas_width());
			int canvasHeight = static_cast<int>(currentEntry.canvas_height());
			if (ImGui::InputInt("Canvas width", &canvasWidth))
			{
				currentEntry.set_canvas_width(static_cast<uint32>(std::max(400, canvasWidth)));
			}
			if (ImGui::InputInt("Canvas height", &canvasHeight))
			{
				currentEntry.set_canvas_height(static_cast<uint32>(std::max(300, canvasHeight)));
			}
			float initialZoom = currentEntry.initial_zoom();
			if (ImGui::SliderFloat("Initial game zoom", &initialZoom, 0.35f, 1.5f, "%.2fx"))
			{
				currentEntry.set_initial_zoom(initialZoom);
			}
		}

		ImGui::Columns(2, "TalentGraphColumns", true);
		DrawTalentCanvas(currentEntry);
		ImGui::NextColumn();
		if (m_selectedTalentId >= 0)
		{
			if (auto* talent = m_project.talents.getById(static_cast<uint32>(m_selectedTalentId)); talent && talent->tab() == currentEntry.id())
			{
				DrawTalentNodeEditor(*talent);
			}
			else
			{
				m_selectedTalentId = -1;
				ImGui::TextDisabled("Select a talent node.");
			}
		}
		else
		{
			ImGui::TextDisabled("Select a talent node.");
		}
		ImGui::Columns(1);
	}

	ImVec2 TalentEditorWindow::GetTalentPosition(const proto::TalentEntry& talent) const
	{
		if (talent.has_position_x() && talent.has_position_y())
		{
			return ImVec2(static_cast<float>(talent.position_x()), static_cast<float>(talent.position_y()));
		}
		return ImVec2(160.0f + talent.column() * 220.0f, 120.0f + talent.row() * 200.0f);
	}

	TexturePtr TalentEditorWindow::GetTalentIcon(const proto::TalentEntry& talent)
	{
		if (talent.ranks_size() == 0)
		{
			return nullptr;
		}
		const auto* spell = m_project.spells.getById(talent.ranks(0));
		if (!spell || !spell->has_icon() || spell->icon().empty())
		{
			return nullptr;
		}

		const std::string& path = spell->icon();
		if (!m_iconCache.contains(path))
		{
			m_iconCache[path] = TextureManager::Get().CreateOrRetrieve(path);
		}
		return m_iconCache[path];
	}

	std::string TalentEditorWindow::GetTalentDisplayName(const proto::TalentEntry& talent) const
	{
		if (talent.ranks_size() > 0)
		{
			if (const auto* spell = m_project.spells.getById(talent.ranks(0)))
			{
				return "[" + std::to_string(talent.id()) + "] " + spell->name();
			}
		}
		return "Talent " + std::to_string(talent.id());
	}

	void TalentEditorWindow::AddPrerequisite(const uint32 targetTalentId, const uint32 sourceTalentId)
	{
		if (targetTalentId == sourceTalentId)
		{
			return;
		}

		auto* target = m_project.talents.getById(targetTalentId);
		const auto* source = m_project.talents.getById(sourceTalentId);
		if (!target || !source)
		{
			return;
		}

		// Avoid adding the same prerequisite twice.
		for (const auto& prerequisite : target->prerequisites())
		{
			if (prerequisite.talent_id() == sourceTalentId)
			{
				return;
			}
		}

		auto* prerequisite = target->add_prerequisites();
		prerequisite->set_talent_id(sourceTalentId);
		// Require the prerequisite talent to be fully maxed (all of its ranks) by default.
		prerequisite->set_rank(static_cast<uint32>(std::max(1, source->ranks_size())));
	}

	void TalentEditorWindow::DrawTalentCanvas(proto::TalentTabEntry& currentTab)
	{
		ImGui::TextUnformatted("Middle-drag to pan, mouse wheel to zoom, double-click empty space to add, left-drag a node to move it");

		ImGui::Checkbox("Show grid", &m_showGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Snap to grid", &m_snapToGrid);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::InputInt("Grid size", &m_gridSize))
		{
			m_gridSize = std::clamp(m_gridSize, 5, 400);
		}
		ImGui::SameLine();
		if (ImGui::Button("Center"))
		{
			m_canvasPan = ImVec2(20.0f, 20.0f);
			m_canvasZoom = currentTab.initial_zoom();
		}

		// NOTE: We intentionally do NOT submit a full-canvas InvisibleButton here. In ImGui the first
		// overlapping item to be submitted claims the hovered id, which would prevent the per-node
		// buttons below from ever being hovered or clicked. Instead we drive pan/zoom/double-click from
		// IsWindowHovered() so the node buttons remain the only interactive items on the canvas.
		ImGui::BeginChild("TalentGraphCanvas", ImVec2(0.0f, 650.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
		const ImVec2 canvasTopLeft = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(canvasTopLeft, ImVec2(canvasTopLeft.x + canvasSize.x, canvasTopLeft.y + canvasSize.y), true);

		// IsWindowHovered() is false while a node is being dragged (an active item blocks it), so this
		// also keeps pan/zoom from fighting node movement.
		const bool canvasHovered = ImGui::IsWindowHovered();
		if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const ImVec2 before((mouse.x - canvasTopLeft.x - m_canvasPan.x) / m_canvasZoom, (mouse.y - canvasTopLeft.y - m_canvasPan.y) / m_canvasZoom);
			m_canvasZoom = std::clamp(m_canvasZoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.12f : 0.89f), 0.25f, 2.0f);
			m_canvasPan = ImVec2(mouse.x - canvasTopLeft.x - before.x * m_canvasZoom, mouse.y - canvasTopLeft.y - before.y * m_canvasZoom);
		}
		if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
		{
			m_canvasPan.x += ImGui::GetIO().MouseDelta.x;
			m_canvasPan.y += ImGui::GetIO().MouseDelta.y;
		}

		const ImVec2 origin(canvasTopLeft.x + m_canvasPan.x, canvasTopLeft.y + m_canvasPan.y);
		const ImVec2 extent(origin.x + currentTab.canvas_width() * m_canvasZoom, origin.y + currentTab.canvas_height() * m_canvasZoom);
		drawList->AddRectFilled(origin, extent, IM_COL32(22, 27, 34, 255));
		drawList->AddRect(origin, extent, IM_COL32(105, 91, 61, 255), 0.0f, 0, 2.0f);

		if (m_showGrid)
		{
			const float gridStep = m_gridSize * m_canvasZoom;
			if (gridStep >= 5.0f)
			{
				// Highlight every fifth line a little stronger so the spacing is easy to read.
				int lineIndex = 0;
				for (float x = origin.x; x <= extent.x; x += gridStep, ++lineIndex)
				{
					const ImU32 color = (lineIndex % 5 == 0) ? IM_COL32(96, 108, 126, 70) : IM_COL32(80, 90, 105, 35);
					drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, extent.y), color);
				}
				lineIndex = 0;
				for (float y = origin.y; y <= extent.y; y += gridStep, ++lineIndex)
				{
					const ImU32 color = (lineIndex % 5 == 0) ? IM_COL32(96, 108, 126, 70) : IM_COL32(80, 90, 105, 35);
					drawList->AddLine(ImVec2(origin.x, y), ImVec2(extent.x, y), color);
				}
			}
		}

		for (const auto& talent : m_project.talents.getTemplates().entry())
		{
			if (talent.tab() != currentTab.id())
			{
				continue;
			}
			const ImVec2 targetPosition = GetTalentPosition(talent);
			const ImVec2 target(origin.x + targetPosition.x * m_canvasZoom, origin.y + targetPosition.y * m_canvasZoom);
			for (const auto& prerequisite : talent.prerequisites())
			{
				const auto* sourceTalent = m_project.talents.getById(prerequisite.talent_id());
				if (!sourceTalent || sourceTalent->tab() != currentTab.id())
				{
					continue;
				}
				const ImVec2 sourcePosition = GetTalentPosition(*sourceTalent);
				const ImVec2 source(origin.x + sourcePosition.x * m_canvasZoom, origin.y + sourcePosition.y * m_canvasZoom);
				drawList->AddLine(source, target, IM_COL32(76, 181, 199, 210), std::max(2.0f, 5.0f * m_canvasZoom));
			}
		}

		bool nodeHovered = false;
		for (int i = 0; i < m_project.talents.count(); ++i)
		{
			auto* talent = m_project.talents.getTemplates().mutable_entry(i);
			if (talent->tab() != currentTab.id())
			{
				continue;
			}

			const ImVec2 position = GetTalentPosition(*talent);
			const ImVec2 center(origin.x + position.x * m_canvasZoom, origin.y + position.y * m_canvasZoom);
			const float radius = NodeRadius * talent->node_scale() * m_canvasZoom;
			const ImVec2 nodeMin(center.x - radius, center.y - radius);
			const ImVec2 nodeMax(center.x + radius, center.y + radius);
			const float rounding = 6.0f * m_canvasZoom;
			const bool selected = m_selectedTalentId == static_cast<int32>(talent->id());
			drawList->AddRectFilled(ImVec2(nodeMin.x - 4.0f, nodeMin.y - 4.0f), ImVec2(nodeMax.x + 4.0f, nodeMax.y + 4.0f), selected ? IM_COL32(255, 209, 76, 255) : IM_COL32(74, 83, 95, 255), rounding);
			drawList->AddRectFilled(nodeMin, nodeMax, IM_COL32(31, 37, 46, 255), rounding);
			if (TexturePtr icon = GetTalentIcon(*talent))
			{
				drawList->AddImage(icon->GetTextureObject(), ImVec2(nodeMin.x + 3.0f, nodeMin.y + 3.0f), ImVec2(nodeMax.x - 3.0f, nodeMax.y - 3.0f));
			}

			ImGui::SetCursorScreenPos(nodeMin);
			ImGui::InvisibleButton(("TalentNode##" + std::to_string(talent->id())).c_str(), ImVec2(radius * 2.0f, radius * 2.0f));
			nodeHovered = nodeHovered || ImGui::IsItemHovered();
			const bool pickingThisNode = m_pickingPrerequisite;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				if (pickingThisNode && m_selectedTalentId >= 0 && m_selectedTalentId != static_cast<int32>(talent->id()))
				{
					AddPrerequisite(static_cast<uint32>(m_selectedTalentId), talent->id());
					m_pickingPrerequisite = false;
				}
				else if (!pickingThisNode)
				{
					m_selectedTalentId = static_cast<int32>(talent->id());
				}
			}
			// Capture the unsnapped node + mouse origin when the drag starts so that snapping quantises
			// the absolute target position instead of accumulating (and losing) sub-grid deltas each frame.
			// While in prerequisite-pick mode we never start a drag (the click selects the prerequisite).
			if (!pickingThisNode && ImGui::IsItemActivated())
			{
				m_draggingTalentId = static_cast<int32>(talent->id());
				m_dragNodeOrigin = position;
				m_dragMouseOrigin = ImGui::GetMousePos();
			}
			if (m_draggingTalentId == static_cast<int32>(talent->id()) && ImGui::IsItemActive())
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				float newX = m_dragNodeOrigin.x + (mouse.x - m_dragMouseOrigin.x) / m_canvasZoom;
				float newY = m_dragNodeOrigin.y + (mouse.y - m_dragMouseOrigin.y) / m_canvasZoom;
				if (m_snapToGrid)
				{
					newX = SnapTo(newX, m_gridSize);
					newY = SnapTo(newY, m_gridSize);
				}
				talent->set_position_x(static_cast<int32>(std::round(newX)));
				talent->set_position_y(static_cast<int32>(std::round(newY)));
			}
			if (ImGui::IsItemDeactivated() && m_draggingTalentId == static_cast<int32>(talent->id()))
			{
				m_draggingTalentId = -1;
			}
		}

		if (canvasHovered && !nodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			CreateNewTalent(currentTab.id(), ImVec2((mouse.x - origin.x) / m_canvasZoom, (mouse.y - origin.y) / m_canvasZoom));
		}

		drawList->PopClipRect();
		ImGui::EndChild();
	}

	void TalentEditorWindow::DrawTalentNodeEditor(proto::TalentEntry& talent)
	{
		ImGui::Text("Talent %u", talent.id());
		ImGui::Separator();
		int position[2] = { static_cast<int>(GetTalentPosition(talent).x), static_cast<int>(GetTalentPosition(talent).y) };
		if (ImGui::InputInt2("Position", position))
		{
			talent.set_position_x(position[0]);
			talent.set_position_y(position[1]);
		}
		if (ImGui::Button("Snap to grid"))
		{
			talent.set_position_x(static_cast<int32>(std::round(SnapTo(static_cast<float>(position[0]), m_gridSize))));
			talent.set_position_y(static_cast<int32>(std::round(SnapTo(static_cast<float>(position[1]), m_gridSize))));
		}
		float scale = talent.node_scale();
		if (ImGui::SliderFloat("Node scale", &scale, 0.65f, 1.75f, "%.2fx"))
		{
			talent.set_node_scale(scale);
		}
		int requiredPoints = talent.has_required_points() ? static_cast<int>(talent.required_points()) : static_cast<int>(talent.row() * 5);
		if (ImGui::InputInt("Points in tree required", &requiredPoints))
		{
			talent.set_required_points(static_cast<uint32>(std::max(0, requiredPoints)));
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Spell ranks");
		static std::string spellSearch;
		for (int rankIndex = 0; rankIndex < talent.ranks_size(); ++rankIndex)
		{
			ImGui::PushID(rankIndex);
			const uint32 spellId = talent.ranks(rankIndex);
			const auto* currentSpell = m_project.spells.getById(spellId);
			const std::string preview = currentSpell ? "[" + std::to_string(spellId) + "] " + currentSpell->name() : "Select spell";
			if (ImGui::BeginCombo("##RankSpell", preview.c_str()))
			{
				ImGui::InputTextWithHint("##SpellSearch", "Search by name or ID", &spellSearch);
				int shown = 0;
				for (const auto& spell : m_project.spells.getTemplates().entry())
				{
					if (!spellSearch.empty() && !ContainsInsensitive(spell.name(), spellSearch) && std::to_string(spell.id()).find(spellSearch) == std::string::npos)
					{
						continue;
					}
					const std::string label = "[" + std::to_string(spell.id()) + "] " + spell.name();
					if (ImGui::Selectable(label.c_str(), spell.id() == spellId))
					{
						talent.set_ranks(rankIndex, spell.id());
					}
					if (++shown >= 200)
					{
						break;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				for (int moveIndex = rankIndex; moveIndex + 1 < talent.ranks_size(); ++moveIndex)
				{
					talent.set_ranks(moveIndex, talent.ranks(moveIndex + 1));
				}
				talent.mutable_ranks()->RemoveLast();
				--rankIndex;
			}
			ImGui::PopID();
		}
		if (ImGui::Button("Add rank"))
		{
			talent.add_ranks(0);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Prerequisites");
		if (m_pickingPrerequisite)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "Click a talent node on the canvas to add it as a prerequisite.");
		}
		for (int index = 0; index < talent.prerequisites_size(); ++index)
		{
			auto* prerequisite = talent.mutable_prerequisites(index);
			ImGui::PushID(1000 + index);
			const auto* current = m_project.talents.getById(prerequisite->talent_id());
			const std::string preview = current ? GetTalentDisplayName(*current) : "Select talent";
			if (ImGui::BeginCombo("##Prerequisite", preview.c_str()))
			{
				for (const auto& candidate : m_project.talents.getTemplates().entry())
				{
					if (candidate.tab() == talent.tab() && candidate.id() != talent.id())
					{
						const std::string label = GetTalentDisplayName(candidate);
						if (ImGui::Selectable(label.c_str(), candidate.id() == prerequisite->talent_id()))
						{
							prerequisite->set_talent_id(candidate.id());
							// Default to requiring all of the prerequisite talent's ranks.
							prerequisite->set_rank(static_cast<uint32>(std::max(1, candidate.ranks_size())));
						}
					}
				}
				ImGui::EndCombo();
			}
			int rank = static_cast<int>(prerequisite->rank());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(72.0f);
			if (ImGui::InputInt("Required rank", &rank))
			{
				prerequisite->set_rank(static_cast<uint32>(std::max(1, rank)));
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				talent.mutable_prerequisites()->DeleteSubrange(index, 1);
				--index;
			}
			ImGui::PopID();
		}
		if (ImGui::Button("Add prerequisite"))
		{
			auto* prerequisite = talent.add_prerequisites();
			prerequisite->set_talent_id(0);
			prerequisite->set_rank(1);
		}
		ImGui::SameLine();
		if (ImGui::Button(m_pickingPrerequisite ? "Cancel pick" : "Pick on canvas"))
		{
			m_pickingPrerequisite = !m_pickingPrerequisite;
		}

		ImGui::Separator();
		if (ImGui::Button("Delete talent"))
		{
			for (auto& other : *m_project.talents.getTemplates().mutable_entry())
			{
				for (int index = other.prerequisites_size() - 1; index >= 0; --index)
				{
					if (other.prerequisites(index).talent_id() == talent.id())
					{
						other.mutable_prerequisites()->DeleteSubrange(index, 1);
					}
				}
			}
			m_project.talents.remove(talent.id());
			m_selectedTalentId = -1;
		}
	}

	void TalentEditorWindow::CreateNewTalent(const uint32 tabId, const ImVec2& position)
	{
		float px = position.x;
		float py = position.y;
		if (m_snapToGrid)
		{
			px = SnapTo(px, m_gridSize);
			py = SnapTo(py, m_gridSize);
		}

		auto* talent = m_project.talents.add();
		talent->set_tab(tabId);
		talent->set_row(0);
		talent->set_column(0);
		talent->set_position_x(static_cast<int32>(std::round(px)));
		talent->set_position_y(static_cast<int32>(std::round(py)));
		talent->set_required_points(0);
		talent->set_node_scale(1.0f);
		m_selectedTalentId = static_cast<int32>(talent->id());
	}
}
