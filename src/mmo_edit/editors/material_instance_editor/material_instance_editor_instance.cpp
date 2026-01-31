// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_instance_editor_instance.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.inl"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include <cinttypes>

#include "material_instance_editor.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "editor_windows/asset_picker_widget.h"
#include "log/default_log_levels.h"
#include "preview_providers/preview_provider_manager.h"
#include "scene_graph/material_instance_serializer.h"
#include "scene_graph/material_serializer.h"
#include "scene_graph/material_manager.h"


namespace mmo
{
	const std::set<String> MaterialInstanceEditorInstance::s_parentMaterialExtensions = { ".hmat" };
	const std::set<String> MaterialInstanceEditorInstance::s_textureExtensions = { ".htex" };
    void MaterialInstanceEditorInstance::RenderMaterialPreview()
    {
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_deferredRenderer->Render(m_scene, *m_camera);
    }

	MaterialInstanceEditorInstance::MaterialInstanceEditorInstance(MaterialInstanceEditor& editor, EditorHost& host, const Path& assetPath)
		: EditorInstance(host, assetPath)
		, m_editor(editor)
	{
		// Load material instance
		m_material = std::static_pointer_cast<MaterialInstance>(MaterialManager::Get().Load(assetPath.string()));
		m_material->SetName(assetPath.string());

		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));
		m_cameraAnchor->Yaw(Degree(-45.0f), TransformSpace::World);

		m_lightNode = m_scene.GetRootSceneNode().CreateChildSceneNode("LightNode");
		m_light = &m_scene.CreateLight("Light", LightType::Directional);
		m_lightNode->AttachObject(*m_light);
		m_light->SetDirection(Vector3(-0.5f, -1.0f, -0.3f));
		m_light->SetIntensity(1.0f);
		m_light->SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, 640, 480);

		m_entity = m_scene.CreateEntity(assetPath.string(), "Editor/Sphere.hmsh");
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);
			
			m_entity->SetMaterial(m_material);
		}

		m_renderConnection = host.beforeUiUpdate.connect(this, &MaterialInstanceEditorInstance::RenderMaterialPreview);

		// Ensure parameters are up to date
		m_material->RefreshParametersFromBase();
	}

	MaterialInstanceEditorInstance::~MaterialInstanceEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}
		
		m_scene.Clear();
	}

	bool MaterialInstanceEditorInstance::Save()
	{
		m_material->SetName(GetAssetPath().string());

		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open material file " << GetAssetPath() << " for writing!");
			return false;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialInstanceSerializer serializer;
		serializer.Export(*m_material, writer);

		ILOG("Successfully saved material instance " << GetAssetPath());

    	m_editor.GetPreviewManager().InvalidatePreview(m_assetPath.string());
		return true;
	}

	void MaterialInstanceEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("MaterialInstanceGraph");
		ImGui::DockSpace(dockspaceId, ImVec2(-1.0f, -1.0f), ImGuiDockNodeFlags_AutoHideTabBar);
		
		const String previewId = "Preview##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();

		if (ImGui::Begin(previewId.c_str()))
		{
			if (ImGui::Button("Save"))
			{
				Save();
			}

			if (ImGui::BeginChild("previewPanel", ImVec2(-1, -1)))
			{
				// Determine the current viewport position
				auto viewportPos = ImGui::GetWindowContentRegionMin();
				viewportPos.x += ImGui::GetWindowPos().x;
				viewportPos.y += ImGui::GetWindowPos().y;

				// Determine the available size for the viewport window and either create the render target
				// or resize it if needed
				const auto availableSpace = ImGui::GetContentRegionAvail();
				
				if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
				{
					m_deferredRenderer->Resize(availableSpace.x, availableSpace.y);
					m_lastAvailViewportSize = availableSpace;

					RenderMaterialPreview();
				}

				// Render the render target content into the window as image object
				ImGui::Image(m_deferredRenderer->GetFinalRenderTarget()->GetTextureObject(), availableSpace);

				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					m_leftButtonPressed = true;
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
		
		if (ImGui::Begin(detailsId.c_str(), nullptr))
		{
			DrawDetailsPanel(detailsId);
		}
		ImGui::End();
		
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
			ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockspaceId;
			auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);

			ImGui::DockBuilderDockWindow(previewId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockspaceId);

		ImGui::PopID();
	}

	void MaterialInstanceEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseButtonDown(button, x, y);
		
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void MaterialInstanceEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseButtonUp(button, x, y);
		
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
	}

	void MaterialInstanceEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseMoved(x, y);
		
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void MaterialInstanceEditorInstance::DrawDetailsPanel(const String& id)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

		// Save Button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
		if (ImGui::Button("Save Material Instance", ImVec2(-1, 0)))
		{
			Save();
		}
		ImGui::PopStyleColor(3);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (m_material)
		{
			// Parent Material Section
			DrawParentMaterialSection();

			ImGui::Spacing();

			// Material Properties Section
			DrawMaterialPropertiesSection();

			ImGui::Spacing();

			// Scalar Parameters Section
			DrawScalarParametersSection();

			ImGui::Spacing();

			// Vector Parameters Section
			DrawVectorParametersSection();

			ImGui::Spacing();

			// Texture Parameters Section
			DrawTextureParametersSection();
		}
		else
		{
			ImGui::TextDisabled("No material instance loaded");
		}

		ImGui::PopStyleVar(2);
	}

	void MaterialInstanceEditorInstance::DrawParentMaterialSection()
	{
		if (ImGui::CollapsingHeader("Parent Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			ImGui::TextDisabled("The parent material defines the shaders and default parameters.");
			ImGui::Spacing();

			// Get current parent material name
			String parentMaterialPath;
			if (m_material->GetParent())
			{
				parentMaterialPath = m_material->GetParent()->GetName();
			}

			// Parent material picker
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Parent:");

			if (AssetPickerWidget::Draw("##parentMaterial", parentMaterialPath, s_parentMaterialExtensions, &m_editor.GetPreviewManager(), nullptr, 64.0f))
			{
				if (!parentMaterialPath.empty())
				{
					ChangeParentMaterial(parentMaterialPath);
				}
			}

			// Show parent material info
			if (m_material->GetParent())
			{
				ImGui::Spacing();
				ImGui::TextDisabled("Base Material: %s", m_material->GetBaseMaterial() ? std::string(m_material->GetBaseMaterial()->GetName()).c_str() : "None");
			}

			ImGui::Unindent();
		}
	}

	void MaterialInstanceEditorInstance::DrawMaterialPropertiesSection()
	{
		if (ImGui::CollapsingHeader("Material Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Two-sided
			bool twoSided = m_material->IsTwoSided();
			if (ImGui::Checkbox("Two Sided", &twoSided))
			{
				m_material->SetTwoSided(twoSided);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Disable backface culling for this material");
			}

			// Cast Shadows
			bool castShadows = m_material->IsCastingShadows();
			if (ImGui::Checkbox("Cast Shadows", &castShadows))
			{
				m_material->SetCastShadows(castShadows);
			}

			// Receive Shadows
			bool receiveShadows = m_material->IsReceivingShadows();
			if (ImGui::Checkbox("Receive Shadows", &receiveShadows))
			{
				m_material->SetReceivesShadows(receiveShadows);
			}

			// Depth Test
			bool depthTest = m_material->IsDepthTestEnabled();
			if (ImGui::Checkbox("Depth Test", &depthTest))
			{
				m_material->SetDepthTestEnabled(depthTest);
			}

			// Depth Write
			bool depthWrite = m_material->IsDepthWriteEnabled();
			if (ImGui::Checkbox("Depth Write", &depthWrite))
			{
				m_material->SetDepthWriteEnabled(depthWrite);
			}

			// Wireframe
			bool wireframe = m_material->IsWireframe();
			if (ImGui::Checkbox("Wireframe", &wireframe))
			{
				m_material->SetWireframe(wireframe);
			}

			ImGui::Unindent();
		}
	}

	void MaterialInstanceEditorInstance::DrawScalarParametersSection()
	{
		const auto& scalarParams = m_material->GetScalarParameters();
		
		String headerLabel = "Scalar Parameters";
		if (!scalarParams.empty())
		{
			headerLabel += " (" + std::to_string(scalarParams.size()) + ")";
		}

		if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (scalarParams.empty())
			{
				ImGui::TextDisabled("No scalar parameters defined in parent material");
			}
			else
			{
				for (const auto& param : scalarParams)
				{
					ImGui::PushID(param.name.c_str());

					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
					
					float value = param.value;
					ImGui::SetNextItemWidth(-1);
					if (ImGui::DragFloat(param.name.c_str(), &value, 0.01f))
					{
						m_material->SetScalarParameter(param.name, value);
					}

					ImGui::PopStyleColor();
					ImGui::PopID();
				}
			}

			ImGui::Unindent();
		}
	}

	void MaterialInstanceEditorInstance::DrawVectorParametersSection()
	{
		const auto& vectorParams = m_material->GetVectorParameters();
		
		String headerLabel = "Vector Parameters";
		if (!vectorParams.empty())
		{
			headerLabel += " (" + std::to_string(vectorParams.size()) + ")";
		}

		if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (vectorParams.empty())
			{
				ImGui::TextDisabled("No vector parameters defined in parent material");
			}
			else
			{
				for (const auto& param : vectorParams)
				{
					ImGui::PushID(param.name.c_str());

					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));

					float values[4] = { param.value.x, param.value.y, param.value.z, param.value.w };
					
					// Use color edit for color-like vectors
					if (ImGui::ColorEdit4(param.name.c_str(), values, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
					{
						m_material->SetVectorParameter(param.name, Vector4(values[0], values[1], values[2], values[3]));
					}

					ImGui::PopStyleColor();
					ImGui::PopID();
				}
			}

			ImGui::Unindent();
		}
	}

	void MaterialInstanceEditorInstance::DrawTextureParametersSection()
	{
		const auto& textureParams = m_material->GetTextureParameters();
		
		String headerLabel = "Texture Parameters";
		if (!textureParams.empty())
		{
			headerLabel += " (" + std::to_string(textureParams.size()) + ")";
		}

		if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (textureParams.empty())
			{
				ImGui::TextDisabled("No texture parameters defined in parent material");
			}
			else
			{
				for (const auto& param : textureParams)
				{
					ImGui::PushID(param.name.c_str());

					// Parameter name header
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.3f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.32f, 0.32f, 0.35f, 1.0f));

					const bool isOpen = ImGui::CollapsingHeader(param.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

					ImGui::PopStyleColor(3);

					if (isOpen)
					{
						ImGui::Indent();

						String texturePath = param.texture;
						if (AssetPickerWidget::Draw("##texture", texturePath, s_textureExtensions, &m_editor.GetPreviewManager(), nullptr, 64.0f))
						{
							m_material->SetTextureParameter(param.name, texturePath);
						}

						ImGui::Unindent();
					}

					ImGui::PopID();

					ImGui::Spacing();
				}
			}

			ImGui::Unindent();
		}
	}

	void MaterialInstanceEditorInstance::ChangeParentMaterial(const String& newParentPath)
	{
		if (newParentPath.empty())
		{
			return;
		}

		// Load the new parent material
		MaterialPtr newParent = std::static_pointer_cast<Material>(MaterialManager::Get().Load(newParentPath));
		if (!newParent)
		{
			ELOG("Failed to load parent material: " << newParentPath);
			return;
		}

		// Get current parent for comparison
		MaterialPtr currentParent = m_material->GetParent();
		if (currentParent && std::string(currentParent->GetName()) == newParentPath)
		{
			// Same parent, no change needed
			return;
		}

		ILOG("Changing parent material to: " << newParentPath);

		// Set the new parent - this will reset parameters to parent defaults
		m_material->SetParent(newParent);

		// Refresh parameters from the new parent
		m_material->RefreshParametersFromBase();

		// Update the entity's material to reflect changes
		if (m_entity)
		{
			m_entity->SetMaterial(m_material);
		}
	}
}
