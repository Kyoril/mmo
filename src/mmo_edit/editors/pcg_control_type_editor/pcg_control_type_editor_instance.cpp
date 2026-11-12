// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_control_type_editor_instance.h"

#include "pcg_control_type_editor.h"

#include "assets/asset_registry.h"
#include "editors/world_editor/edit_modes/procedural_edit_mode.h"
#include "editors/world_editor/pcg/control_details.h"
#include "editors/world_editor/world_editor_instance.h"
#include "icon_font.h"
#include "log/default_log_levels.h"

#include <imgui.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace mmo
{
	namespace
	{
		using pcg::graph::ControlShape;
		using pcg::graph::PropertyDef;
		using pcg::graph::PropertyGroup;
		using pcg::graph::PropertyType;

		const ImVec4 s_errorColor(1.0f, 0.35f, 0.35f, 1.0f);

		const char* const s_shapeNames[] = { "Closed Loop (area)", "Open Spline (path)", "Volume (box)", "Point", "Rectangle (area)" };
		const char* const s_propertyTypeNames[] = { "Float", "Int", "Bool", "Enum", "String", "Asset", "AssetList", "Color" };

		String Trim(const String& text)
		{
			const size_t first = text.find_first_not_of(" \t");
			if (first == String::npos)
			{
				return {};
			}
			return text.substr(first, text.find_last_not_of(" \t") - first + 1);
		}

		/// "a, b, c" -> {a, b, c}; empty entries are dropped.
		std::set<String> ParseTags(const String& text)
		{
			std::set<String> tags;
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find(',', start);
				if (end == String::npos)
				{
					end = text.size();
				}
				const String tag = Trim(text.substr(start, end - start));
				if (!tag.empty())
				{
					tags.insert(tag);
				}
				start = end + 1;
			}
			return tags;
		}

		String JoinTags(const std::set<String>& tags)
		{
			String text;
			for (const String& tag : tags)
			{
				text += (text.empty() ? "" : ", ") + tag;
			}
			return text;
		}

		/// Keeps an Enum default one of the allowed values (the first one if it is not).
		void NormalizeEnumDefault(PropertyDef& property)
		{
			if (property.type != PropertyType::Enum || property.enumValues.empty())
			{
				return;
			}
			const String* current = std::get_if<String>(&property.defaultValue);
			if (current == nullptr || std::find(property.enumValues.begin(), property.enumValues.end(), *current) == property.enumValues.end())
			{
				property.defaultValue = property.enumValues.front();
			}
		}

		/// `base`, or `base2`, `base3`, ... whichever no property of the type uses yet.
		String UniquePropertyName(const pcg::graph::ControlType& type, const String& base)
		{
			const auto exists = [&type](const String& name)
			{
				for (const PropertyGroup& group : type.groups)
				{
					for (const PropertyDef& property : group.properties)
					{
						if (property.name == name)
						{
							return true;
						}
					}
				}
				return false;
			};

			String name = base;
			for (int suffix = 2; exists(name); ++suffix)
			{
				name = base + std::to_string(suffix);
			}
			return name;
		}

		/// Moves the element at `index` by `direction` (-1 up, +1 down) if that stays inside the vector.
		template <typename T>
		void MoveElement(std::vector<T>& items, const size_t index, const int direction)
		{
			const int target = static_cast<int>(index) + direction;
			if (index >= items.size() || target < 0 || target >= static_cast<int>(items.size()))
			{
				return;
			}
			std::swap(items[index], items[static_cast<size_t>(target)]);
		}
	}

	PcgControlTypeEditorInstance::PcgControlTypeEditorInstance(EditorHost& host, PcgControlTypeEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
	{
		LoadFromFile();
	}

	String PcgControlTypeEditorInstance::GetNormalizedAssetPath() const
	{
		String path = GetAssetPath().string();
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	void PcgControlTypeEditorInstance::LoadFromFile()
	{
		const String path = GetNormalizedAssetPath();

		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(path);
		if (!file)
		{
			m_loadError = "Unable to open " + path;
			ELOG("Control type editor: " << m_loadError);
			return;
		}

		const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
		String error;
		if (!pcg::graph::ParseControlType(text, m_type, error))
		{
			m_loadError = path + " is not a valid control type: " + error;
			ELOG("Control type editor: " << m_loadError);
			return;
		}

		m_savedText = pcg::graph::WriteControlType(m_type);
		ResetUiState();
	}

	void PcgControlTypeEditorInstance::ResetUiState()
	{
		m_tagsText = JoinTags(m_type.defaultTags);
		m_keys.clear();
		for (const PropertyGroup& group : m_type.groups)
		{
			GroupKeys keys;
			keys.key = m_nextKey++;
			for (size_t i = 0; i < group.properties.size(); ++i)
			{
				keys.propertyKeys.push_back(m_nextKey++);
			}
			m_keys.push_back(std::move(keys));
		}
	}

	std::vector<String> PcgControlTypeEditorInstance::Validate() const
	{
		std::vector<String> problems;
		std::map<String, int> counts;
		for (const PropertyGroup& group : m_type.groups)
		{
			for (const PropertyDef& property : group.properties)
			{
				if (property.name.empty())
				{
					problems.push_back("Group '" + group.name + "' has a property with an empty name.");
				}
				else
				{
					++counts[property.name];
				}
			}
		}
		for (const auto& entry : counts)
		{
			if (entry.second > 1)
			{
				problems.push_back("The property name '" + entry.first + "' is used " + std::to_string(entry.second) + " times; names must be unique.");
			}
		}
		return problems;
	}

	bool PcgControlTypeEditorInstance::HasUnsavedChanges() const
	{
		return m_loadError.empty() && pcg::graph::WriteControlType(m_type) != m_savedText;
	}

	void PcgControlTypeEditorInstance::UpdateDirtyState()
	{
		const bool dirty = HasUnsavedChanges();
		if (dirty && !m_wasDirty)
		{
			m_editor.modified();
		}
		m_wasDirty = dirty;
	}

	void PcgControlTypeEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().string().c_str());

		if (!m_loadError.empty())
		{
			ImGui::TextColored(s_errorColor, "%s", m_loadError.c_str());
			ImGui::TextWrapped("The file is left untouched; saving is disabled.");
			ImGui::PopID();
			return;
		}

		for (const String& problem : Validate())
		{
			ImGui::TextColored(s_errorColor, "%s", problem.c_str());
		}

		ImGui::InputText("Name", &m_type.name);

		int shape = static_cast<int>(m_type.shape);
		if (ImGui::Combo("Shape", &shape, s_shapeNames, static_cast<int>(std::size(s_shapeNames))))
		{
			m_type.shape = static_cast<ControlShape>(shape);
		}

		if (ImGui::InputText("Default tags", &m_tagsText))
		{
			m_type.defaultTags = ParseTags(m_tagsText);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Comma separated. Every placed control of this type carries these tags.");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Property groups");

		PendingEdit pending;
		for (size_t g = 0; g < m_type.groups.size(); ++g)
		{
			DrawGroup(g, pending);
		}

		if (ImGui::Button(ICON_FA_PLUS " Add group"))
		{
			PropertyGroup group;
			group.name = "Group";
			m_type.groups.push_back(std::move(group));
			GroupKeys keys;
			keys.key = m_nextKey++;
			m_keys.push_back(std::move(keys));
		}

		ApplyPendingEdit(pending);
		UpdateDirtyState();

		ImGui::PopID();
	}

	void PcgControlTypeEditorInstance::DrawGroup(const size_t groupIndex, PendingEdit& pending)
	{
		PropertyGroup& group = m_type.groups[groupIndex];

		ImGui::PushID(static_cast<int>(m_keys[groupIndex].key));

		const bool open = ImGui::TreeNodeEx("group", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed,
			"%s", group.name.empty() ? "(unnamed group)" : group.name.c_str());
		if (open)
		{
			ImGui::InputText("Group name", &group.name);

			ImGui::BeginDisabled(groupIndex == 0);
			if (ImGui::Button("Move up"))
			{
				pending = { PendingEdit::Kind::MoveGroup, groupIndex, 0, -1 };
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(groupIndex + 1 >= m_type.groups.size());
			if (ImGui::Button("Move down"))
			{
				pending = { PendingEdit::Kind::MoveGroup, groupIndex, 0, 1 };
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH " Remove group"))
			{
				pending = { PendingEdit::Kind::RemoveGroup, groupIndex, 0, 0 };
			}

			for (size_t p = 0; p < group.properties.size(); ++p)
			{
				DrawProperty(groupIndex, p, pending);
			}

			if (ImGui::Button(ICON_FA_PLUS " Add property"))
			{
				pending = { PendingEdit::Kind::AddProperty, groupIndex, 0, 0 };
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	void PcgControlTypeEditorInstance::DrawProperty(const size_t groupIndex, const size_t propertyIndex, PendingEdit& pending)
	{
		PropertyGroup& group = m_type.groups[groupIndex];
		PropertyDef& property = group.properties[propertyIndex];

		ImGui::PushID(static_cast<int>(m_keys[groupIndex].propertyKeys[propertyIndex]));

		const int typeIndex = static_cast<int>(property.type);
		const bool open = ImGui::TreeNodeEx("property", ImGuiTreeNodeFlags_DefaultOpen,
			"%s  (%s)", property.name.empty() ? "(no name)" : property.name.c_str(), s_propertyTypeNames[typeIndex]);
		if (open)
		{
			ImGui::InputText("Name", &property.name);

			int type = typeIndex;
			if (ImGui::Combo("Type", &type, s_propertyTypeNames, static_cast<int>(std::size(s_propertyTypeNames))))
			{
				const PropertyType newType = static_cast<PropertyType>(type);
				if (pcg::graph::AttributeTypeOf(newType) != pcg::graph::AttributeTypeOf(property.type))
				{
					property.defaultValue = pcg::graph::DefaultValue(pcg::graph::AttributeTypeOf(newType));
				}
				property.type = newType;
				NormalizeEnumDefault(property);
			}

			if (property.type == PropertyType::Float || property.type == PropertyType::Int)
			{
				ImGui::DragFloat("Min", &property.min, 0.1f);
				ImGui::DragFloat("Max", &property.max, 0.1f);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Values are clamped to [Min, Max] only when Max is greater than Min.");
				}
			}

			if (property.type == PropertyType::Enum)
			{
				ImGui::TextUnformatted("Values");
				int removeValue = -1;
				for (size_t v = 0; v < property.enumValues.size(); ++v)
				{
					ImGui::PushID(static_cast<int>(v));
					const String before = property.enumValues[v];
					if (ImGui::InputText("##value", &property.enumValues[v]))
					{
						// A renamed value takes the default along if it was the default.
						const String* current = std::get_if<String>(&property.defaultValue);
						if (current != nullptr && *current == before)
						{
							property.defaultValue = property.enumValues[v];
						}
						NormalizeEnumDefault(property);
					}
					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_TRASH))
					{
						removeValue = static_cast<int>(v);
					}
					ImGui::PopID();
				}
				if (removeValue >= 0)
				{
					property.enumValues.erase(property.enumValues.begin() + removeValue);
					NormalizeEnumDefault(property);
				}
				if (ImGui::Button(ICON_FA_PLUS " Add value"))
				{
					property.enumValues.push_back("Value" + std::to_string(property.enumValues.size() + 1));
					NormalizeEnumDefault(property);
				}
			}

			DrawControlPropertyWidget("Default", property, property.defaultValue);

			ImGui::InputText("Description", &property.description);

			ImGui::BeginDisabled(propertyIndex == 0);
			if (ImGui::Button("Move up"))
			{
				pending = { PendingEdit::Kind::MoveProperty, groupIndex, propertyIndex, -1 };
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(propertyIndex + 1 >= group.properties.size());
			if (ImGui::Button("Move down"))
			{
				pending = { PendingEdit::Kind::MoveProperty, groupIndex, propertyIndex, 1 };
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH " Remove property"))
			{
				pending = { PendingEdit::Kind::RemoveProperty, groupIndex, propertyIndex, 0 };
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	void PcgControlTypeEditorInstance::ApplyPendingEdit(const PendingEdit& pending)
	{
		if (pending.kind == PendingEdit::Kind::None || pending.group >= m_type.groups.size())
		{
			return;
		}

		PropertyGroup& group = m_type.groups[pending.group];
		GroupKeys& keys = m_keys[pending.group];

		switch (pending.kind)
		{
		case PendingEdit::Kind::AddProperty:
		{
			PropertyDef property;
			property.name = UniquePropertyName(m_type, "property");
			property.type = PropertyType::Float;
			property.defaultValue = pcg::graph::DefaultValue(pcg::graph::AttributeType::Float);
			group.properties.push_back(std::move(property));
			keys.propertyKeys.push_back(m_nextKey++);
			break;
		}
		case PendingEdit::Kind::RemoveGroup:
			m_type.groups.erase(m_type.groups.begin() + static_cast<std::ptrdiff_t>(pending.group));
			m_keys.erase(m_keys.begin() + static_cast<std::ptrdiff_t>(pending.group));
			break;
		case PendingEdit::Kind::MoveGroup:
			MoveElement(m_type.groups, pending.group, pending.direction);
			MoveElement(m_keys, pending.group, pending.direction);
			break;
		case PendingEdit::Kind::RemoveProperty:
			if (pending.property < group.properties.size())
			{
				group.properties.erase(group.properties.begin() + static_cast<std::ptrdiff_t>(pending.property));
				keys.propertyKeys.erase(keys.propertyKeys.begin() + static_cast<std::ptrdiff_t>(pending.property));
			}
			break;
		case PendingEdit::Kind::MoveProperty:
			MoveElement(group.properties, pending.property, pending.direction);
			MoveElement(keys.propertyKeys, pending.property, pending.direction);
			break;
		case PendingEdit::Kind::None:
			break;
		}
	}

	void PcgControlTypeEditorInstance::InvalidateInOpenWorlds() const
	{
		const String typePath = GetNormalizedAssetPath();
		for (WorldEditorInstance* world : WorldEditorInstance::GetOpenInstances())
		{
			if (ProceduralEditMode* mode = world->GetProceduralEditMode())
			{
				mode->GetControlLibrary().Invalidate(typePath);
			}
		}
	}

	bool PcgControlTypeEditorInstance::Save()
	{
		const String path = GetNormalizedAssetPath();

		if (!m_loadError.empty())
		{
			// Never overwrite a file this editor could not read: it would lose its content.
			ELOG("Not saving control type " << path << ": it could not be loaded (" << m_loadError << ")");
			return false;
		}

		const std::vector<String> problems = Validate();
		if (!problems.empty())
		{
			ELOG("Not saving control type " << path << ": " << problems.front());
			return false;
		}

		const std::unique_ptr<std::ostream> stream = AssetRegistry::CreateNewFile(path);
		if (!stream)
		{
			ELOG("Failed to save control type " << path << ": unable to open file for writing");
			return false;
		}

		const String text = pcg::graph::WriteControlType(m_type);
		*stream << text;
		stream->flush();

		m_savedText = text;
		m_wasDirty = false;
		m_editor.saved();
		InvalidateInOpenWorlds();
		ILOG("Saved control type " << path);
		return true;
	}
}
