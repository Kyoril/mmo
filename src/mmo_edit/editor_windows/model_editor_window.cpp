// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "model_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <algorithm>

#include "asset_picker_widget.h"
#include "assets/asset_registry.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/axis_display.h"

namespace mmo
{
	class ModelPreview final : public CustomizationPropertyGroupApplier
	{
	public:
		explicit ModelPreview(EditorHost& host)
			: m_host(host)
		{
			m_cameraAnchor = &m_scene.CreateSceneNode("ModelPreviewCameraAnchor");
			m_cameraNode = &m_scene.CreateSceneNode("ModelPreviewCameraNode");
			m_cameraAnchor->AddChild(*m_cameraNode);
			m_camera = m_scene.CreateCamera("ModelPreviewCamera");
			m_cameraNode->AttachObject(*m_camera);
			m_cameraNode->SetPosition(Vector3(0.0f, 1.6f, 4.5f));
			m_cameraAnchor->SetOrientation(Quaternion(Degree(-12.0f), Vector3::UnitX));
			m_camera->SetNearClipDistance(0.1f);
			m_camera->SetFarClipDistance(500.0f);
			m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

			m_worldGrid = std::make_unique<WorldGrid>(m_scene, "ModelPreviewGrid");
			m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "ModelPreviewAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());
			m_renderConnection = m_host.beforeUiUpdate.connect(this, &ModelPreview::Render);
		}

		~ModelPreview() override
		{
			if (m_entity)
			{
				m_scene.DestroyEntity(*m_entity);
				m_entity = nullptr;
			}
			m_worldGrid.reset();
			m_axisDisplay.reset();
			m_scene.Clear();
		}

	public:
		void Draw(proto::ModelDataEntry* entry)
		{
			if (entry != m_currentEntry)
			{
				m_currentEntry = entry;
				m_lastEntrySerialized.clear();
				m_rebuildRequested = true;
			}

			if (m_currentEntry)
			{
				const std::string serialized = m_currentEntry->SerializeAsString();
				if (serialized != m_lastEntrySerialized)
				{
					m_lastEntrySerialized = serialized;
					m_rebuildRequested = true;
				}
			}

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			if (DrawPrimaryButton("Reload Preview", ImVec2(140.0f, 0.0f)))
			{
				m_rebuildRequested = true;
			}
			ImGui::SameLine();
			DrawHelpMarker("Left-drag: orbit, Right-drag: pan, Mouse wheel: zoom");

			RebuildPreviewIfNeeded();
			ImGui::Separator();
			DrawViewport();

			ImGui::PopStyleVar(2);
		}

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override
		{
			if (!m_entity)
			{
				return;
			}

			if (!group.subEntityTag.empty())
			{
				for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(i);
					if (subMesh.HasTag(group.subEntityTag))
					{
						if (SubEntity* subEntity = m_entity->GetSubEntity(i))
						{
							subEntity->SetVisible(false);
						}
					}
				}
			}

			const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
			if (it == configuration.chosenOptionPerGroup.end())
			{
				return;
			}

			for (const auto& value : group.possibleValues)
			{
				if (value.valueId != it->second)
				{
					continue;
				}

				for (const auto& subEntityName : value.visibleSubEntities)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}

		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override
		{
			if (!m_entity)
			{
				return;
			}

			const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
			if (it == configuration.chosenOptionPerGroup.end())
			{
				return;
			}

			for (const auto& value : group.possibleValues)
			{
				if (value.valueId != it->second)
				{
					continue;
				}

				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(pair.first))
					{
						if (MaterialPtr material = MaterialManager::Get().Load(pair.second))
						{
							subEntity->SetMaterial(material);
						}
					}
				}
			}
		}

		void Apply(const ScalarParameterPropertyGroup&, const AvatarConfiguration&) override
		{
		}

	private:
		void RebuildPreviewIfNeeded()
		{
			if (!m_rebuildRequested)
			{
				return;
			}

			m_rebuildRequested = false;

			if (m_entity)
			{
				m_scene.DestroyEntity(*m_entity);
				m_entity = nullptr;
			}

			m_avatarDefinition.reset();
			m_avatarConfiguration = AvatarConfiguration();

			if (!m_currentEntry || m_currentEntry->filename().empty())
			{
				return;
			}

			String meshPath = m_currentEntry->filename();
			const Path filePath = meshPath;
			if (filePath.extension() == ".char")
			{
				m_avatarDefinition = AvatarDefinitionManager::Get().Load(meshPath);
				if (!m_avatarDefinition)
				{
					return;
				}

				meshPath = m_avatarDefinition->GetBaseMesh();
				if (meshPath.empty())
				{
					return;
				}

				// Start from deterministic defaults for all customization groups.
				for (const auto& property : *m_avatarDefinition)
				{
					switch (property->GetType())
					{
					case CharacterCustomizationPropertyType::VisibilitySet:
					{
						auto* vis = static_cast<VisibilitySetPropertyGroup*>(property.get());
						if (!vis->possibleValues.empty())
						{
							m_avatarConfiguration.chosenOptionPerGroup[property->GetId()] = vis->possibleValues.front().valueId;
						}
						break;
					}
					case CharacterCustomizationPropertyType::MaterialOverride:
					{
						auto* mat = static_cast<MaterialOverridePropertyGroup*>(property.get());
						if (!mat->possibleValues.empty())
						{
							m_avatarConfiguration.chosenOptionPerGroup[property->GetId()] = mat->possibleValues.front().valueId;
						}
						break;
					}
					default:
						break;
					}
				}

				// Apply explicit editor overrides on top.
				for (const auto& pair : m_currentEntry->customizationproperties())
				{
					bool validOverride = false;
					for (const auto& property : *m_avatarDefinition)
					{
						if (property->GetId() != pair.first)
						{
							continue;
						}

						switch (property->GetType())
						{
						case CharacterCustomizationPropertyType::VisibilitySet:
						{
							const auto* vis = static_cast<const VisibilitySetPropertyGroup*>(property.get());
							for (const auto& value : vis->possibleValues)
							{
								if (value.valueId == pair.second)
								{
									validOverride = true;
									break;
								}
							}
							break;
						}
						case CharacterCustomizationPropertyType::MaterialOverride:
						{
							const auto* mat = static_cast<const MaterialOverridePropertyGroup*>(property.get());
							for (const auto& value : mat->possibleValues)
							{
								if (value.valueId == pair.second)
								{
									validOverride = true;
									break;
								}
							}
							break;
						}
						default:
							break;
						}

						break;
					}

					// Ignore stale overrides from old definition revisions.
					if (validOverride)
					{
						m_avatarConfiguration.chosenOptionPerGroup[pair.first] = pair.second;
					}
				}
			}

			m_entity = m_scene.CreateEntity("ModelEditorPreviewEntity", meshPath);
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_entity->ResetSubEntities();

			if (m_avatarDefinition)
			{
				m_avatarConfiguration.Apply(*this, *m_avatarDefinition);
			}

			const float radius = std::max(1.0f, m_entity->GetBoundingRadius());
			m_cameraAnchor->SetPosition(Vector3::UnitY * radius * 0.5f);
			m_cameraNode->SetPosition(Vector3(0.0f, radius * 0.2f, radius * 2.2f));
		}

		void DrawViewport()
		{
			const ImVec2 availableSpace = ImGui::GetContentRegionAvail();
			if (!m_viewportRT)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture(
					"ModelEditorPreviewViewport",
					std::max(1.0f, availableSpace.x),
					std::max(1.0f, availableSpace.y),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_viewportSize = availableSpace;
			}
			else if (m_viewportSize.x != availableSpace.x || m_viewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
				m_viewportSize = availableSpace;
			}

			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
			ImGui::SetItemUsingMouseWheel();

			if (ImGui::IsItemHovered())
			{
				const float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.0f)
				{
					m_cameraNode->Translate(Vector3::UnitZ * -wheel * 0.35f, TransformSpace::Local);
				}

				if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				{
					const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
					ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
					m_cameraAnchor->Yaw(Degree(-delta.x * 0.25f), TransformSpace::World);
					m_cameraAnchor->Pitch(Degree(-delta.y * 0.25f), TransformSpace::Local);
				}

				if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
				{
					const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
					ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
					m_cameraAnchor->Translate(Vector3(-delta.x * 0.01f, delta.y * 0.01f, 0.0f), TransformSpace::World);
				}
			}
		}

		void Render()
		{
			if (!m_viewportRT || m_viewportSize.x <= 0.0f || m_viewportSize.y <= 0.0f)
			{
				return;
			}0

			auto& gx = GraphicsDevice::Get();
			gx.Reset();
			gx.SetClearColor(Color(0.1f, 0.12f, 0.16f, 1.0f));
			m_viewportRT->Activate();
			m_viewportRT->Clear(ClearFlags::All);
			gx.SetViewport(0, 0, m_viewportSize.x, m_viewportSize.y, 0.0f, 1.0f);
			m_camera->SetAspectRatio(m_viewportSize.x / m_viewportSize.y);
			m_scene.Render(*m_camera, PixelShaderType::Forward);
			m_viewportRT->Update();
		}

	private:
		EditorHost& m_host;
		scoped_connection m_renderConnection;
		Scene m_scene;
		SceneNode* m_cameraAnchor{ nullptr };
		SceneNode* m_cameraNode{ nullptr };
		Camera* m_camera{ nullptr };
		std::unique_ptr<WorldGrid> m_worldGrid;
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		RenderTexturePtr m_viewportRT;
		ImVec2 m_viewportSize{ 0.0f, 0.0f };
		Entity* m_entity{ nullptr };

		proto::ModelDataEntry* m_currentEntry{ nullptr };
		String m_lastEntrySerialized;
		bool m_rebuildRequested{ true };

		std::shared_ptr<CustomizableAvatarDefinition> m_avatarDefinition;
		AvatarConfiguration m_avatarConfiguration;
	};
}

namespace mmo
{
	ModelEditorWindow::ModelEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.models, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_toolbarButtonText = "Models";
		ReloadModelList();
		m_preview = std::make_unique<ModelPreview>(m_host);

		m_assetImported = m_host.assetImported.connect(this, &ModelEditorWindow::OnAssetImported);
	}

	ModelEditorWindow::~ModelEditorWindow() = default;

	bool ModelEditorWindow::Draw()
	{
		if (!m_visible)
		{
			return false;
		}

		ImGui::SetNextWindowSize(ImVec2(1400, 860), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(m_name.c_str(), &m_visible, ImGuiWindowFlags_MenuBar))
		{
			const float windowWidth = ImGui::GetContentRegionAvail().x;
			const float listWidth = 320.0f;
			const float previewWidth = std::max(windowWidth * 0.38f, 470.0f);
			const float detailsWidth = windowWidth - listWidth - previewWidth - 16.0f;

			ImGui::BeginChild("##modelList", ImVec2(listWidth, 0), true);
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

				if (DrawSuccessButton("+ Add New", ImVec2(-1, 0)))
				{
					auto* entry = m_manager.add();
					OnNewEntry(*entry);
					m_currentItem = m_manager.count() - 1;
				}

				ImGui::BeginDisabled(m_currentItem < 0 || m_currentItem >= m_manager.count());
				if (DrawDangerButton("Remove Selected", ImVec2(-1, 0)))
				{
					m_manager.remove(m_manager.getTemplates().entry().at(m_currentItem).id());
					m_currentItem = -1;
				}
				ImGui::EndDisabled();

				ImGui::Spacing();
				static char searchBuffer[256] = "";
				ImGui::SetNextItemWidth(-1);
				ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
				std::string lowerSearch = searchBuffer;
				std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%d entries", m_manager.count());
				ImGui::Spacing();

				ImGui::BeginChild("##modelListScroll", ImVec2(0, 0));
				for (int i = 0; i < m_manager.count(); ++i)
				{
					const auto& entry = m_manager.getTemplates().entry().at(i);
					if (!lowerSearch.empty())
					{
						std::string lowerName = entry.name();
						std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						std::string idString = std::to_string(entry.id());
						if (lowerName.find(lowerSearch) == std::string::npos && idString.find(lowerSearch) == std::string::npos)
						{
							continue;
						}
					}

					char label[512];
					std::snprintf(label, sizeof(label), "%u: %s", entry.id(), entry.name().c_str());
					if (ImGui::Selectable(label, m_currentItem == i))
					{
						m_currentItem = i;
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleVar(2);
			}
			ImGui::EndChild();

			ImGui::SameLine();
			ImGui::BeginChild("##modelDetails", ImVec2(detailsWidth, 0), true);
			{
				if (m_currentItem >= 0 && m_currentItem < m_manager.count())
				{
					auto* currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
					DrawDetailsImpl(*currentEntry);
				}
				else
				{
					ImGui::TextDisabled("Select a model from the list.");
				}
			}
			ImGui::EndChild();

			ImGui::SameLine();
			ImGui::BeginChild("##modelPreview", ImVec2(0, 0), true);
			{
				DrawSectionHeader("Live Preview");
				proto::ModelDataEntry* currentEntry = nullptr;
				if (m_currentItem >= 0 && m_currentItem < m_manager.count())
				{
					currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
				}

				if (m_preview)
				{
					m_preview->Draw(currentEntry);
				}
			}
			ImGui::EndChild();
		}

		ImGui::End();
		return false;
	}

	void ModelEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (DrawPrimaryButton("Duplicate", ImVec2(140.0f, 0.0f)))
		{
			proto::ModelDataEntry* copied = m_project.models.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Display", ImGuiTreeNodeFlags_DefaultOpen))
		{
			CHECKBOX_FLAG_PROP(flags, "Customizable", model_data_flags::IsCustomizable);
			CHECKBOX_FLAG_PROP(flags, "Is Player Character", model_data_flags::IsPlayerCharacter);

			static const std::set<String> modelExtensions = { ".hmsh", ".char" };
			String modelFile = currentEntry.filename();
			if (AssetPickerWidget::Draw("File", modelFile, modelExtensions, nullptr, nullptr, 48.0f))
			{
				currentEntry.set_filename(modelFile);
				const Path p = modelFile;
				if (p.extension() == ".char")
				{
					currentEntry.set_flags(currentEntry.flags() | model_data_flags::IsCustomizable);
				}
				else
				{
					currentEntry.set_flags(currentEntry.flags() & ~model_data_flags::IsCustomizable);
				}
			}

			if (currentEntry.flags() & model_data_flags::IsCustomizable)
			{
				m_definition = AvatarDefinitionManager::Get().Load(currentEntry.filename());
				if (m_definition)
				{
					static const char* s_none = "(None)";

					// For each property group, show a dropdown to select the value
					for (const auto& property : *m_definition)
					{
						// Draw the property name
						switch (property->GetType())
						{
						case CharacterCustomizationPropertyType::VisibilitySet:
						{
							auto it = currentEntry.customizationproperties().find(property->GetId());
							const auto visibilityProp = static_cast<VisibilitySetPropertyGroup*>(property.get());

							const char* previewString = s_none;
							if (it != currentEntry.customizationproperties().end())
							{
								const int32 valueIndex = visibilityProp->GetPropertyValueIndex(it->second);
								if (valueIndex != -1)
								{
									previewString = visibilityProp->possibleValues[valueIndex].valueName.c_str();
								}
							}

							if (ImGui::BeginCombo(property->GetName().c_str(), previewString))
							{
								for (const auto& value : visibilityProp->possibleValues)
								{
									if (ImGui::Selectable(value.valueName.c_str()))
									{
										(*currentEntry.mutable_customizationproperties())[property->GetId()] = value.valueId;
									}
								}
								ImGui::EndCombo();
							}
							break;
						}
						case CharacterCustomizationPropertyType::MaterialOverride:
						{
							auto it = currentEntry.customizationproperties().find(property->GetId());
							const auto materialProp = static_cast<MaterialOverridePropertyGroup*>(property.get());

							const char* previewString = s_none;
							if (it != currentEntry.customizationproperties().end())
							{
								const int32 valueIndex = materialProp->GetPropertyValueIndex(it->second);
								if (valueIndex != -1)
								{
									previewString = materialProp->possibleValues[valueIndex].valueName.c_str();
								}
							}

							if (ImGui::BeginCombo(property->GetName().c_str(), previewString))
							{
								for (const auto& value : materialProp->possibleValues)
								{
									if (ImGui::Selectable(value.valueName.c_str()))
									{
										(*currentEntry.mutable_customizationproperties())[property->GetId()] = value.valueId;
									}
								}
								ImGui::EndCombo();
							}
							break;
						}
						}
					}

				}
			}
		}
	}

	void ModelEditorWindow::OnNewEntry(
		proto::TemplateManager<proto::ModelDatas, proto::ModelDataEntry>::EntryType& entry)
	{
		entry.set_filename(m_modelFiles[0].empty() ? "" : m_modelFiles[0].c_str());
	}

	void ModelEditorWindow::OnAssetImported(const Path& path)
	{
		if (path.extension() == ".hmsh" || path.extension() == ".char")
		{
			ReloadModelList();
		}
	}

	void ModelEditorWindow::ReloadModelList()
	{
		m_modelFiles.clear();

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for (const auto& filename : files)
		{
			if (filename.ends_with(".hmsh") || filename.ends_with(".char"))
			{
				m_modelFiles.push_back(filename);
			}
		}
	}
}
