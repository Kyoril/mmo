// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "control_details.h"
#include "text_parsing.h"

#include "editors/pcg_graph_editor/setting_widgets.h"
#include "editor_windows/editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <vector>

namespace mmo
{
	namespace
	{
		using pcg::graph::AttributeValue;
		using pcg::graph::PropertyDef;
		using pcg::graph::PropertyType;

		/// Asset kinds an Asset or AssetList property can pick (content a graph places or reads).
		const std::set<String> s_propertyAssetExtensions = { ".hmsh", ".hwmo", ".hmat", ".hmi", ".htex", ".hpar", ".hsec", ".hpcgg" };

		/// The extension list of s_propertyAssetExtensions in widget hint form (".a;.b").
		String PropertyAssetExtensionList()
		{
			String list;
			for (const String& extension : s_propertyAssetExtensions)
			{
				list += (list.empty() ? "" : ";") + extension;
			}
			return list;
		}
	}

	bool DrawControlPropertyWidget(const char* label, const PropertyDef& property, AttributeValue& value)
	{
		String widget;
		switch (property.type)
		{
		case PropertyType::Asset:
			widget = "asset:" + PropertyAssetExtensionList();
			break;
		case PropertyType::AssetList:
			widget = "assetList:" + PropertyAssetExtensionList();
			break;
		case PropertyType::Color:
			widget = "color";
			break;
		default:
			break;
		}

		static const std::vector<String> s_noEnumValues;
		const std::vector<String>& enumValues = property.type == PropertyType::Enum ? property.enumValues : s_noEnumValues;
		return DrawAttributeValueWidget(label, value, pcg::graph::AttributeTypeOf(property.type), enumValues,
			property.min, property.max, widget);
	}

	const PropertyDef* FindControlProperty(const pcg::graph::ControlType& type, const String& name)
	{
		for (const pcg::graph::PropertyGroup& group : type.groups)
		{
			for (const PropertyDef& property : group.properties)
			{
				if (property.name == name)
				{
					return &property;
				}
			}
		}

		return nullptr;
	}

	bool ParseControlPropertyValue(const PropertyDef& property, const String& text, AttributeValue& out, String& error)
	{
		switch (property.type)
		{
		case PropertyType::Float:
		{
			float f = 0.0f;
			if (!ParseFloatText(text, f))
			{
				error = "property '" + property.name + "' needs a number, got '" + text + "'";
				return false;
			}
			out = f;
			return true;
		}

		case PropertyType::Int:
		{
			errno = 0;
			char* end = nullptr;
			const long long i = std::strtoll(text.c_str(), &end, 10);
			if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE)
			{
				error = "property '" + property.name + "' needs an integer, got '" + text + "'";
				return false;
			}
			out = static_cast<int64>(i);
			return true;
		}

		case PropertyType::Bool:
			if (text == "true" || text == "1")
			{
				out = true;
				return true;
			}
			if (text == "false" || text == "0")
			{
				out = false;
				return true;
			}
			error = "property '" + property.name + "' needs true or false, got '" + text + "'";
			return false;

		case PropertyType::Enum:
			if (std::find(property.enumValues.begin(), property.enumValues.end(), text) == property.enumValues.end())
			{
				String allowed;
				for (const String& value : property.enumValues)
				{
					allowed += (allowed.empty() ? "" : ", ") + value;
				}
				error = "property '" + property.name + "' must be one of: " + allowed + "; got '" + text + "'";
				return false;
			}
			out = text;
			return true;

		case PropertyType::Color:
		{
			float rgb[3] = {};
			size_t start = 0;
			for (int c = 0; c < 3; ++c)
			{
				const size_t end = c < 2 ? text.find(',', start) : text.size();
				if (end == String::npos || !ParseFloatText(text.substr(start, end - start), rgb[c]))
				{
					error = "property '" + property.name + "' needs a colour r,g,b, got '" + text + "'";
					return false;
				}
				start = end + 1;
			}
			out = Vector3(rgb[0], rgb[1], rgb[2]);
			return true;
		}

		case PropertyType::String:
		case PropertyType::Asset:
		case PropertyType::AssetList:
			out = text;
			return true;
		}

		error = "property '" + property.name + "' has an unknown type";
		return false;
	}

	bool DrawControlProperties(const pcg::graph::ControlType& type, pcg::graph::ControlInstance& instance)
	{
		bool changed = false;
		for (size_t g = 0; g < type.groups.size(); ++g)
		{
			const pcg::graph::PropertyGroup& group = type.groups[g];
			ImGui::PushID(static_cast<int>(g));
			const String header = (group.name.empty() ? String("Properties") : group.name) + "##group";
			if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const PropertyDef& property : group.properties)
				{
					ImGui::PushID(property.name.c_str());
					const pcg::graph::AttributeType expected = pcg::graph::AttributeTypeOf(property.type);

					// The override if it has the property's type, else the default (a mismatched override is
					// shown as the default and replaced by the first edit; resolution warns about it).
					const auto found = instance.overrides.find(property.name);
					const bool overridden = found != instance.overrides.end();
					AttributeValue value = property.defaultValue;
					if (overridden && pcg::graph::TypeOf(found->second) == expected)
					{
						value = found->second;
					}
					if (pcg::graph::TypeOf(value) != expected)
					{
						value = pcg::graph::DefaultValue(expected);
					}

					if (overridden)
					{
						// Overridden values stand out from defaults.
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.45f, 1.0f));
					}
					const bool edited = DrawControlPropertyWidget(property.name.c_str(), property, value);
					if (overridden)
					{
						ImGui::PopStyleColor();
					}

					if (edited)
					{
						instance.overrides[property.name] = value;
						changed = true;
					}

					if (!property.description.empty())
					{
						ImGui::SameLine();
						DrawHelpMarker(property.description.c_str());
					}

					if (overridden)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton("Reset"))
						{
							instance.overrides.erase(property.name);
							changed = true;
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Back to the control type's default.");
						}
					}
					ImGui::PopID();
				}
			}
			ImGui::PopID();
		}

		// Overrides of properties the type does not have (kept, resolution warns about them).
		std::vector<String> unknown;
		for (const auto& entry : instance.overrides)
		{
			if (!FindControlProperty(type, entry.first))
			{
				unknown.push_back(entry.first);
			}
		}
		if (!unknown.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Overrides of properties the type does not have:");
			for (const String& name : unknown)
			{
				ImGui::PushID(name.c_str());
				ImGui::BulletText("%s", name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					instance.overrides.erase(name);
					changed = true;
				}
				ImGui::PopID();
			}
		}

		return changed;
	}
}
