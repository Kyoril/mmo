// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "texture_editor_instance.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "texture_editor.h"
#include "graphics/texture_mgr.h"

namespace mmo
{
	TextureEditorInstance::TextureEditorInstance(EditorHost& host, TextureEditor& editor, Path assetPath)
		: EditorInstance(host, std::move(assetPath))
		, m_textureEditor(editor)
	{
		m_texture = TextureManager::Get().CreateOrRetrieve(m_assetPath.string());
	}

	void TextureEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##texture_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();

		// Draw sidebar windows
		DrawDetails(detailsId);

		// Draw viewport
		DrawViewport(viewportId);

		// Init dock layout to dock sidebar windows to the right by default
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockSpaceId;
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

		ImGui::PopID();
	}

	bool TextureEditorInstance::Save()
	{
		// TODO
		return true;
	}

	void TextureEditorInstance::DrawDetails(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			if (ImGui::CollapsingHeader("Texture Info", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

				if (ImGui::BeginTable("texture_info", 2, ImGuiTableFlags_Resizable))
				{
					ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 100.0f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					if (m_texture)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted("Width");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%d px", m_texture->GetWidth());

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted("Height");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%d px", m_texture->GetHeight());

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted("Memory");
						ImGui::TableSetColumnIndex(1);
						const uint32 memSize = m_texture->GetMemorySize();
						if (memSize >= 1024 * 1024)
						{
							ImGui::Text("%.2f MB", static_cast<float>(memSize) / (1024.0f * 1024.0f));
						}
						else if (memSize >= 1024)
						{
							ImGui::Text("%.2f KB", static_cast<float>(memSize) / 1024.0f);
						}
						else
						{
							ImGui::Text("%u bytes", memSize);
						}
					}
					else
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load texture");
					}

					ImGui::EndTable();
				}

				ImGui::PopStyleVar();
			}

			ImGui::Separator();

			if (ImGui::CollapsingHeader("View Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Show Checkerboard", &m_showCheckerboard);

				ImGui::Separator();

				ImGui::Text("Zoom: %.0f%%", m_zoom * 100.0f);
				ImGui::SameLine();
				if (ImGui::Button("Reset View"))
				{
					ResetView();
				}

				ImGui::Checkbox("Fit to Window", &m_fitToWindow);
			}
		}
		ImGui::End();
	}

	void TextureEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (!m_texture || !m_texture->GetTextureObject())
			{
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load texture: %s", m_assetPath.string().c_str());
				ImGui::End();
				return;
			}

			const auto availableSpace = ImGui::GetContentRegionAvail();
			if (availableSpace.x <= 0.0f || availableSpace.y <= 0.0f)
			{
				ImGui::End();
				return;
			}

			const float texWidth = static_cast<float>(m_texture->GetWidth());
			const float texHeight = static_cast<float>(m_texture->GetHeight());

			if (texWidth <= 0.0f || texHeight <= 0.0f)
			{
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Invalid texture dimensions");
				ImGui::End();
				return;
			}

			const float texAspect = texWidth / texHeight;

			// Calculate display size based on zoom and fit mode
			float displayWidth, displayHeight;
			if (m_fitToWindow)
			{
				const float viewAspect = availableSpace.x / availableSpace.y;
				if (texAspect > viewAspect)
				{
					displayWidth = availableSpace.x;
					displayHeight = availableSpace.x / texAspect;
				}
				else
				{
					displayHeight = availableSpace.y;
					displayWidth = availableSpace.y * texAspect;
				}

				m_zoom = displayWidth / texWidth;
				m_panOffset = ImVec2(0.0f, 0.0f);
			}
			else
			{
				displayWidth = texWidth * m_zoom;
				displayHeight = texHeight * m_zoom;
			}

			// Compute the top-left position to center the texture in the viewport
			const auto contentMin = ImGui::GetCursorScreenPos();
			const float offsetX = (availableSpace.x - displayWidth) * 0.5f + m_panOffset.x;
			const float offsetY = (availableSpace.y - displayHeight) * 0.5f + m_panOffset.y;

			const ImVec2 imageTopLeft(contentMin.x + offsetX, contentMin.y + offsetY);
			const ImVec2 imageBottomRight(imageTopLeft.x + displayWidth, imageTopLeft.y + displayHeight);

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			// Draw background
			const ImVec2 viewportTopLeft = contentMin;
			const ImVec2 viewportBottomRight(contentMin.x + availableSpace.x, contentMin.y + availableSpace.y);
			drawList->AddRectFilled(viewportTopLeft, viewportBottomRight, IM_COL32(40, 40, 40, 255));

			// Draw checkerboard behind the texture to show transparency
			if (m_showCheckerboard)
			{
				// Clip the checkerboard to the image area
				drawList->PushClipRect(
					ImVec2(std::max(imageTopLeft.x, viewportTopLeft.x), std::max(imageTopLeft.y, viewportTopLeft.y)),
					ImVec2(std::min(imageBottomRight.x, viewportBottomRight.x), std::min(imageBottomRight.y, viewportBottomRight.y)),
					true);
				DrawCheckerboard(drawList, imageTopLeft, imageBottomRight, 16.0f);
				drawList->PopClipRect();
			}

			// Draw the texture
			drawList->AddImage(m_texture->GetTextureObject(), imageTopLeft, imageBottomRight);

			// Invisible button to capture input over the entire viewport area
			ImGui::InvisibleButton("##texture_viewport", availableSpace);
			const bool isHovered = ImGui::IsItemHovered();

			// Handle zoom with mouse wheel
			if (isHovered && !m_fitToWindow)
			{
				const float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.0f)
				{
					const ImVec2 mousePos = ImGui::GetIO().MousePos;

					// Zoom toward mouse position
					const float oldZoom = m_zoom;
					const float zoomFactor = 1.15f;

					if (wheel > 0.0f)
					{
						m_zoom *= zoomFactor;
					}
					else
					{
						m_zoom /= zoomFactor;
					}

					// Clamp zoom
					m_zoom = std::max(0.01f, std::min(m_zoom, 100.0f));

					// Adjust pan so zoom is centered on mouse position
					const float zoomRatio = m_zoom / oldZoom;
					const float centerX = contentMin.x + availableSpace.x * 0.5f;
					const float centerY = contentMin.y + availableSpace.y * 0.5f;
					const float mouseRelX = mousePos.x - centerX - m_panOffset.x;
					const float mouseRelY = mousePos.y - centerY - m_panOffset.y;

					m_panOffset.x -= mouseRelX * (zoomRatio - 1.0f);
					m_panOffset.y -= mouseRelY * (zoomRatio - 1.0f);
				}
			}

			// Handle panning with middle mouse button or right mouse button
			if (isHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
			{
				m_isPanning = true;
				m_lastMousePos = ImGui::GetIO().MousePos;
			}

			if (m_isPanning)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					const ImVec2 mousePos = ImGui::GetIO().MousePos;
					const ImVec2 delta(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);
					m_panOffset.x += delta.x;
					m_panOffset.y += delta.y;
					m_lastMousePos = mousePos;

					// Disable fit-to-window when the user starts panning
					if (m_fitToWindow && (delta.x != 0.0f || delta.y != 0.0f))
					{
						m_fitToWindow = false;
					}
				}
				else
				{
					m_isPanning = false;
				}
			}

			// Handle zoom with mouse wheel even in fit mode (switches to manual zoom)
			if (isHovered && m_fitToWindow)
			{
				const float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.0f)
				{
					m_fitToWindow = false;

					const float zoomFactor = 1.15f;
					if (wheel > 0.0f)
					{
						m_zoom *= zoomFactor;
					}
					else
					{
						m_zoom /= zoomFactor;
					}
					m_zoom = std::max(0.01f, std::min(m_zoom, 100.0f));
					m_panOffset = ImVec2(0.0f, 0.0f);
				}
			}

			// Show pixel coordinates and zoom info in the status area
			if (isHovered)
			{
				const ImVec2 mousePos = ImGui::GetIO().MousePos;
				const float pixelX = (mousePos.x - imageTopLeft.x) / m_zoom;
				const float pixelY = (mousePos.y - imageTopLeft.y) / m_zoom;

				if (pixelX >= 0.0f && pixelX < texWidth && pixelY >= 0.0f && pixelY < texHeight)
				{
					ImGui::SetCursorScreenPos(ImVec2(viewportTopLeft.x + 4.0f, viewportBottomRight.y - ImGui::GetTextLineHeightWithSpacing() - 4.0f));
					ImGui::Text("Pixel: (%d, %d)  Zoom: %.0f%%", static_cast<int>(pixelX), static_cast<int>(pixelY), m_zoom * 100.0f);
				}
			}
		}
		ImGui::End();
	}

	void TextureEditorInstance::DrawCheckerboard(ImDrawList* drawList, const ImVec2 topLeft, const ImVec2 bottomRight, const float cellSize) const
	{
		const ImU32 colorLight = IM_COL32(200, 200, 200, 255);
		const ImU32 colorDark = IM_COL32(128, 128, 128, 255);

		const float width = bottomRight.x - topLeft.x;
		const float height = bottomRight.y - topLeft.y;

		const int cols = static_cast<int>(std::ceil(width / cellSize));
		const int rows = static_cast<int>(std::ceil(height / cellSize));

		for (int y = 0; y < rows; ++y)
		{
			for (int x = 0; x < cols; ++x)
			{
				const ImU32 color = ((x + y) % 2 == 0) ? colorLight : colorDark;
				const ImVec2 cellMin(topLeft.x + x * cellSize, topLeft.y + y * cellSize);
				const ImVec2 cellMax(
					std::min(cellMin.x + cellSize, bottomRight.x),
					std::min(cellMin.y + cellSize, bottomRight.y));
				drawList->AddRectFilled(cellMin, cellMax, color);
			}
		}
	}

	void TextureEditorInstance::ResetView()
	{
		m_fitToWindow = true;
		m_zoom = 1.0f;
		m_panOffset = ImVec2(0.0f, 0.0f);
	}
}
