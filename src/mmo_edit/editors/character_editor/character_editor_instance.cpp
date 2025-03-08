// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "character_editor.h"
#include "editor_host.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/skeleton_serializer.h"

namespace mmo
{
	CharacterEditorInstance::CharacterEditorInstance(EditorHost& host, CharacterEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_worldGrid->SetGridSize(1.0f);
		m_worldGrid->SetLargeGridInterval(5);
		m_worldGrid->SetRowCount(20);
		m_worldGrid->SetColumnCount(20);

		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
		m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());
		
		// TODO: Load
		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(m_assetPath.string());
		ASSERT(file);

		// Ensure file is loaded successfully
		io::StreamSource source{ *file };
		io::Reader reader{ source };
		m_avatarDefinition = std::make_shared<CustomizableAvatarDefinition>();
		if (!m_avatarDefinition->Read(reader))
		{
			ELOG("Failed to read customizable avatar definition from file " << m_assetPath);
		}

		// Load the mesh and entity to render
		UpdateBaseMesh();

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &CharacterEditorInstance::Render);
	}

	CharacterEditorInstance::~CharacterEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();

		m_scene.Clear();
	}

	void CharacterEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		if (m_animState && m_playAnimation)
		{
			m_animState->AddTime(ImGui::GetIO().DeltaTime);
		}

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
				
		m_scene.Render(*m_camera);
		
		m_viewportRT->Update();
	}

	void CharacterEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String previewId = "Preview##" + GetAssetPath().string();

		// Draw sidebar windows
		DrawDetails(detailsId);

		DrawPreview(previewId);

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
			ImGui::DockBuilderDockWindow(previewId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

		ImGui::PopID();
	}

	void CharacterEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void CharacterEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
		else if(button == 2)
		{
			m_middleButtonPressed = false;
		}
	}

	void CharacterEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		if (m_middleButtonPressed)
		{
			m_cameraAnchor->Translate(Vector3(0.0f, deltaY * 0.05f, 0.0f), TransformSpace::World);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void CharacterEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open character file " << GetAssetPath() << " for writing!");
			return;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };
		m_avatarDefinition->Serialize(writer);

		ILOG("Successfully saved character data");
	}

	void CharacterEditorInstance::DrawDetails(const String& id)
	{
		if (!m_avatarDefinition)
		{
			return;
		}

		if (ImGui::Begin(id.c_str()))
		{
			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			if (ImGui::BeginCombo("Mesh", m_avatarDefinition->GetBaseMesh().c_str()))
			{
				if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
				{
					ImGui::SetKeyboardFocusHere(0);
				}
				m_assetFilter.Draw("##asset_filter");

				const auto files = AssetRegistry::ListFiles();
				for (auto& file : files)
				{
					if (!file.ends_with(".hmsh"))
					{
						continue;
					}

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
						m_avatarDefinition->SetBaseMesh(file);
						UpdateBaseMesh();

						m_assetFilter.Clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					m_avatarDefinition->SetBaseMesh(*static_cast<String*>(payload->Data));
					UpdateBaseMesh();
				}

				ImGui::EndDragDropTarget();
			}

			// TODO: Draw properties
			if (ImGui::Button("Add Property"))
			{
				ImGui::OpenPopup("AddPropertyPopup");
			}

			if (ImGui::BeginPopup("AddPropertyPopup"))
			{
				static int selectedType = 0; // or store it in your class
				const char* typeLabels[] = { "MaterialOverride", "VisibilitySet", "ScalarParameter" };
				ImGui::Combo("Property Type", &selectedType, typeLabels, IM_ARRAYSIZE(typeLabels));

				// Possibly let the user choose an initial property name
				static char newPropName[64] = "New Property";
				ImGui::InputText("Name", newPropName, IM_ARRAYSIZE(newPropName));

				if (ImGui::Button("Create"))
				{
					std::unique_ptr<CustomizationPropertyGroup> newProp;
					switch (selectedType)
					{
					case 0: // MaterialOverride
						newProp = std::make_unique<MaterialOverridePropertyGroup>(m_avatarDefinition->GetNextPropertyId(), newPropName);
						break;
					case 1: // VisibilitySet
						newProp = std::make_unique<VisibilitySetPropertyGroup>(m_avatarDefinition->GetNextPropertyId(), newPropName);
						break;
					case 2: // ScalarParameter
						newProp = std::make_unique<ScalarParameterPropertyGroup>(m_avatarDefinition->GetNextPropertyId(), newPropName);
						break;
					}

					m_avatarDefinition->AddProperty(std::move(newProp));
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			int indexToRemove = -1;

			size_t counter = 0;
			for (auto& property : *m_avatarDefinition)
			{
				ImGui::PushID(static_cast<int>(counter));

				char propertyNameBuffer[128];
				std::snprintf(propertyNameBuffer, sizeof(propertyNameBuffer), "%s (%u)", property->GetName().c_str(), property->GetId());
				if (ImGui::CollapsingHeader(propertyNameBuffer, ImGuiTreeNodeFlags_DefaultOpen))
				{
					// Show a rename input
					char nameBuffer[64];
					std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", property->GetName().c_str());
					if (ImGui::InputText("Property Name", nameBuffer, IM_ARRAYSIZE(nameBuffer)))
					{
						property->SetName(nameBuffer);
					}

					// Show a "Remove" button
					if (ImGui::Button("Remove Property"))
					{
						indexToRemove = (int)counter;
					}

					// Now show type-specific fields
					DrawPropertyGroupDetails(*property);
				}

				ImGui::PopID();
				counter++;
			}

			if (indexToRemove != -1)
			{
				// remove outside the loop
				m_avatarDefinition->RemovePropertyByIndex(indexToRemove);
			}
		}
		ImGui::End();
	}

	void CharacterEditorInstance::DrawPropertyGroupDetails(VisibilitySetPropertyGroup& visProp)
	{
		// Edit the "subEntityTag" if you have one
		char tagBuffer[64];
		std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", visProp.subEntityTag.c_str());
		if (ImGui::InputText("Sub-Entity Tag", tagBuffer, IM_ARRAYSIZE(tagBuffer)))
		{
			visProp.subEntityTag = tagBuffer;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Possible Values:");

		// A button to add a new value
		if (ImGui::Button("Add Value"))
		{
			VisibilitySetValue newVal;
			newVal.valueId = visProp.idGenerator.GenerateId();
			newVal.valueName = "NewValue";  // default name
			newVal.visibleSubEntities = {};
			visProp.possibleValues.push_back(std::move(newVal));
		}

		// We need an index to remove an item if user clicks "Remove"
		int indexToRemove = -1;

		// Iterate possible values
		for (int i = 0; i < (int)visProp.possibleValues.size(); ++i)
		{
			auto& val = visProp.possibleValues[i];

			// Make a TreeNode so we can expand/collapse each value
			if (ImGui::TreeNode((void*)(intptr_t)i, "Value %d: %s (%u)", i, val.valueName.c_str(), val.valueId))
			{
				// Edit the valueId
				char idBuffer[64];
				std::snprintf(idBuffer, sizeof(idBuffer), "%s", val.valueName.c_str());
				if (ImGui::InputText("Value Name", idBuffer, IM_ARRAYSIZE(idBuffer)))
				{
					val.valueName = idBuffer;
				}

				// Edit the list of visibleSubEntities
				DrawVisibleSubEntityList(val.visibleSubEntities);

				// A remove button
				if (ImGui::Button("Remove This Value"))
				{
					indexToRemove = i;
				}

				ImGui::TreePop();
			}
		}

		// After the loop, if user clicked Remove, do it here
		if (indexToRemove >= 0)
		{
			visProp.possibleValues.erase(visProp.possibleValues.begin() + indexToRemove);
		}
	}

	void CharacterEditorInstance::DrawVisibleSubEntityList(std::vector<std::string>& visibleSubEntities)
	{
		// Let’s show each sub-entity in a list
		int removeIndex = -1;

		for (int j = 0; j < (int)visibleSubEntities.size(); ++j)
		{
			ImGui::PushID(j);
			// Show the sub-entity name in an InputText
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%s", visibleSubEntities[j].c_str());
			if (ImGui::InputText("Sub Entity", buf, IM_ARRAYSIZE(buf)))
			{
				visibleSubEntities[j] = buf;
			}

			ImGui::SameLine();
			if (ImGui::Button("Remove"))
			{
				removeIndex = j;
			}
			ImGui::PopID();
		}

		if (removeIndex >= 0)
		{
			visibleSubEntities.erase(visibleSubEntities.begin() + removeIndex);
		}

		// Button to add a new sub-entity name
		if (ImGui::Button("Add SubEntity"))
		{
			visibleSubEntities.push_back("NewSubEntity");
		}
	}

	void CharacterEditorInstance::DrawPropertyGroupDetails(MaterialOverridePropertyGroup& matProp)
	{
		ImGui::TextUnformatted("Material Override Values:");

		if (ImGui::Button("Add Value"))
		{
			MaterialOverrideValue newVal;
			newVal.valueId = matProp.idGenerator.GenerateId();
			newVal.valueName = "NewSkinColor"; // or something
			matProp.possibleValues.push_back(std::move(newVal));
		}

		int indexToRemove = -1;
		for (int i = 0; i < (int)matProp.possibleValues.size(); ++i)
		{
			auto& val = matProp.possibleValues[i];
			if (ImGui::TreeNode((void*)(intptr_t)i, "Value %d: %s (%u)", i, val.valueName.c_str(), val.valueId))
			{
				// Edit the valueId
				char valBuf[64];
				std::snprintf(valBuf, sizeof(valBuf), "%s", val.valueName.c_str());
				if (ImGui::InputText("Value ID", valBuf, IM_ARRAYSIZE(valBuf)))
				{
					val.valueName = valBuf;
				}

				// Now show the subEntity->material pairs
				DrawMaterialMap(val.subEntityToMaterial);

				if (ImGui::Button("Remove Value"))
				{
					indexToRemove = i;
				}

				ImGui::TreePop();
			}
		}

		if (indexToRemove >= 0)
		{
			matProp.possibleValues.erase(matProp.possibleValues.begin() + indexToRemove);
		}
	}

	void CharacterEditorInstance::DrawMaterialMap(std::unordered_map<std::string, std::string>& subEntityToMaterial)
	{
		// We'll just iterate a stable copy of keys. If you want to allow removing them,
		// you can do a separate approach with an ordered container or gather removal in a separate array.

		// For iteration, we can do:
		std::vector<std::string> keys;
		keys.reserve(subEntityToMaterial.size());
		for (const auto& kv : subEntityToMaterial)
			keys.push_back(kv.first);

		// Let's also store which key to remove if requested
		std::string keyToRemove;

		for (auto& key : keys)
		{
			ImGui::PushID(key.c_str());

			std::string& matRef = subEntityToMaterial[key];

			ImGui::Separator();

			char keyBuf[256];
			std::snprintf(keyBuf, sizeof(keyBuf), "%s", key.c_str());
			if (ImGui::InputText("Sub Entity", keyBuf, IM_ARRAYSIZE(keyBuf), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
			{
				keyToRemove = key;
				subEntityToMaterial[keyBuf] = matRef;
			}

			// We can let user rename the material reference
			char matBuf[128];
			std::snprintf(matBuf, sizeof(matBuf), "%s", matRef.c_str());
			if (ImGui::InputText("Material", matBuf, IM_ARRAYSIZE(matBuf)))
			{
				matRef = matBuf;
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmat"))
				{
					matRef = *static_cast<String*>(payload->Data);
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmi"))
				{
					matRef = *static_cast<String*>(payload->Data);
				}

				ImGui::EndDragDropTarget();
			}

			// Optionally let user rename the sub-entity key too,
			// but be careful about re-inserting. This can be tricky with a map.
			if (ImGui::Button("Remove Pair"))
			{
				keyToRemove = key;
			}

			ImGui::PopID();
		}

		if (!keyToRemove.empty())
		{
			subEntityToMaterial.erase(keyToRemove);
		}

		if (ImGui::Button("Add Pair"))
		{
			subEntityToMaterial["NewSubEntity"] = "Materials/Path/Default.hmi";
		}
	}

	void CharacterEditorInstance::DrawPreview(const String& id)
	{
		if (!m_avatarDefinition)
		{
			return;
		}

		static const char* s_none = "(None)";

		if (ImGui::Begin(id.c_str()))
		{
			// For each property group, show a dropdown to select the value
			for (const auto& property : *m_avatarDefinition)
			{

				// Draw the property name
				switch (property->GetType())
				{
				case CharacterCustomizationPropertyType::VisibilitySet:
				{
					auto it = m_configuration.chosenOptionPerGroup.find(property->GetId());
					const auto visibilityProp = static_cast<VisibilitySetPropertyGroup*>(property.get());

					const char* previewString = s_none;
					if (it != m_configuration.chosenOptionPerGroup.end())
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
								m_configuration.chosenOptionPerGroup[property->GetId()] = value.valueId;

								// Apply the visibility set
								// For each sub-entity in value.visibleSubEntities, set visibility
								// to true or false based on the value
								UpdatePreview();
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case CharacterCustomizationPropertyType::MaterialOverride:
				{
					auto it = m_configuration.chosenOptionPerGroup.find(property->GetId());
					const auto materialProp = static_cast<MaterialOverridePropertyGroup*>(property.get());

					const char* previewString = s_none;
					if (it != m_configuration.chosenOptionPerGroup.end())
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
								m_configuration.chosenOptionPerGroup[property->GetId()] = value.valueId;

								// Apply the visibility set
								// For each sub-entity in value.visibleSubEntities, set visibility
								// to true or false based on the value
								UpdatePreview();
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				case CharacterCustomizationPropertyType::ScalarParameter:
					// TODO :)
					break;
				}
			}
		}

		ImGui::End();
	}

	void CharacterEditorInstance::UpdateBaseMesh()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
			m_animState = nullptr;
			m_entity = nullptr;
		}

		if (!m_avatarDefinition->GetBaseMesh().empty())
		{
			m_entity = m_scene.CreateEntity(m_assetPath.string(), m_avatarDefinition->GetBaseMesh());
			m_scene.GetRootSceneNode().AttachObject(*m_entity);

			m_animState = m_entity->GetAnimationState("Idle");
			if (m_animState)
			{
				m_animState->SetLoop(true);
				m_animState->SetEnabled(true);
				m_animState->SetWeight(1.0f);
			}

			m_cameraAnchor->SetPosition(Vector3::UnitY * m_entity->GetBoundingRadius() * 0.5f);
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius());
		}
		else
		{
			WLOG("Avatar definition does not have a base mesh set up!");
			m_cameraNode->SetPosition(Vector3::UnitZ * 1.0f);
		}
	}

	void CharacterEditorInstance::UpdatePreview()
	{
		// Iterate through each property
		for (const auto& property : *m_avatarDefinition)
		{
			// Alright, implement the value
			m_configuration.Apply(*this, *m_avatarDefinition);
		}
	}

	void CharacterEditorInstance::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
			{
				ASSERT(m_entity->GetMesh()->GetSubMeshCount() == m_entity->GetNumSubEntities());

				SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity* subEntity = m_entity->GetSubEntity(i);
					ASSERT(subEntity);
					subEntity->SetVisible(false);
				}
			}
			
		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& subEntityName : value.visibleSubEntities)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(subEntityName)) 
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void CharacterEditorInstance::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(pair.first))
					{
						MaterialPtr material = MaterialManager::Get().Load(pair.second);
						if (material)
						{
							subEntity->SetMaterial(material);
						}						
					}
				}
			}
		}
	}

	void CharacterEditorInstance::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		// TODO
	}

	void CharacterEditorInstance::DrawPropertyGroupDetails(CustomizationPropertyGroup& propertyGroup)
	{
		switch (propertyGroup.GetType())
		{
		case CharacterCustomizationPropertyType::MaterialOverride:
		{
			auto& matProp = static_cast<MaterialOverridePropertyGroup&>(propertyGroup);
			// Show fields that are relevant to material override:
			// Possibly a list of sub-entity -> material pairs, or any other data
			DrawPropertyGroupDetails(matProp);

			break;
		}
		case CharacterCustomizationPropertyType::VisibilitySet:
		{
			auto& visProp = static_cast<VisibilitySetPropertyGroup&>(propertyGroup);

			// Possibly show multiple "values" if it's discrete. For each value in
			// visProp.possibleValues, show a sub-UI, etc.
			DrawPropertyGroupDetails(visProp);
			break;
		}
		case CharacterCustomizationPropertyType::ScalarParameter:
		{
			auto& scalarProp = static_cast<ScalarParameterPropertyGroup&>(propertyGroup);
			// Example: show min/max
			ImGui::DragFloat("Min", &scalarProp.minValue, 0.01f, -100.f, 100.f);
			ImGui::DragFloat("Max", &scalarProp.maxValue, 0.01f, -100.f, 100.f);
			break;
		}
		}
	}

	void CharacterEditorInstance::DrawViewport(const String& id)
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
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
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

			if (ImGui::IsItemHovered())
			{
				m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * 0.1f, TransformSpace::Local);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_leftButtonPressed = true;
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
			{
				m_middleButtonPressed = true;
			}

		}
		ImGui::End();
	}
}
