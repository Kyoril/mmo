// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "control_details.h"

#include "editor_windows/asset_picker_widget.h"
#include "editor_windows/editor_imgui_helpers.h"
#include "editor_windows/proto_entry_picker.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
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

		/// One entry of an AssetList value.
		struct WeightedAsset
		{
			String asset;
			float weight = 1.0f;
		};

		bool ParseFloat(const String& text, float& out)
		{
			char* end = nullptr;
			out = std::strtof(text.c_str(), &end);
			return !text.empty() && end == text.c_str() + text.size() && std::isfinite(out);
		}

		/// "asset|weight;asset|weight" -> entries (a missing or invalid weight is 1).
		std::vector<WeightedAsset> ParseAssetList(const String& text)
		{
			std::vector<WeightedAsset> entries;
			size_t start = 0;
			while (start < text.size())
			{
				const size_t end = std::min(text.find(';', start), text.size());
				const String item = text.substr(start, end - start);
				start = end + 1;
				if (item.empty())
				{
					continue;
				}

				WeightedAsset entry;
				const size_t bar = item.rfind('|');
				entry.asset = item.substr(0, bar);
				if (bar != String::npos && !ParseFloat(item.substr(bar + 1), entry.weight))
				{
					entry.weight = 1.0f;
				}
				entries.push_back(std::move(entry));
			}
			return entries;
		}

		String WriteAssetList(const std::vector<WeightedAsset>& entries)
		{
			String text;
			for (const WeightedAsset& entry : entries)
			{
				char weight[32];
				std::snprintf(weight, sizeof(weight), "%g", static_cast<double>(entry.weight));
				text += (text.empty() ? "" : ";") + entry.asset + "|" + weight;
			}
			return text;
		}

		/// Draws an AssetList value as rows of asset and weight. @return True if it changed.
		bool DrawAssetList(String& value)
		{
			std::vector<WeightedAsset> entries = ParseAssetList(value);
			bool changed = false;
			int removeIndex = -1;
			for (size_t i = 0; i < entries.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
				changed = AssetPickerWidget::Draw("##asset", entries[i].asset, s_propertyAssetExtensions, nullptr, nullptr, 0.0f) || changed;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				changed = ImGui::DragFloat("##weight", &entries[i].weight, 0.05f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp) || changed;
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Weight of this asset when the graph picks one.");
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					removeIndex = static_cast<int>(i);
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				entries.erase(entries.begin() + removeIndex);
				changed = true;
			}

			if (ImGui::SmallButton("Add asset"))
			{
				entries.push_back(WeightedAsset());
				changed = true;
			}

			if (changed)
			{
				value = WriteAssetList(entries);
			}
			return changed;
		}

		/// Draws the widget of one property for its current value. @return True if the value changed.
		bool DrawPropertyWidget(const PropertyDef& property, AttributeValue& value)
		{
			const char* label = property.name.c_str();
			const bool ranged = property.max > property.min;
			switch (property.type)
			{
			case PropertyType::Float:
			{
				float& f = std::get<float>(value);
				const float speed = ranged ? std::max((property.max - property.min) / 200.0f, 0.001f) : 0.1f;
				return ImGui::DragFloat(label, &f, speed, ranged ? property.min : 0.0f, ranged ? property.max : 0.0f, "%.3f",
					ranged ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
			}

			case PropertyType::Int:
			{
				int64& i = std::get<int64>(value);
				const int64 minValue = static_cast<int64>(std::ceil(property.min));
				const int64 maxValue = static_cast<int64>(std::floor(property.max));
				return ImGui::DragScalar(label, ImGuiDataType_S64, &i, 0.2f, ranged ? &minValue : nullptr, ranged ? &maxValue : nullptr, nullptr,
					ranged ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
			}

			case PropertyType::Bool:
				return ImGui::Checkbox(label, &std::get<bool>(value));

			case PropertyType::Enum:
			{
				String& s = std::get<String>(value);
				std::vector<const char*> names;
				names.reserve(property.enumValues.size());
				for (const String& name : property.enumValues)
				{
					names.push_back(name.c_str());
				}

				const auto found = std::find(property.enumValues.begin(), property.enumValues.end(), s);
				int index = found == property.enumValues.end() ? -1 : static_cast<int>(found - property.enumValues.begin());
				const String id = "##" + property.name;
				const bool changed = DrawEnumCombo(id.c_str(), label, index, names.data(), static_cast<int>(names.size()), nullptr, ImGui::CalcItemWidth());
				if (changed && index >= 0 && index < static_cast<int>(property.enumValues.size()))
				{
					s = property.enumValues[index];
					return true;
				}
				return false;
			}

			case PropertyType::String:
				return ImGui::InputText(label, &std::get<String>(value));

			case PropertyType::Asset:
				return AssetPickerWidget::Draw(label, std::get<String>(value), s_propertyAssetExtensions, nullptr, nullptr, 0.0f);

			case PropertyType::AssetList:
				ImGui::TextUnformatted(label);
				ImGui::Indent();
				{
					const bool changed = DrawAssetList(std::get<String>(value));
					ImGui::Unindent();
					return changed;
				}

			case PropertyType::Color:
			{
				Vector3& c = std::get<Vector3>(value);
				float rgb[3] = { c.x, c.y, c.z };
				if (ImGui::ColorEdit3(label, rgb))
				{
					c = Vector3(rgb[0], rgb[1], rgb[2]);
					return true;
				}
				return false;
			}
			}

			return false;
		}
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
			if (!ParseFloat(text, f))
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
				if (end == String::npos || !ParseFloat(text.substr(start, end - start), rgb[c]))
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
					const bool edited = DrawPropertyWidget(property, value);
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
