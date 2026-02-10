
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_display_editor_window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstring>

#include "asset_picker_widget.h"
#include "assets/asset_registry.h"
#include "editor_imgui_helpers.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/tag_point.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/axis_display.h"
#include "stream_source.h"

namespace mmo
{
	namespace
	{
		void DrawStringArrayEditor(const char* id, google::protobuf::RepeatedPtrField<std::string>& values, const char* addLabel, const char* valueLabel = "Value")
		{
			if (!ImGui::TreeNode(id, "%s (%d)", id, values.size())) return;
			int removeIndex = -1;
			for (int i = 0; i < values.size(); ++i)
			{
				ImGui::PushID(i);
				char buffer[256];
				std::strncpy(buffer, values.Get(i).c_str(), sizeof(buffer));
				buffer[sizeof(buffer) - 1] = '\0';
				ImGui::SetNextItemWidth(-90.0f);
				if (ImGui::InputText(valueLabel, buffer, sizeof(buffer))) *values.Mutable(i) = buffer;
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) removeIndex = i;
				ImGui::PopID();
			}
			if (removeIndex >= 0) values.DeleteSubrange(removeIndex, 1);
			if (ImGui::Button(addLabel)) values.Add("");
			ImGui::TreePop();
		}

		void DrawBoneAttachmentEditor(const char* title, proto::ItemDisplayBoneAttachment* att)
		{
			if (!ImGui::TreeNode(title)) return;
			char boneBuf[256];
			std::strncpy(boneBuf, att->bone_name().c_str(), sizeof(boneBuf));
			boneBuf[sizeof(boneBuf) - 1] = '\0';
			if (ImGui::InputText("Bone Name", boneBuf, sizeof(boneBuf))) att->set_bone_name(boneBuf);

			float offset[3] = { att->offset_x(), att->offset_y(), att->offset_z() };
			if (ImGui::InputFloat3("Offset", offset))
			{
				att->set_offset_x(offset[0]); att->set_offset_y(offset[1]); att->set_offset_z(offset[2]);
			}

			float rotation[4] = { att->rotation_w(), att->rotation_x(), att->rotation_y(), att->rotation_z() };
			Quaternion rotationQuat(rotation[0], rotation[1], rotation[2], rotation[3]);
			if (DrawQuaternionEulerDegreesControl("Rotation (Degrees)", rotationQuat))
			{
				rotation[0] = rotationQuat.w;
				rotation[1] = rotationQuat.x;
				rotation[2] = rotationQuat.y;
				rotation[3] = rotationQuat.z;
				att->set_rotation_w(rotation[0]); att->set_rotation_x(rotation[1]); att->set_rotation_y(rotation[2]); att->set_rotation_z(rotation[3]);
			}
			ImGui::BeginDisabled(true);
			ImGui::InputFloat4("Quaternion (wxyz)", rotation);
			ImGui::EndDisabled();

			float scale[3] = { att->scale_x(), att->scale_y(), att->scale_z() };
			if (ImGui::InputFloat3("Scale", scale))
			{
				att->set_scale_x(scale[0]); att->set_scale_y(scale[1]); att->set_scale_z(scale[2]);
			}

			if (ImGui::Button("Reset Transform"))
			{
				att->set_offset_x(0.0f); att->set_offset_y(0.0f); att->set_offset_z(0.0f);
				att->set_rotation_w(1.0f); att->set_rotation_x(0.0f); att->set_rotation_y(0.0f); att->set_rotation_z(0.0f);
				att->set_scale_x(1.0f); att->set_scale_y(1.0f); att->set_scale_z(1.0f);
			}
			ImGui::TreePop();
		}

		struct PreviewAttachment
		{
			Entity* entity{ nullptr };
			TagPoint* tagPoint{ nullptr };
		};
	}

	class ItemDisplayPreview final : public CustomizationPropertyGroupApplier
	{
	public:
		explicit ItemDisplayPreview(EditorHost& host)
			: m_host(host)
		{
			m_cameraAnchor = &m_scene.CreateSceneNode("ItemDisplayPreviewCameraAnchor");
			m_cameraNode = &m_scene.CreateSceneNode("ItemDisplayPreviewCameraNode");
			m_cameraAnchor->AddChild(*m_cameraNode);
			m_camera = m_scene.CreateCamera("ItemDisplayPreviewCamera");
			m_cameraNode->AttachObject(*m_camera);
			m_cameraNode->SetPosition(Vector3(0.0f, 1.8f, 6.0f));
			m_cameraAnchor->SetOrientation(Quaternion(Degree(-12.0f), Vector3::UnitX));
			m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

			m_worldGrid = std::make_unique<WorldGrid>(m_scene, "ItemDisplayPreviewGrid");
			m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "ItemDisplayPreviewAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());
			m_renderConnection = m_host.beforeUiUpdate.connect(this, &ItemDisplayPreview::Render);
		}

		~ItemDisplayPreview() override
		{
			ClearAttachments();
			if (m_entity)
			{
				m_scene.DestroyEntity(*m_entity);
				m_entity = nullptr;
			}
			m_worldGrid.reset();
			m_axisDisplay.reset();
			m_scene.Clear();
		}

		void Draw(proto::ItemDisplayEntry* entry, PreviewProviderManager& previewManager, Configuration& config, const proto::Project& project)
		{
			if (m_characterAssetPath.empty() && !config.itemDisplayPreviewCharacter.empty())
			{
				m_characterAssetPath = config.itemDisplayPreviewCharacter;
				m_rebuildRequested = true;
			}

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

			DrawSectionHeader("Preview Source");
			static const std::set<String> charExtensions = { ".char" };
			String previewCharacter = m_characterAssetPath;
			if (AssetPickerWidget::Draw("Character (.char)", previewCharacter, charExtensions, &previewManager, nullptr, 0.0f))
			{
				m_characterAssetPath = previewCharacter;
				config.itemDisplayPreviewCharacter = previewCharacter;
				if (!config.configFilePath.empty()) config.Save(config.configFilePath);
				m_rebuildRequested = true;
			}
			ImGui::SameLine();
			DrawHelpMarker("Default preview character used by this editor. Saved to editor config.");

			static const std::set<String> meshExtensions = { ".hmsh" };
			String meshOverride = m_meshOverridePath;
			if (AssetPickerWidget::Draw("Preview Mesh Override", meshOverride, meshExtensions, &previewManager, nullptr, 32.0f))
			{
				m_meshOverridePath = meshOverride;
				m_rebuildRequested = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear Override"))
			{
				m_meshOverridePath.clear();
				m_rebuildRequested = true;
			}

			if (ImGui::Checkbox("Use Model Override", &m_useModelOverride)) m_rebuildRequested = true;
			if (m_useModelOverride)
			{
				const auto* selectedModel = project.models.getById(m_modelOverrideId);
				if (ImGui::BeginCombo("Model Override", selectedModel ? selectedModel->name().c_str() : "(None)"))
				{
					for (int i = 0; i < project.models.count(); ++i)
					{
						const auto& modelEntry = project.models.getTemplates().entry(i);
						const bool selected = m_modelOverrideId == modelEntry.id();
						if (ImGui::Selectable(modelEntry.name().c_str(), selected))
						{
							m_modelOverrideId = modelEntry.id();
							m_rebuildRequested = true;
						}
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				const auto* effectiveModel = project.models.getById(m_effectiveModelId);
				ImGui::TextDisabled("Auto model: %s", effectiveModel ? effectiveModel->name().c_str() : "(All)");
			}

			if (ImGui::Button("Reload Preview", ImVec2(140.0f, 0.0f))) m_rebuildRequested = true;
			RebuildPreviewIfNeeded(project);
			ImGui::Separator();
			DrawViewport();

			ImGui::PopStyleVar(2);
		}

	private:
		void RebuildPreviewIfNeeded(const proto::Project& project);
		void ApplyPreview();
		void ClearAttachments();
		void DrawViewport();
		void Render();
	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override
		{
			if (!m_entity) return;
			if (!group.subEntityTag.empty())
			{
				for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(i);
					if (subMesh.HasTag(group.subEntityTag))
					{
						if (SubEntity* subEntity = m_entity->GetSubEntity(i)) subEntity->SetVisible(false);
					}
				}
			}
			const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
			if (it == configuration.chosenOptionPerGroup.end()) return;
			for (const auto& value : group.possibleValues)
			{
				if (value.valueId != it->second) continue;
				for (const auto& subEntityName : value.visibleSubEntities)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(subEntityName)) subEntity->SetVisible(true);
				}
			}
		}

		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override
		{
			if (!m_entity) return;
			const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
			if (it == configuration.chosenOptionPerGroup.end()) return;
			for (const auto& value : group.possibleValues)
			{
				if (value.valueId != it->second) continue;
				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(pair.first))
					{
						if (MaterialPtr material = MaterialManager::Get().Load(pair.second)) subEntity->SetMaterial(material);
					}
				}
			}
		}

		void Apply(const ScalarParameterPropertyGroup&, const AvatarConfiguration&) override
		{
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
		std::vector<PreviewAttachment> m_attachments;

		String m_characterAssetPath;
		String m_meshOverridePath;
		std::shared_ptr<CustomizableAvatarDefinition> m_avatarDefinition;
		AvatarConfiguration m_avatarConfiguration;

		bool m_useModelOverride{ false };
		uint32 m_modelOverrideId{ 0 };
		uint32 m_effectiveModelId{ 0 };

		proto::ItemDisplayEntry* m_currentEntry{ nullptr };
		String m_lastEntrySerialized;
		bool m_rebuildRequested{ true };
	};

	void ItemDisplayPreview::RebuildPreviewIfNeeded(const proto::Project& project)
	{
		if (!m_rebuildRequested) return;
		m_rebuildRequested = false;
		ClearAttachments();
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
			m_entity = nullptr;
		}

		m_avatarDefinition.reset();
		m_avatarConfiguration = AvatarConfiguration();
		m_effectiveModelId = 0;

		String meshPath;
		if (!m_meshOverridePath.empty())
		{
			meshPath = m_meshOverridePath;
		}
		else if (!m_characterAssetPath.empty())
		{
			std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(m_characterAssetPath);
			if (!file) return;
			io::StreamSource source{ *file };
			io::Reader reader{ source };
			m_avatarDefinition = std::make_shared<CustomizableAvatarDefinition>();
			if (!m_avatarDefinition->Read(reader))
			{
				m_avatarDefinition.reset();
				return;
			}
			meshPath = m_avatarDefinition->GetBaseMesh();
			if (meshPath.empty())
			{
				m_avatarDefinition.reset();
				return;
			}

			for (int i = 0; i < project.models.count(); ++i)
			{
				const auto& modelEntry = project.models.getTemplates().entry(i);
				if (modelEntry.filename() == meshPath)
				{
					m_effectiveModelId = modelEntry.id();
					break;
				}
			}

			for (const auto& property : *m_avatarDefinition)
			{
				switch (property->GetType())
				{
				case CharacterCustomizationPropertyType::VisibilitySet:
				{
					auto* vis = static_cast<VisibilitySetPropertyGroup*>(property.get());
					if (!vis->possibleValues.empty()) m_avatarConfiguration.chosenOptionPerGroup[property->GetId()] = vis->possibleValues.front().valueId;
					break;
				}
				case CharacterCustomizationPropertyType::MaterialOverride:
				{
					auto* mat = static_cast<MaterialOverridePropertyGroup*>(property.get());
					if (!mat->possibleValues.empty()) m_avatarConfiguration.chosenOptionPerGroup[property->GetId()] = mat->possibleValues.front().valueId;
					break;
				}
				default:
					break;
				}
			}
		}
		else
		{
			return;
		}

		m_entity = m_scene.CreateEntity("ItemDisplayPreviewEntity", meshPath);
		m_scene.GetRootSceneNode().AttachObject(*m_entity);
		const float radius = std::max(1.0f, m_entity->GetBoundingRadius());
		m_cameraAnchor->SetPosition(Vector3::UnitY * radius * 0.5f);
		m_cameraNode->SetPosition(Vector3(0.0f, radius * 0.2f, radius * 2.2f));
		ApplyPreview();
	}

	void ItemDisplayPreview::ApplyPreview()
	{
		if (!m_entity) return;
		m_entity->ResetSubEntities();
		ClearAttachments();
		if (m_avatarDefinition) m_avatarConfiguration.Apply(*this, *m_avatarDefinition);
		if (!m_currentEntry) return;

		const uint32 modelId = m_useModelOverride ? m_modelOverrideId : m_effectiveModelId;
		int attachmentCounter = 0;
		for (const auto& variant : m_currentEntry->variants())
		{
			if (variant.model() != 0 && variant.model() != modelId) continue;

			for (const auto& subEntity : variant.hidden_by_name()) if (auto* sub = m_entity->GetSubEntity(subEntity)) sub->SetVisible(false);
			for (const auto& tag : variant.hidden_by_tag())
			{
				for (uint16 j = 0; j < m_entity->GetNumSubEntities(); ++j)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(j);
					if (subMesh.HasTag(tag)) if (SubEntity* sub = m_entity->GetSubEntity(j)) sub->SetVisible(false);
				}
			}

			for (const auto& subEntity : variant.shown_by_name()) if (auto* sub = m_entity->GetSubEntity(subEntity)) sub->SetVisible(true);
			for (const auto& tag : variant.shown_by_tag())
			{
				for (uint16 j = 0; j < m_entity->GetNumSubEntities(); ++j)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(j);
					if (subMesh.HasTag(tag)) if (SubEntity* sub = m_entity->GetSubEntity(j)) sub->SetVisible(true);
				}
			}

			for (const auto& materialOverride : variant.material_overrides())
			{
				if (auto* sub = m_entity->GetSubEntity(materialOverride.first))
				{
					if (auto mat = MaterialManager::Get().Load(materialOverride.second)) sub->SetMaterial(mat);
				}
			}

			if (variant.has_mesh() && !variant.mesh().empty() && variant.has_attached_bone_default())
			{
				if (m_entity->GetSkeleton() && m_entity->GetSkeleton()->HasBone(variant.attached_bone_default().bone_name()))
				{
					PreviewAttachment attachment;
					attachment.entity = m_scene.CreateEntity(m_entity->GetName() + "_ITEM_" + std::to_string(attachmentCounter++), variant.mesh());
					attachment.tagPoint = m_entity->AttachObjectToBone(variant.attached_bone_default().bone_name(), *attachment.entity);
					if (attachment.tagPoint)
					{
						attachment.tagPoint->SetPosition(Vector3(variant.attached_bone_default().offset_x(), variant.attached_bone_default().offset_y(), variant.attached_bone_default().offset_z()));
						attachment.tagPoint->SetOrientation(Quaternion(variant.attached_bone_default().rotation_w(), variant.attached_bone_default().rotation_x(), variant.attached_bone_default().rotation_y(), variant.attached_bone_default().rotation_z()));
						attachment.tagPoint->SetScale(Vector3(variant.attached_bone_default().scale_x(), variant.attached_bone_default().scale_y(), variant.attached_bone_default().scale_z()));
					}
					m_attachments.push_back(attachment);
				}
			}
		}
	}
	void ItemDisplayPreview::ClearAttachments()
	{
		if (!m_entity)
		{
			m_attachments.clear();
			return;
		}
		for (const auto& attachment : m_attachments)
		{
			if (attachment.entity)
			{
				const String entityName = attachment.entity->GetName();
				m_entity->DetachObjectFromBone(entityName);
				m_scene.DestroyEntity(*attachment.entity);
			}
		}
		m_attachments.clear();
	}

	void ItemDisplayPreview::DrawViewport()
	{
		const ImVec2 availableSpace = ImGui::GetContentRegionAvail();
		if (m_viewportRT == nullptr)
		{
			m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("ItemDisplayPreviewViewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y), RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
			m_viewportSize = availableSpace;
		}
		else if (m_viewportSize.x != availableSpace.x || m_viewportSize.y != availableSpace.y)
		{
			m_viewportRT->Resize(std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
			m_viewportSize = availableSpace;
		}

		ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
		ImGui::SetItemUsingMouseWheel();

		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			// Mouse wheel: zoom in/out.
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				m_cameraNode->Translate(Vector3::UnitZ * -wheel * 0.35f, TransformSpace::Local);
			}

			// Left-drag: orbit camera.
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
				ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
				m_cameraAnchor->Yaw(Degree(-delta.x * 0.25f), TransformSpace::World);
				m_cameraAnchor->Pitch(Degree(-delta.y * 0.25f), TransformSpace::Local);
			}

			// Right-drag: pan camera anchor.
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
			{
				const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
				ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
				m_cameraAnchor->Translate(Vector3(-delta.x * 0.01f, delta.y * 0.01f, 0.0f), TransformSpace::World);
			}
		}
	}

	void ItemDisplayPreview::Render()
	{
		if (!m_viewportRT || m_viewportSize.x <= 0.0f || m_viewportSize.y <= 0.0f) return;
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

	ItemDisplayEditorWindow::ItemDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host, PreviewProviderManager& previewManager, Configuration& config)
		: EditorEntryWindowBase(project, project.itemDisplays, name)
		, m_host(host)
		, m_previewManager(previewManager)
		, m_config(config)
	{
		EditorWindowBase::SetVisible(false);
		m_hasToolbarButton = false;
		m_toolbarButtonText = "Item Displays";
		m_preview = std::make_unique<ItemDisplayPreview>(host);
	}

	ItemDisplayEditorWindow::~ItemDisplayEditorWindow() = default;

	void ItemDisplayEditorWindow::OnNewEntry(EntryType& entry)
	{
		EditorEntryWindowBase::OnNewEntry(entry);
		if (entry.name().empty()) entry.set_name("New Item Display");
	}

	bool ItemDisplayEditorWindow::Draw()
	{
		if (!m_visible) return false;
		ImGui::SetNextWindowSize(ImVec2(1400, 860), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(m_name.c_str(), &m_visible, ImGuiWindowFlags_MenuBar))
		{
			const float windowWidth = ImGui::GetContentRegionAvail().x;
			const float listWidth = 320.0f;
			const float previewWidth = std::max(windowWidth * 0.38f, 470.0f);
			const float detailsWidth = windowWidth - listWidth - previewWidth - 16.0f;

			ImGui::BeginChild("##itemDisplayList", ImVec2(listWidth, 0), true);
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
				if (ImGui::Button("+ Add New", ImVec2(-1, 0)))
				{
					auto* entry = m_manager.add();
					OnNewEntry(*entry);
					m_currentItem = m_manager.count() - 1;
				}
				ImGui::PopStyleColor();

				ImGui::BeginDisabled(m_currentItem < 0 || m_currentItem >= m_manager.count());
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
				if (ImGui::Button("Remove Selected", ImVec2(-1, 0)))
				{
					m_manager.remove(m_manager.getTemplates().entry().at(m_currentItem).id());
					m_currentItem = -1;
				}
				ImGui::PopStyleColor();
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

				ImGui::BeginChild("##itemDisplayListScroll", ImVec2(0, 0));
				for (int i = 0; i < m_manager.count(); ++i)
				{
					const auto& entry = m_manager.getTemplates().entry().at(i);
					if (!lowerSearch.empty())
					{
						std::string lowerName = entry.name();
						std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						std::string idString = std::to_string(entry.id());
						if (lowerName.find(lowerSearch) == std::string::npos && idString.find(lowerSearch) == std::string::npos) continue;
					}

					char label[512];
					std::snprintf(label, sizeof(label), "%u: %s", entry.id(), entry.name().c_str());
					if (ImGui::Selectable(label, m_currentItem == i)) m_currentItem = i;
				}
				ImGui::EndChild();
				ImGui::PopStyleVar(2);
			}
			ImGui::EndChild();

			ImGui::SameLine();
			ImGui::BeginChild("##itemDisplayDetails", ImVec2(detailsWidth, 0), true);
			{
				if (m_currentItem >= 0 && m_currentItem < m_manager.count())
				{
					auto* currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
					DrawDetailsImpl(*currentEntry);
				}
				else ImGui::TextDisabled("Select an item display from the list.");
			}
			ImGui::EndChild();

			ImGui::SameLine();
			ImGui::BeginChild("##itemDisplayPreview", ImVec2(0, 0), true);
			{
				DrawSectionHeader("Live Preview");
				proto::ItemDisplayEntry* currentEntry = nullptr;
				if (m_currentItem >= 0 && m_currentItem < m_manager.count()) currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
				if (m_preview) m_preview->Draw(currentEntry, m_previewManager, m_config, m_project);
			}
			ImGui::EndChild();
		}
		ImGui::End();
		return false;
	}
	void ItemDisplayEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		if (ImGui::Button("Duplicate Display Data", ImVec2(180.0f, 0.0f)))
		{
			proto::ItemDisplayEntry* copied = m_project.itemDisplays.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
		DrawHelpMarker("Creates a full copy of this item display with a new id");

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
			DrawSectionHeader("Identity");
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn()) ImGui::InputText("Name", currentEntry.mutable_name());
				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}
				ImGui::EndTable();
			}
			DrawSectionHeader("Icon");
			static const std::set<String> iconExtensions = { ".htex", ".blp" };
			String icon = currentEntry.icon();
			if (AssetPickerWidget::Draw("Icon", icon, iconExtensions, &m_previewManager, nullptr, 64.0f)) currentEntry.set_icon(icon);
			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Variants", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
			DrawSectionHeader("Workflow");
			if (ImGui::Button("Add SubEntity Variant", ImVec2(180.0f, 0.0f))) { auto* newVariant = currentEntry.add_variants(); newVariant->set_model(0); }
			ImGui::SameLine();
			if (ImGui::Button("Add Attachment Variant", ImVec2(180.0f, 0.0f))) { auto* newVariant = currentEntry.add_variants(); newVariant->set_model(0); }
			ImGui::SameLine();
			if (ImGui::Button("Duplicate Last Variant", ImVec2(180.0f, 0.0f)) && currentEntry.variants_size() > 0)
			{
				auto* newVariant = currentEntry.add_variants();
				newVariant->CopyFrom(currentEntry.variants(currentEntry.variants_size() - 1));
			}

			ImGui::Spacing();
			ImGui::TextDisabled("%d variant(s) configured", currentEntry.variants_size());
			static ImGuiTextFilter modelFilter;

			for (int i = 0; i < currentEntry.variants_size(); ++i)
			{
				auto* variant = currentEntry.mutable_variants(i);
				const auto* selectedModelEntry = m_project.models.getById(variant->model());
				const bool isAttachmentVariant = variant->has_mesh() && !variant->mesh().empty();
				if (ImGui::TreeNode(variant, "Variant %d [%s] (Model: %s)", i + 1, isAttachmentVariant ? "Attachment" : "SubEntity", selectedModelEntry ? selectedModelEntry->name().c_str() : "(All)"))
				{
					bool removeVariant = false, duplicateVariant = false, moveVariantUp = false, moveVariantDown = false;
					if (ImGui::Button("Remove Variant", ImVec2(150.0f, 0.0f))) removeVariant = true;
					ImGui::SameLine();
					if (ImGui::Button("Duplicate Variant", ImVec2(150.0f, 0.0f))) duplicateVariant = true;
					ImGui::SameLine();
					ImGui::BeginDisabled(i == 0); if (ImGui::SmallButton("Move Up")) moveVariantUp = true; ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(i == currentEntry.variants_size() - 1); if (ImGui::SmallButton("Move Down")) moveVariantDown = true; ImGui::EndDisabled();

					DrawSectionHeader("Target Model");
					if (ImGui::BeginCombo("Model", selectedModelEntry ? selectedModelEntry->name().c_str() : "(All)"))
					{
						modelFilter.Draw("Filter Models");
						ImGui::Separator();
						if (ImGui::Selectable("(All)", variant->model() == 0)) variant->set_model(0);
						for (int modelIndex = 0; modelIndex < m_project.models.getTemplates().entry_size(); ++modelIndex)
						{
							const auto& modelEntry = m_project.models.getTemplates().entry(modelIndex);
							if (modelFilter.IsActive() && !modelFilter.PassFilter(modelEntry.name().c_str())) continue;
							if (ImGui::Selectable(modelEntry.name().c_str(), variant->model() == modelEntry.id())) variant->set_model(modelEntry.id());
						}
						ImGui::EndCombo();
					}

					DrawSectionHeader("SubEntity Rules");
					DrawStringArrayEditor("Hide By Tag", *variant->mutable_hidden_by_tag(), "Add Hide Tag", "Tag");
					DrawStringArrayEditor("Hide By Name", *variant->mutable_hidden_by_name(), "Add Hide Name", "SubEntity");
					DrawStringArrayEditor("Show By Tag", *variant->mutable_shown_by_tag(), "Add Show Tag", "Tag");
					DrawStringArrayEditor("Show By Name", *variant->mutable_shown_by_name(), "Add Show Name", "SubEntity");

					if (ImGui::CollapsingHeader("Material Overrides", ImGuiTreeNodeFlags_DefaultOpen))
					{
						auto* overrides = variant->mutable_material_overrides();
						std::string removeKey;
						int row = 0;
						for (auto it = overrides->begin(); it != overrides->end(); ++it, ++row)
						{
							ImGui::PushID(row);
							char keyBuf[256];
							std::strncpy(keyBuf, it->first.c_str(), sizeof(keyBuf));
							keyBuf[sizeof(keyBuf) - 1] = '\0';
							if (ImGui::InputText("SubEntity", keyBuf, sizeof(keyBuf)))
							{
								if (std::string(keyBuf) != it->first && keyBuf[0] != '\0') { (*overrides)[keyBuf] = it->second; removeKey = it->first; }
							}
							static const std::set<String> materialExtensions = { ".hmat", ".hmi" };
							std::string material = it->second;
							if (AssetPickerWidget::Draw("Material", material, materialExtensions, &m_previewManager, nullptr, 32.0f)) it->second = material;
							if (ImGui::SmallButton("Remove Override")) removeKey = it->first;
							ImGui::PopID();
						}
						static char newSubEntity[256] = "";
						ImGui::InputText("New SubEntity", newSubEntity, sizeof(newSubEntity));
						static std::string newMaterial;
						static const std::set<String> materialExtensions = { ".hmat", ".hmi" };
						AssetPickerWidget::Draw("New Material", newMaterial, materialExtensions, &m_previewManager, nullptr, 32.0f);
						if (ImGui::Button("Add Override") && newSubEntity[0] != '\0') { (*overrides)[newSubEntity] = newMaterial; newSubEntity[0] = '\0'; newMaterial.clear(); }
						if (!removeKey.empty()) overrides->erase(removeKey);
					}

					DrawSectionHeader("Attachment");
					static const std::set<String> meshExtensions = { ".hmsh" };
					std::string attachedMesh = variant->mesh();
					if (AssetPickerWidget::Draw("Attached Mesh", attachedMesh, meshExtensions, &m_previewManager, nullptr, 48.0f)) variant->set_mesh(attachedMesh);
					if (variant->has_mesh() && !variant->mesh().empty())
					{
						DrawBoneAttachmentEditor("Attached Bone Default", variant->mutable_attached_bone_default());
						DrawBoneAttachmentEditor("Attached Bone Drawn", variant->mutable_attached_bone_drawn());
						DrawBoneAttachmentEditor("Attached Bone Sheath", variant->mutable_attached_bone_sheath());
					}

					DrawSectionHeader("Validation");
					if (variant->hidden_by_name_size() + variant->hidden_by_tag_size() + variant->shown_by_name_size() + variant->shown_by_tag_size() + variant->material_overrides_size() == 0 && variant->mesh().empty())
						ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "This variant has no effect yet.");
					if (!variant->mesh().empty() && (!variant->has_attached_bone_default() || variant->attached_bone_default().bone_name().empty()))
						ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Attachment variant needs `Attached Bone Default -> Bone Name`.");

					ImGui::TreePop();
					if (removeVariant) { currentEntry.mutable_variants()->DeleteSubrange(i, 1); break; }
					if (duplicateVariant) { auto* newVariant = currentEntry.add_variants(); newVariant->CopyFrom(*variant); break; }
					if (moveVariantUp) { currentEntry.mutable_variants()->SwapElements(i, i - 1); break; }
					if (moveVariantDown) { currentEntry.mutable_variants()->SwapElements(i, i + 1); break; }
				}
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}
	}
}
