// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_display_editor_window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/item.h"
#include "game/spell.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ItemDisplayEditorWindow::ItemDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.itemDisplays, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Item Displays";

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for (const auto& filename : files)
		{
			if (filename.ends_with(".htex") && filename.starts_with("Interface/Icon"))
			{
				m_textures.push_back(filename);
			}
		}
	}

	void ItemDisplayEditorWindow::OnNewEntry(EntryType& entry)
	{
		EditorEntryWindowBase::OnNewEntry(entry);


	}

	void ItemDisplayEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate Display Data"))
		{
			proto::ItemDisplayEntry* copied = m_project.itemDisplays.add();
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
#define SLIDER_UNSIGNED_SUB_PROP(sub, name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.sub.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.sub.set_##name(value); \
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
#define CHECKBOX_ATTR_PROP(index, label, flags) \
	{ \
		bool value = (currentEntry.attributes(index) & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) | static_cast<uint32>(flags)); \
			else \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) & ~static_cast<uint32>(flags)); \
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

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
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

			if (!currentEntry.icon().empty())
			{
				if (!m_iconCache.contains(currentEntry.icon()))
				{
					m_iconCache[currentEntry.icon()] = TextureManager::Get().CreateOrRetrieve(currentEntry.icon());
				}

				if (const TexturePtr tex = m_iconCache[currentEntry.icon()])
				{
					ImGui::Image(tex->GetTextureObject(), ImVec2(64, 64));
				}
			}

			if (ImGui::BeginCombo("Icon", currentEntry.icon().c_str(), ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_textures.size(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_textures[i] == currentEntry.icon();
					const char* item_text = m_textures[i].c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_icon(item_text);
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".htex"))
				{
					currentEntry.set_icon(*static_cast<String*>(payload->Data));
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (ImGui::CollapsingHeader("Visuals"))
		{
			// Button to add a new variant
			if (ImGui::Button("Add Variant"))
			{
				auto* newVariant = currentEntry.add_variants();
				// Optionally initialize default values if needed
				newVariant->set_model(0);
			}

			// Iterate through each variant
			for (int i = 0; i < currentEntry.variants_size(); i++)
			{
				auto* variant = currentEntry.mutable_variants(i);

				const uint32_t model = variant->model();
				const auto* selectedModelEntry = m_project.models.getById(model);

				if (ImGui::TreeNode(variant, "Variant %d (Model: %s)", i, selectedModelEntry ? selectedModelEntry->name().c_str() : "(All)"))
				{
					// Button to remove this variant
					if (ImGui::Button("Remove Variant"))
					{
						currentEntry.mutable_variants()->DeleteSubrange(i, 1);
						ImGui::TreePop();
						break;
					}

					// Edit 'model' property
					{
						// Model dropdown
						if (ImGui::BeginCombo("Model", selectedModelEntry ? selectedModelEntry->name().c_str() : "(All)"))
						{
							if (ImGui::Selectable("(All)", model == 0))
							{
								variant->set_model(0);
							}
							if (model == 0)
							{
								ImGui::SetItemDefaultFocus();
							}

							for (int i = 0; i < m_project.models.getTemplates().entry_size(); i++)
							{
								const auto& modelEntry = m_project.models.getTemplates().entry(i);
								const bool isSelected = model == modelEntry.id();
								if (ImGui::Selectable(modelEntry.name().c_str(), isSelected))
								{
									variant->set_model(modelEntry.id());
								}
								if (isSelected)
								{
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}
					}

					// Managing string arrays: hide_by_tag, hide_by_name, show_by_tag, show_by_name
					auto ManageStringArray = [&currentEntry](const char* header, const std::function<google::protobuf::RepeatedPtrField<std::string>* ()>& fieldGetter, const char* addLabel)
						{
							if (ImGui::TreeNode(header))
							{
								auto* field = fieldGetter();
								const int count = field->size();
								for (int i = 0; i < count; i++)
								{
									char buf[256];
									std::string currentVal = field->Get(i);
									std::strncpy(buf, currentVal.c_str(), sizeof(buf));
									buf[sizeof(buf) - 1] = '\0';
									ImGui::PushID(i);
									if (ImGui::InputText("Value", buf, sizeof(buf)))
									{
										*field->Mutable(i) = buf;
									}
									ImGui::SameLine();
									if (ImGui::Button("Remove"))
									{
										field->DeleteSubrange(i, 1);
										ImGui::PopID();
										break;
									}
									ImGui::PopID();
								}
								if (ImGui::Button(addLabel))
								{
									field->Add("");
								}
								ImGui::TreePop();
							}
						};

					ManageStringArray("Hide By Tag", [&currentEntry, variant]() -> google::protobuf::RepeatedPtrField<std::string>* { return variant->mutable_hidden_by_tag(); }, "Add Hide Tag");
					ManageStringArray("Hide By Name", [&currentEntry, variant]() -> google::protobuf::RepeatedPtrField<std::string>* { return variant->mutable_hidden_by_name(); }, "Add Hide Name");
					ManageStringArray("Show By Tag", [&currentEntry, variant]() -> google::protobuf::RepeatedPtrField<std::string>* { return variant->mutable_shown_by_tag(); }, "Add Show Tag");
					ManageStringArray("Show By Name", [&currentEntry, variant]() -> google::protobuf::RepeatedPtrField<std::string>*{ return variant->mutable_shown_by_name(); }, "Add Show Name");

					if (ImGui::CollapsingHeader("Material Overrides"))
					{
						auto* overrides = variant->mutable_material_overrides();
						int index = 0;
						// Iterate through all existing overrides.
						for (auto it = overrides->begin(); it != overrides->end(); )
						{
							ImGui::PushID(index);
							// Display the subentity name (key) and allow editing the material (value)
							ImGui::Text("Subentity: %s", it->first.c_str());
							ImGui::SameLine();
							char valBuf[256];
							std::strncpy(valBuf, it->second.c_str(), sizeof(valBuf));
							valBuf[sizeof(valBuf) - 1] = '\0';
							if (ImGui::InputText("Material", valBuf, sizeof(valBuf)))
							{
								it->second = std::string(valBuf);
							}
							ImGui::SameLine();
							if (ImGui::Button("Remove"))
							{
								it = overrides->erase(it);
								ImGui::PopID();
								continue;
							}
							ImGui::PopID();
							++it;
							++index;
						}

						// Fields to add a new override.
						static char newKey[256] = "";
						static char newVal[256] = "";
						ImGui::InputText("New Subentity", newKey, sizeof(newKey));
						ImGui::InputText("New Material", newVal, sizeof(newVal));
						if (ImGui::Button("Add Override"))
						{
							if (newKey[0] != '\0')
							{
								// Prevent duplicate keys.
								if (overrides->find(newKey) == overrides->end())
								{
									(*overrides)[newKey] = newVal;
									newKey[0] = '\0';
									newVal[0] = '\0';
								}
								else
								{
									ImGui::TextColored(ImVec4(1, 0, 0, 1), "Duplicate subentity name!");
								}
							}
						}
					}

					// Edit 'mesh' property
					{
						char buf[512];
						std::string mesh = variant->mesh();
						std::strncpy(buf, mesh.c_str(), sizeof(buf));
						buf[sizeof(buf) - 1] = '\0';
						if (ImGui::InputText("Attached Mesh", buf, sizeof(buf)))
						{
							variant->set_mesh(std::string(buf));
						}
					}

					if (variant->has_mesh() && !variant->mesh().empty())
					{
						// Lambda to draw bone attachment properties
						auto DrawBoneAttachment = [](const char* label, auto* att)
							{
								if (ImGui::TreeNode(label))
								{
									{
										char boneBuf[256];
										std::string boneName = att->bone_name();
										std::strncpy(boneBuf, boneName.c_str(), sizeof(boneBuf));
										boneBuf[sizeof(boneBuf) - 1] = '\0';
										if (ImGui::InputText("Bone Name", boneBuf, sizeof(boneBuf)))
										{
											att->set_bone_name(std::string(boneBuf));
										}
									}
									{
										float offset = att->offset_x();
										if (ImGui::InputFloat("Offset X", &offset))
											att->set_offset_x(offset);
									}
									{
										float offset = att->offset_y();
										if (ImGui::InputFloat("Offset Y", &offset))
											att->set_offset_y(offset);
									}
									{
										float offset = att->offset_z();
										if (ImGui::InputFloat("Offset Z", &offset))
											att->set_offset_z(offset);
									}
									{
										float rot = att->rotation_w();
										if (ImGui::InputFloat("Rotation W", &rot))
											att->set_rotation_w(rot);
									}
									{
										float rot = att->rotation_x();
										if (ImGui::InputFloat("Rotation X", &rot))
											att->set_rotation_x(rot);
									}
									{
										float rot = att->rotation_y();
										if (ImGui::InputFloat("Rotation Y", &rot))
											att->set_rotation_y(rot);
									}
									{
										float rot = att->rotation_z();
										if (ImGui::InputFloat("Rotation Z", &rot))
											att->set_rotation_z(rot);
									}
									{
										float scale = att->scale_x();
										if (ImGui::InputFloat("Scale X", &scale))
											att->set_scale_x(scale);
									}
									{
										float scale = att->scale_y();
										if (ImGui::InputFloat("Scale Y", &scale))
											att->set_scale_y(scale);
									}
									{
										float scale = att->scale_z();
										if (ImGui::InputFloat("Scale Z", &scale))
											att->set_scale_z(scale);
									}
									ImGui::TreePop();
								}
							};

						// Draw bone attachment sections
						DrawBoneAttachment("Attached Bone Default", variant->mutable_attached_bone_default());
						DrawBoneAttachment("Attached Bone Drawn", variant->mutable_attached_bone_drawn());
						DrawBoneAttachment("Attached Bone Sheath", variant->mutable_attached_bone_sheath());
					}
					
					ImGui::TreePop();
				}
			}
		}
	}
}
