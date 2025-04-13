// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "texture_editor_instance.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "texture_editor.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/render_queue.h"

namespace mmo
{
	TextureEditorInstance::TextureEditorInstance(EditorHost& host, TextureEditor& editor, Path assetPath)
		: EditorInstance(host, std::move(assetPath))
		, m_textureEditor(editor)
	{
		m_renderConnection = m_textureEditor.GetHost().beforeUiUpdate.connect(this, &TextureEditorInstance::Render);

		// Create preview material instance
		m_previewMaterialInst = std::make_shared<MaterialInstance>("TexturePreview", MaterialManager::Get().Load("Editor/TextureEditorPreview.hmat"));
		m_previewMaterialInst->SetTextureParameter("Texture", m_assetPath.string());

		// Create preview buffer
		const POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX vertices[] = {
			{ Vector3(-1.0f, -1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {0.0f, 1.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {1.0f, 1.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {0.0f, 0.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {0.0f, 0.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {1.0f, 1.0f}},
			{ Vector3(1.0f, 1.0f, 0.0f), Color::White, Vector3::UnitY, Vector3::UnitZ, Vector3::UnitZ, {1.0f, 0.0f}}
		};

		m_vertexData = std::make_shared<VertexData>(&GraphicsDevice::Get());
		m_vertexData->vertexBufferBinding->SetBinding(0, GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX), BufferUsage::Static, vertices));

		uint32 offset = 0;
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position, 0).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Color, VertexElementSemantic::Diffuse, 0).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal, 0).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal, 0).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent, 0).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate, 0).GetSize();

		m_vertexData->vertexCount = 6;
		m_vertexData->vertexStart = 0;
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
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
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

	void TextureEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);

		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::Identity);

		RenderOperation op{ Main };
		op.vertexData = m_vertexData.get();
		op.material = m_previewMaterialInst;
		op.topology = TopologyType::TriangleList;
		op.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
		gx.Render(op);

		m_viewportRT->Update();
	}

	void TextureEditorInstance::Save()
	{
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

			if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
				if (ImGui::BeginTable("split", 2, ImGuiTableFlags_Resizable))
				{
					// TODO

					ImGui::EndTable();
				}

				ImGui::PopStyleVar();
			}
		}
		ImGui::End();
	}

	void TextureEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			// Determine the current viewport position
			auto viewportPos = ImGui::GetWindowContentRegionMin();
			viewportPos.x += ImGui::GetWindowPos().x;
			viewportPos.y += ImGui::GetWindowPos().y;

			// Determine the available size for the viewport window and either create the render target
			// or resize it if needed
			const auto availableSpace = ImGui::GetContentRegionAvail();

			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y), RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the render target content into the window as image object
			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
			ImGui::SetItemUsingMouseWheel();

			// TODO
		}
		ImGui::End();
	}
}
