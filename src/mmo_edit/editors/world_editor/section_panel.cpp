// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_panel.h"

#include "world_editor_instance.h"
#include "pcg/section_bake_job.h"

#include "assets/asset_registry.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "log/default_log_levels.h"
#include "math/vector4.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mmo
{
	namespace
	{
		/// Overlay colours, cycled per variant.
		const ImU32 VariantColors[] = {
			IM_COL32(255, 200, 60, 255),
			IM_COL32(90, 220, 120, 255),
			IM_COL32(110, 170, 255, 255),
			IM_COL32(240, 110, 200, 255),
		};

		ImU32 WithAlpha(const ImU32 color, const uint8 alpha)
		{
			return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
		}

		/// Lowest value the size fields accept, so a definition stays bakeable while dragging.
		constexpr float MinLength = 1.0f;
		constexpr float MinHalfWidth = 0.5f;
	}

	SectionPanel::SectionPanel(WorldEditorInstance& world)
		: m_world(world)
	{
		m_worldPath = world.GetAssetPath().generic_string();
		std::replace(m_worldPath.begin(), m_worldPath.end(), '\\', '/');

		String error;
		m_definitionLoaded = LoadSectionDefinition(m_worldPath, m_definition, error);
		if (!m_definitionLoaded)
		{
			m_status = error;
			m_statusIsError = true;
			ELOG(error);
		}
		else if (m_definition.name != GetSectionName(m_worldPath))
		{
			// The name follows the folder; keep the definition in sync.
			m_definition.name = GetSectionName(m_worldPath);
			Commit();
		}

		RefreshStamp();
		RefreshPlacedAssets();
	}

	void SectionPanel::OnWorldSaved()
	{
		RefreshStamp();
		RefreshPlacedAssets();
	}

	void SectionPanel::Commit()
	{
		for (pcg::PropRule& rule : m_definition.rules)
		{
			pcg::ClampPropRule(rule);
		}

		String error;
		if (!SaveSectionDefinition(m_worldPath, m_definition, error))
		{
			m_status = error;
			m_statusIsError = true;
			return;
		}

		RefreshStamp();
	}

	void SectionPanel::RefreshStamp()
	{
		m_currentStamp = ComputeSectionSourceStamp(m_worldPath);
		m_hasBake = ReadSectionAssetStamp(GetSectionAssetPath(m_worldPath), m_bakedStamp);

		terrain::Terrain* terrain = m_world.GetTerrain();
		GetSectionLayerTextures(terrain ? terrain->GetDefaultMaterial() : nullptr, m_layerTextures);
	}

	void SectionPanel::RefreshPlacedAssets()
	{
		std::vector<pcg::Placement> placements;
		m_world.CollectWorldPlacements(placements);

		m_placedAssets.clear();
		for (const pcg::Placement& placement : placements)
		{
			m_placedAssets.push_back(placement.asset);
		}

		std::sort(m_placedAssets.begin(), m_placedAssets.end());
		m_placedAssets.erase(std::unique(m_placedAssets.begin(), m_placedAssets.end()), m_placedAssets.end());
	}

	void SectionPanel::Bake()
	{
		if (!m_world.Save())
		{
			m_status = "Saving the world failed; nothing was baked";
			m_statusIsError = true;
			return;
		}

		String error;
		if (!BakeSectionWorld(m_world, error))
		{
			m_status = "Bake failed: " + error;
			m_statusIsError = true;
			ELOG("Section bake failed: " << error);
		}
		else
		{
			m_status = "Baked " + GetSectionAssetPath(m_worldPath);
			m_statusIsError = false;
		}

		RefreshStamp();
		RefreshPlacedAssets();
	}

	float SectionPanel::NextFreeVariantSlot() const
	{
		for (uint32 slot = 0;; ++slot)
		{
			const float x = static_cast<float>(slot) * SectionVariantSpacing;
			const bool used = std::any_of(m_definition.variants.begin(), m_definition.variants.end(),
				[x](const pcg::SectionVariantDef& variant)
				{
					return std::fabs(variant.originX - x) < SectionVariantSpacing * 0.5f;
				});
			if (!used)
			{
				return x;
			}
		}
	}

	String SectionPanel::ValidateDefinition() const
	{
		if (m_definition.variants.empty())
		{
			return "The section needs at least one variant.";
		}

		if (m_definition.length <= 0.0f || m_definition.halfWidth <= 0.0f || m_definition.sideFalloff < 0.0f)
		{
			return "Length and half width must be positive, the side falloff at least 0.";
		}

		const float span = 2.0f * (m_definition.halfWidth + m_definition.sideFalloff);
		for (size_t i = 0; i < m_definition.variants.size(); ++i)
		{
			for (size_t j = i + 1; j < m_definition.variants.size(); ++j)
			{
				if (std::fabs(m_definition.variants[i].originX - m_definition.variants[j].originX) < span - 1.0e-4f)
				{
					return "Variants " + std::to_string(i + 1) + " and " + std::to_string(j + 1) + " overlap (keep their originX at least " +
						std::to_string(static_cast<int32>(std::ceil(span))) + " m apart).";
				}
			}
		}

		return String();
	}

	void SectionPanel::Draw(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (!m_definitionLoaded)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_status.c_str());
				ImGui::End();
				return;
			}

			ImGui::TextDisabled("Section world: %s", m_worldPath.c_str());
			ImGui::TextDisabled("Origin = start of the centre line, along = +Z, +X = right.");

			DrawSizeSection();
			DrawVariantSection();
			DrawRuleSection();
			DrawBakeSection();
		}
		ImGui::End();
	}

	void SectionPanel::DrawSizeSection()
	{
		if (!ImGui::CollapsingHeader("Section", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// Read-only: the name is the world's file name (and the baked asset's).
		ImGui::Text("Name: %s", m_definition.name.c_str());

		ImGui::DragFloat("Length (m)", &m_definition.length, 0.1f, MinLength, 500.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Commit();
		}

		ImGui::DragFloat("Half width (m)", &m_definition.halfWidth, 0.05f, MinHalfWidth, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Commit();
		}

		ImGui::DragFloat("Side falloff (m)", &m_definition.sideFalloff, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Commit();
		}
	}

	void SectionPanel::DrawVariantSection()
	{
		if (!ImGui::CollapsingHeader("Variants", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		int32 removeIndex = -1;
		for (size_t i = 0; i < m_definition.variants.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			const ImU32 color = VariantColors[i % std::size(VariantColors)];
			ImGui::ColorButton("##color", ImGui::ColorConvertU32ToFloat4(color), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::Text("Variant %u", static_cast<uint32>(i + 1));
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::DragFloat("originX (m)", &m_definition.variants[i].originX, 0.5f, -10000.0f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				Commit();
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(m_definition.variants.size() <= 1);
			if (ImGui::SmallButton("Remove"))
			{
				removeIndex = static_cast<int32>(i);
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}

		if (removeIndex >= 0)
		{
			m_definition.variants.erase(m_definition.variants.begin() + removeIndex);
			Commit();
		}

		if (ImGui::Button("Add Variant"))
		{
			m_definition.variants.push_back(pcg::SectionVariantDef{ NextFreeVariantSlot() });
			Commit();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Adds a variant at the next free %.0f m slot along X. Build it next to the others; all variants share length and width.", SectionVariantSpacing);
		}

		const String problem = ValidateDefinition();
		if (!problem.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", problem.c_str());
		}
	}

	void SectionPanel::DrawRuleSection()
	{
		if (!ImGui::CollapsingHeader("Prop Rules", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		ImGui::TextDisabled("Per asset, applied to every placement of it when the section is stamped.");

		const auto hasRule = [this](const String& asset)
		{
			return std::any_of(m_definition.rules.begin(), m_definition.rules.end(), [&asset](const pcg::PropRule& rule) { return rule.asset == asset; });
		};

		if (ImGui::BeginCombo("Add rule for", "Pick a placed asset..."))
		{
			for (const String& asset : m_placedAssets)
			{
				if (hasRule(asset))
				{
					continue;
				}

				if (ImGui::Selectable(asset.c_str()))
				{
					pcg::PropRule rule;
					rule.asset = asset;
					m_definition.rules.push_back(rule);
					std::stable_sort(m_definition.rules.begin(), m_definition.rules.end(),
						[](const pcg::PropRule& a, const pcg::PropRule& b) { return a.asset < b.asset; });
					Commit();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh"))
		{
			RefreshPlacedAssets();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Re-reads the assets placed in the section world.");
		}

		m_swapInputs.resize(m_definition.rules.size());

		int32 removeIndex = -1;
		for (size_t i = 0; i < m_definition.rules.size(); ++i)
		{
			pcg::PropRule& rule = m_definition.rules[i];
			ImGui::PushID(static_cast<int>(i));

			const bool placed = std::binary_search(m_placedAssets.begin(), m_placedAssets.end(), rule.asset);
			const String label = rule.asset + (placed ? "" : "  (not placed)");
			if (ImGui::TreeNodeEx("##rule", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str()))
			{
				ImGui::SliderFloat("Keep chance", &rule.keepChance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Commit();
				}

				ImGui::DragFloat("Position jitter (m)", &rule.positionJitter, 0.01f, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Commit();
				}

				ImGui::DragFloat("Yaw jitter (deg)", &rule.yawJitterDeg, 0.5f, 0.0f, 180.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Commit();
				}

				ImGui::DragFloat("Scale jitter (+/-)", &rule.scaleJitter, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Commit();
				}

				ImGui::TextUnformatted("Swap group:");
				int32 removeSwap = -1;
				for (size_t s = 0; s < rule.swapGroup.size(); ++s)
				{
					ImGui::PushID(static_cast<int>(s));
					ImGui::BulletText("%s", rule.swapGroup[s].c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						removeSwap = static_cast<int32>(s);
					}
					ImGui::PopID();
				}

				if (removeSwap >= 0)
				{
					rule.swapGroup.erase(rule.swapGroup.begin() + removeSwap);
					Commit();
				}

				const auto inGroup = [&rule](const String& asset)
				{
					return asset == rule.asset || std::find(rule.swapGroup.begin(), rule.swapGroup.end(), asset) != rule.swapGroup.end();
				};

				if (ImGui::BeginCombo("Add placed asset", "..."))
				{
					for (const String& asset : m_placedAssets)
					{
						if (!inGroup(asset) && ImGui::Selectable(asset.c_str()))
						{
							rule.swapGroup.push_back(asset);
							Commit();
						}
					}
					ImGui::EndCombo();
				}

				ImGui::InputText("##swapPath", &m_swapInputs[i]);
				ImGui::SameLine();
				String& input = m_swapInputs[i];
				std::replace(input.begin(), input.end(), '\\', '/');
				const bool canAdd = !input.empty() && !inGroup(input) && AssetRegistry::HasFile(input);
				ImGui::BeginDisabled(!canAdd);
				if (ImGui::Button("Add path"))
				{
					rule.swapGroup.push_back(input);
					input.clear();
					Commit();
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Adds any existing mesh asset path to the swap group.");
				}

				if (ImGui::Button("Remove rule"))
				{
					removeIndex = static_cast<int32>(i);
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0)
		{
			m_definition.rules.erase(m_definition.rules.begin() + removeIndex);
			m_swapInputs.erase(m_swapInputs.begin() + removeIndex);
			Commit();
		}
	}

	void SectionPanel::DrawBakeSection()
	{
		if (!ImGui::CollapsingHeader("Bake", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		const String assetPath = GetSectionAssetPath(m_worldPath);
		ImGui::TextDisabled("Asset: %s", assetPath.c_str());

		if (!m_hasBake)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Not baked yet");
		}
		else if (m_bakedStamp == m_currentStamp)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Baked asset is up to date");
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Baked asset is stale: the section changed since the bake");
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Compares the files on disk (world, pages, entities, foliage, definition). Unsaved edits count once the world is saved.");
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("Check"))
		{
			RefreshStamp();
		}

		ImGui::TextUnformatted("Paint layers (matched by albedo texture when stamped):");
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			if (m_layerTextures[layer].empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "  Layer %u: no texture (no layer binding, no Albedo0%u parameter); painting it fails the bake", static_cast<uint32>(layer + 1), static_cast<uint32>(layer + 1));
			}
			else
			{
				ImGui::TextDisabled("  Layer %u: %s", static_cast<uint32>(layer + 1), m_layerTextures[layer].c_str());
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Resolved from the terrain material's layer binding (Base Color), else its Albedo01..Albedo04 parameter.");
		}

		const String problem = ValidateDefinition();
		ImGui::BeginDisabled(!problem.empty());
		// The collapsing header above is also labelled "Bake": without its own ID the button would
		// share the header's ID and never fire.
		if (ImGui::Button("Bake##bakeSection"))
		{
			Bake();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Saves the world, then writes %s.", assetPath.c_str());
		}

		if (!m_status.empty())
		{
			if (m_statusIsError)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_status.c_str());
			}
			else
			{
				ImGui::TextWrapped("%s", m_status.c_str());
			}
		}
	}

	bool SectionPanel::ProjectToScreen(const Vector3& position, ImVec2& out) const
	{
		const Vector4 clip = m_overlayViewProj * Vector4(position, 1.0f);
		if (clip.w <= 0.0f)
		{
			return false;
		}

		out.x = m_overlayViewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * m_overlayViewportSize.x;
		out.y = m_overlayViewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * m_overlayViewportSize.y;
		return true;
	}

	float SectionPanel::GroundHeight(const float x, const float z) const
	{
		float height = 0.0f;
		terrain::Terrain* terrain = m_world.GetTerrain();
		if (terrain && m_world.HasTerrain() && terrain->TryGetSmoothHeightAt(x, z, height))
		{
			return height;
		}

		return 0.0f;
	}

	void SectionPanel::DrawGroundLine(ImDrawList* drawList, const float ax, const float az, const float bx, const float bz, const ImU32 color, const float thickness) const
	{
		constexpr float Step = 1.0f;
		constexpr float Lift = 0.05f;
		const float length = std::sqrt((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
		const int32 steps = std::max(1, static_cast<int32>(std::ceil(length / Step)));

		ImVec2 previous;
		bool previousVisible = false;
		for (int32 s = 0; s <= steps; ++s)
		{
			const float t = static_cast<float>(s) / static_cast<float>(steps);
			const float x = ax + (bx - ax) * t;
			const float z = az + (bz - az) * t;
			ImVec2 screen;
			const bool visible = ProjectToScreen(Vector3(x, GroundHeight(x, z) + Lift, z), screen);
			if (visible && previousVisible)
			{
				drawList->AddLine(previous, screen, color, thickness);
			}
			previous = screen;
			previousVisible = visible;
		}
	}

	void SectionPanel::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		if (!drawList || !m_definitionLoaded)
		{
			return;
		}

		Camera& camera = m_world.GetCamera();
		m_overlayViewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		m_overlayViewportMin = viewportMin;
		m_overlayViewportSize = viewportSize;

		drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

		const float length = m_definition.length;
		const float halfWidth = m_definition.halfWidth;
		const float reach = halfWidth + m_definition.sideFalloff;
		for (size_t i = 0; i < m_definition.variants.size(); ++i)
		{
			const float x = m_definition.variants[i].originX;
			const ImU32 color = VariantColors[i % std::size(VariantColors)];
			const ImU32 faint = WithAlpha(color, 110);

			// Falloff band: where the section fades into the world terrain.
			if (reach > halfWidth)
			{
				DrawGroundLine(drawList, x - reach, 0.0f, x - reach, length, faint, 1.0f);
				DrawGroundLine(drawList, x + reach, 0.0f, x + reach, length, faint, 1.0f);
				DrawGroundLine(drawList, x - reach, 0.0f, x - halfWidth, 0.0f, faint, 1.0f);
				DrawGroundLine(drawList, x + halfWidth, 0.0f, x + reach, 0.0f, faint, 1.0f);
				DrawGroundLine(drawList, x - reach, length, x - halfWidth, length, faint, 1.0f);
				DrawGroundLine(drawList, x + halfWidth, length, x + reach, length, faint, 1.0f);
			}

			// The rectangle the section owns.
			DrawGroundLine(drawList, x - halfWidth, 0.0f, x + halfWidth, 0.0f, color, 2.0f);
			DrawGroundLine(drawList, x + halfWidth, 0.0f, x + halfWidth, length, color, 2.0f);
			DrawGroundLine(drawList, x + halfWidth, length, x - halfWidth, length, color, 2.0f);
			DrawGroundLine(drawList, x - halfWidth, length, x - halfWidth, 0.0f, color, 2.0f);

			// Centre line with an arrow head pointing along +Z.
			const ImU32 centre = IM_COL32(255, 255, 255, 220);
			DrawGroundLine(drawList, x, 0.0f, x, length, centre, 1.5f);
			const float head = std::min(1.5f, length * 0.25f);
			DrawGroundLine(drawList, x - head * 0.6f, length - head, x, length, centre, 1.5f);
			DrawGroundLine(drawList, x + head * 0.6f, length - head, x, length, centre, 1.5f);

			ImVec2 labelPosition;
			if (ProjectToScreen(Vector3(x, GroundHeight(x, 0.0f) + 0.5f, 0.0f), labelPosition))
			{
				char label[48];
				std::snprintf(label, sizeof(label), "Variant %u", static_cast<uint32>(i + 1));
				drawList->AddText(labelPosition, color, label);
			}
		}

		drawList->PopClipRect();
	}
}
