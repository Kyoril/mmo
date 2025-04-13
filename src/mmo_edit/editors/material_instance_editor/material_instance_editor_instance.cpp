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
#include "log/default_log_levels.h"
#include "preview_providers/preview_provider_manager.h"
#include "scene_graph/material_instance_serializer.h"
#include "scene_graph/material_serializer.h"
#include "scene_graph/material_manager.h"


namespace mmo
{
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

	void MaterialInstanceEditorInstance::Save() const
	{
		m_material->SetName(GetAssetPath().string());

		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open material file " << GetAssetPath() << " for writing!");
			return;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialInstanceSerializer serializer;
		serializer.Export(*m_material, writer);

		ILOG("Successfully saved material");

    	m_editor.GetPreviewManager().InvalidatePreview(m_assetPath.string());
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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

			if (m_material)
			{
				// Scalar parameters
				if (ImGui::CollapsingHeader("Scalar Parameters", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushID("ScalarParameters");

					for (const auto& param : m_material->GetScalarParameters())
					{
						float value = param.value;
						if (ImGui::InputFloat(param.name.c_str(), &value))
						{
							m_material->SetScalarParameter(param.name, value);
						}
					}

					ImGui::PopID();
				}

				if (ImGui::CollapsingHeader("Vector Parameters", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushID("VectorParameters");

					for (const auto& param : m_material->GetVectorParameters())
					{
						float values[4] = { param.value.x, param.value.y, param.value.z, param.value.w };
						if (ImGui::ColorEdit4(param.name.c_str(), values, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float))
						{
							m_material->SetVectorParameter(param.name, Vector4(values[0], values[1], values[2], values[3]));
						}
					}

					ImGui::PopID();
				}

				if (ImGui::CollapsingHeader("Texture Parameters", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushID("TextureParameters");

					for (const auto& param : m_material->GetTextureParameters())
					{
						if (ImGui::BeginCombo(param.name.c_str(), !param.texture.empty() ? param.texture.c_str() : "(None)", ImGuiComboFlags_HeightLargest))
						{
							if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
							{
								ImGui::SetKeyboardFocusHere(0);
							}
							m_assetFilter.Draw("##asset_filter");

							if (ImGui::BeginChild("##asset_scroll_area", ImVec2(0, 400)))
							{
								const auto files = AssetRegistry::ListFiles(".htex");
								for (auto& file : files)
								{
									if (m_assetFilter.IsActive())
									{
										if (!m_assetFilter.PassFilter(file.c_str()))
										{
											continue;
										}
									}

									ImGui::PushID(file.c_str());
									if (ImGui::Selectable(Path(file).filename().string().c_str()))
									{
										m_material->SetTextureParameter(param.name, file);

										m_assetFilter.Clear();
										ImGui::CloseCurrentPopup();
									}
									ImGui::PopID();
								}
							}
							ImGui::EndChild();

							ImGui::EndCombo();
						}
					}

					ImGui::PopID();
				}

			}

		    ImGui::PopStyleVar();
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
}
