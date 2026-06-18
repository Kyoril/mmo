// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "texture_editor_instance.h"

#include <cmath>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "texture_editor.h"
#include "graphics/texture_mgr.h"
#include "graphics/graphics_device.h"
#include "assets/asset_registry.h"
#include "tex_v1_0/header_load.h"
#include "tex/pre_header_load.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		/// @brief Returns a human-readable name for the given pixel format.
		const char* GetPixelFormatName(const PixelFormat format)
		{
			switch (format)
			{
			case PixelFormat::R8:			return "R8 (8-bit Single Channel)";
			case PixelFormat::RG8:			return "RG8 (16-bit Two Channel)";
			case PixelFormat::R8G8B8A8:		return "R8G8B8A8 (32-bit)";
			case PixelFormat::B8G8R8A8:		return "B8G8R8A8 (32-bit)";
			case PixelFormat::R16G16B16A16:	return "R16G16B16A16 (64-bit)";
			case PixelFormat::R32G32B32A32:	return "R32G32B32A32 (128-bit)";
			case PixelFormat::DXT1:			return "DXT1 / BC1 (Compressed)";
			case PixelFormat::DXT3:			return "DXT3 / BC2 (Compressed)";
			case PixelFormat::DXT5:			return "DXT5 / BC3 (Compressed)";
			case PixelFormat::BC4:			return "BC4 (Compressed Single Channel)";
			case PixelFormat::BC5:			return "BC5 (Compressed Two Channel)";
			case PixelFormat::D32F:			return "D32F (Depth 32-bit)";
			default:						return "Unknown";
			}
		}
	}

	TextureEditorInstance::TextureEditorInstance(EditorHost& host, TextureEditor& editor, Path assetPath)
		: EditorInstance(host, std::move(assetPath))
		, m_textureEditor(editor)
	{
		m_texture = TextureManager::Get().CreateOrRetrieve(m_assetPath.string());
		CreateDisplayTexture();
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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			// Save button with green styling
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
			if (ImGui::Button("Save Texture", ImVec2(200, 0)))
			{
				Save();
			}
			ImGui::PopStyleColor(3);

			ImGui::Spacing();

			// Texture Info Section
			if (ImGui::CollapsingHeader("Texture Info", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				if (m_texture)
				{
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Dimensions:");
					ImGui::SameLine();
					ImGui::Text("%d x %d px", m_texture->GetWidth(), m_texture->GetHeight());

					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Format:");
					ImGui::SameLine();
					ImGui::Text("%s", GetPixelFormatName(m_texture->GetPixelFormat()));

					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Compressed:");
					ImGui::SameLine();
					const PixelFormat fmt = m_texture->GetPixelFormat();
					const bool isCompressed = (fmt == PixelFormat::DXT1 || fmt == PixelFormat::DXT3 || fmt == PixelFormat::DXT5 || fmt == PixelFormat::BC4 || fmt == PixelFormat::BC5);
					ImGui::TextColored(
						isCompressed ? ImVec4(0.3f, 0.8f, 0.3f, 1.0f) : ImVec4(0.8f, 0.8f, 0.3f, 1.0f),
						"%s", isCompressed ? "Yes" : "No");

					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Mip Maps:");
					ImGui::SameLine();
					if (m_texture->HasMipMaps())
					{
						ImGui::Text("Yes (%u levels)", m_texture->GetMipMapCount());
					}
					else
					{
						ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "No");
					}

					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Memory:");
					ImGui::SameLine();
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

					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Asset:");
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
					String assetStr = m_assetPath.string();
					ImGui::SetNextItemWidth(-1);
					ImGui::InputText("##asset_path", &assetStr, ImGuiInputTextFlags_ReadOnly);
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load texture");
				}

				ImGui::Unindent();
			}

			ImGui::Spacing();

			// Texture Settings Section
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.32f, 0.32f, 0.35f, 1.0f));
			if (ImGui::CollapsingHeader("Texture Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				if (m_texture)
				{
					// Address Mode U
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Address U:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
					int addressModeU = static_cast<int>(m_texture->GetTextureAddressModeU());
					if (ImGui::Combo("##address_u", &addressModeU, "Clamp\0Wrap\0Mirror\0Border\0"))
					{
						m_texture->SetTextureAddressModeU(static_cast<TextureAddressMode>(addressModeU));
					}
					ImGui::PopStyleColor();

					// Address Mode V
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Address V:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
					int addressModeV = static_cast<int>(m_texture->GetTextureAddressModeV());
					if (ImGui::Combo("##address_v", &addressModeV, "Clamp\0Wrap\0Mirror\0Border\0"))
					{
						m_texture->SetTextureAddressModeV(static_cast<TextureAddressMode>(addressModeV));
					}
					ImGui::PopStyleColor();

					// Filter Mode
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Filter:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
					int filterMode = static_cast<int>(m_texture->GetTextureFilter());
					if (ImGui::Combo("##filter", &filterMode, "None\0Bilinear\0Trilinear\0Anisotropic\0"))
					{
						m_texture->SetFilter(static_cast<TextureFilter>(filterMode));
					}
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::TextDisabled("No texture loaded");
				}

				ImGui::Unindent();
			}
			ImGui::PopStyleColor(3);

			ImGui::Spacing();

			// View Settings Section
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.32f, 0.32f, 0.35f, 1.0f));
			if (ImGui::CollapsingHeader("View Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("Zoom:");
				ImGui::SameLine();
				ImGui::Text("%.0f%%", m_zoom * 100.0f);

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.5f, 0.8f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.6f, 0.9f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
				if (ImGui::Button("Reset View"))
				{
					ResetView();
				}
				ImGui::PopStyleColor(3);

				ImGui::Checkbox("Fit to Window", &m_fitToWindow);
				ImGui::Checkbox("Show Checkerboard", &m_showCheckerboard);

				ImGui::Unindent();
			}
			ImGui::PopStyleColor(3);

			ImGui::PopStyleVar(2);
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

			// Draw the texture (use display texture if available for proper channel reconstruction)
			const auto displayTex = m_displayTexture ? m_displayTexture : m_texture;
			drawList->AddImage(displayTex->GetTextureObject(), imageTopLeft, imageBottomRight);

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

	void TextureEditorInstance::CreateDisplayTexture()
	{
		if (!m_texture)
		{
			return;
		}

		const PixelFormat fmt = m_texture->GetPixelFormat();

		// Only create display texture for formats that don't render well as-is
		if (fmt != PixelFormat::BC4 && fmt != PixelFormat::BC5 &&
			fmt != PixelFormat::R8 && fmt != PixelFormat::RG8)
		{
			return;
		}

		const uint16 width = m_texture->GetWidth();
		const uint16 height = m_texture->GetHeight();
		if (width == 0 || height == 0)
		{
			return;
		}

		// Open the htex file and read the raw mip 0 data
		auto file = AssetRegistry::OpenFile(m_assetPath.string());
		if (!file)
		{
			WLOG("Failed to open texture file for display texture creation: " << m_assetPath.string());
			return;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		// Load pre header
		tex::PreHeader preHeader;
		if (!tex::loadPreHeader(preHeader, reader))
		{
			WLOG("Failed to load texture pre header for display texture");
			return;
		}

		// Load header
		tex::v1_0::Header header(tex::Version_1_0);
		if (!tex::v1_0::loadHeader(header, reader))
		{
			WLOG("Failed to load texture header for display texture");
			return;
		}

		if (header.mipmapOffsets[0] == 0 || header.mipmapLengths[0] == 0)
		{
			WLOG("No mip data available for display texture");
			return;
		}

		// Read mip 0 data
		std::vector<uint8> mipData(header.mipmapLengths[0]);
		file->seekg(header.mipmapOffsets[0], std::ios::beg);
		file->read(reinterpret_cast<char*>(mipData.data()), header.mipmapLengths[0]);

		// Create RGBA pixel data
		const size_t pixelCount = static_cast<size_t>(width) * height;
		std::vector<uint8> rgbaData(pixelCount * 4);

		if (fmt == PixelFormat::R8)
		{
			// Expand single channel to grayscale RGBA
			for (size_t i = 0; i < pixelCount; ++i)
			{
				const uint8 v = mipData[i];
				rgbaData[i * 4 + 0] = v;
				rgbaData[i * 4 + 1] = v;
				rgbaData[i * 4 + 2] = v;
				rgbaData[i * 4 + 3] = 255;
			}
		}
		else if (fmt == PixelFormat::RG8)
		{
			// Expand RG to RGBA, reconstruct blue channel for normal maps
			for (size_t i = 0; i < pixelCount; ++i)
			{
				const float r = mipData[i * 2 + 0] / 255.0f;
				const float g = mipData[i * 2 + 1] / 255.0f;

				// Reconstruct Z from X,Y (tangent space normal map: x² + y² + z² = 1)
				const float nx = r * 2.0f - 1.0f;
				const float ny = g * 2.0f - 1.0f;
				const float nzSq = 1.0f - nx * nx - ny * ny;
				const float nz = std::sqrt(std::max(0.0f, nzSq));
				const uint8 b = static_cast<uint8>((nz * 0.5f + 0.5f) * 255.0f);

				rgbaData[i * 4 + 0] = mipData[i * 2 + 0];
				rgbaData[i * 4 + 1] = mipData[i * 2 + 1];
				rgbaData[i * 4 + 2] = b;
				rgbaData[i * 4 + 3] = 255;
			}
		}
		else if (fmt == PixelFormat::BC4 || fmt == PixelFormat::BC5)
		{
			// BC4/BC5 decompression
			// A BC4 block is 8 bytes: 2 reference values + 6 bytes of 3-bit indices for a 4x4 texel block
			// BC5 is two BC4 blocks (16 bytes per texel block)
			const int blocksX = std::max(1, (width + 3) / 4);
			const int blocksY = std::max(1, (height + 3) / 4);
			const int blockSize = (fmt == PixelFormat::BC4) ? 8 : 16;

			// Lambda to decompress a single BC4 block (8 bytes) into 16 output values
			auto decompressBC4Block = [](const uint8* block, uint8 output[16])
			{
				const uint8 alpha0 = block[0];
				const uint8 alpha1 = block[1];

				// Build lookup table of 8 interpolated values
				uint8 lut[8];
				lut[0] = alpha0;
				lut[1] = alpha1;

				if (alpha0 > alpha1)
				{
					lut[2] = static_cast<uint8>((6 * alpha0 + 1 * alpha1) / 7);
					lut[3] = static_cast<uint8>((5 * alpha0 + 2 * alpha1) / 7);
					lut[4] = static_cast<uint8>((4 * alpha0 + 3 * alpha1) / 7);
					lut[5] = static_cast<uint8>((3 * alpha0 + 4 * alpha1) / 7);
					lut[6] = static_cast<uint8>((2 * alpha0 + 5 * alpha1) / 7);
					lut[7] = static_cast<uint8>((1 * alpha0 + 6 * alpha1) / 7);
				}
				else
				{
					lut[2] = static_cast<uint8>((4 * alpha0 + 1 * alpha1) / 5);
					lut[3] = static_cast<uint8>((3 * alpha0 + 2 * alpha1) / 5);
					lut[4] = static_cast<uint8>((2 * alpha0 + 3 * alpha1) / 5);
					lut[5] = static_cast<uint8>((1 * alpha0 + 4 * alpha1) / 5);
					lut[6] = 0;
					lut[7] = 255;
				}

				// Decode 3-bit indices from bytes 2..7 (48 bits total, 16 texels * 3 bits each)
				uint64 bits = 0;
				for (int i = 0; i < 6; ++i)
				{
					bits |= static_cast<uint64>(block[2 + i]) << (i * 8);
				}

				for (int i = 0; i < 16; ++i)
				{
					const int index = static_cast<int>((bits >> (i * 3)) & 0x7);
					output[i] = lut[index];
				}
			};

			for (int by = 0; by < blocksY; ++by)
			{
				for (int bx = 0; bx < blocksX; ++bx)
				{
					const size_t blockIndex = static_cast<size_t>(by) * blocksX + bx;
					const uint8* blockPtr = mipData.data() + blockIndex * blockSize;

					uint8 redValues[16];
					uint8 greenValues[16];

					if (fmt == PixelFormat::BC4)
					{
						decompressBC4Block(blockPtr, redValues);
						// For BC4/grayscale, green = red
						std::memcpy(greenValues, redValues, 16);
					}
					else
					{
						// BC5: first 8 bytes = red channel, second 8 bytes = green channel
						decompressBC4Block(blockPtr, redValues);
						decompressBC4Block(blockPtr + 8, greenValues);
					}

					// Write decompressed texels into the RGBA output
					for (int ty = 0; ty < 4; ++ty)
					{
						for (int tx = 0; tx < 4; ++tx)
						{
							const int px = bx * 4 + tx;
							const int py = by * 4 + ty;
							if (px >= width || py >= height)
							{
								continue;
							}

							const size_t pixelIndex = static_cast<size_t>(py) * width + px;
							const int texelIndex = ty * 4 + tx;
							const uint8 r = redValues[texelIndex];
							const uint8 g = greenValues[texelIndex];

							if (fmt == PixelFormat::BC4)
							{
								// Grayscale
								rgbaData[pixelIndex * 4 + 0] = r;
								rgbaData[pixelIndex * 4 + 1] = r;
								rgbaData[pixelIndex * 4 + 2] = r;
								rgbaData[pixelIndex * 4 + 3] = 255;
							}
							else
							{
								// BC5: reconstruct blue channel from normal map
								const float nx = r / 255.0f * 2.0f - 1.0f;
								const float ny = g / 255.0f * 2.0f - 1.0f;
								const float nzSq = 1.0f - nx * nx - ny * ny;
								const float nz = std::sqrt(std::max(0.0f, nzSq));
								const uint8 b = static_cast<uint8>((nz * 0.5f + 0.5f) * 255.0f);

								rgbaData[pixelIndex * 4 + 0] = r;
								rgbaData[pixelIndex * 4 + 1] = g;
								rgbaData[pixelIndex * 4 + 2] = b;
								rgbaData[pixelIndex * 4 + 3] = 255;
							}
						}
					}
				}
			}
		}

		// Create the display texture
		const String displayName = m_assetPath.string() + "_display";
		m_displayTexture = TextureManager::Get().CreateManual(displayName, width, height, PixelFormat::R8G8B8A8, BufferUsage::Static);
		if (m_displayTexture)
		{
			m_displayTexture->LoadRaw(rgbaData.data(), rgbaData.size());
			ILOG("Created display texture for " << m_assetPath.string());
		}
	}
}
