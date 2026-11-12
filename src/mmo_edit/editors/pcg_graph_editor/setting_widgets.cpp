// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "setting_widgets.h"

#include "editor_windows/asset_picker_widget.h"
#include "editor_windows/proto_entry_picker.h"
#include "editors/world_editor/pcg/text_parsing.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace mmo
{
	namespace
	{
		const String AssetHintPrefix = "asset:";
		const String AssetListHintPrefix = "assetList:";
		const String ColorHint = "color";

		bool StartsWith(const String& text, const String& prefix)
		{
			return text.compare(0, prefix.size(), prefix) == 0;
		}
	}

	std::vector<WeightedAsset> ParseWeightedAssetList(const String& text)
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
			if (bar != String::npos && !ParseFloatText(item.substr(bar + 1), entry.weight))
			{
				entry.weight = 1.0f;
			}
			entries.push_back(std::move(entry));
		}
		return entries;
	}

	String WriteWeightedAssetList(const std::vector<WeightedAsset>& entries)
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

	std::set<String> ParseWidgetExtensions(const String& widget)
	{
		String list;
		if (StartsWith(widget, AssetListHintPrefix))
		{
			list = widget.substr(AssetListHintPrefix.size());
		}
		else if (StartsWith(widget, AssetHintPrefix))
		{
			list = widget.substr(AssetHintPrefix.size());
		}

		std::set<String> extensions;
		size_t start = 0;
		while (start < list.size())
		{
			const size_t end = std::min(list.find(';', start), list.size());
			if (end > start)
			{
				extensions.insert(list.substr(start, end - start));
			}
			start = end + 1;
		}
		return extensions;
	}

	bool DrawWeightedAssetList(String& value, const std::set<String>& extensions)
	{
		std::vector<WeightedAsset> entries = ParseWeightedAssetList(value);
		bool changed = false;
		int removeIndex = -1;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			changed = AssetPickerWidget::Draw("##asset", entries[i].asset, extensions, nullptr, nullptr, 0.0f) || changed;
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
			value = WriteWeightedAssetList(entries);
		}
		return changed;
	}

	bool DrawAttributeValueWidget(const char* label, pcg::graph::AttributeValue& value, const pcg::graph::AttributeType type,
		const std::vector<String>& enumValues, const float min, const float max, const String& widget)
	{
		using pcg::graph::AttributeType;

		pcg::graph::AttributeValue edited = pcg::graph::TypeOf(value) == type ? value : pcg::graph::DefaultValue(type);
		const bool ranged = max > min;
		bool changed = false;

		switch (type)
		{
		case AttributeType::Float:
		{
			float& f = std::get<float>(edited);
			const float speed = ranged ? std::max((max - min) / 200.0f, 0.001f) : 0.1f;
			changed = ImGui::DragFloat(label, &f, speed, ranged ? min : 0.0f, ranged ? max : 0.0f, "%.3f",
				ranged ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
			break;
		}

		case AttributeType::Int:
		{
			int64& i = std::get<int64>(edited);
			const int64 minValue = static_cast<int64>(std::ceil(min));
			const int64 maxValue = static_cast<int64>(std::floor(max));
			changed = ImGui::DragScalar(label, ImGuiDataType_S64, &i, 0.2f, ranged ? &minValue : nullptr, ranged ? &maxValue : nullptr, nullptr,
				ranged ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
			break;
		}

		case AttributeType::Bool:
			changed = ImGui::Checkbox(label, &std::get<bool>(edited));
			break;

		case AttributeType::Vector3:
		{
			Vector3& v = std::get<Vector3>(edited);
			float xyz[3] = { v.x, v.y, v.z };
			changed = widget == ColorHint ? ImGui::ColorEdit3(label, xyz) : ImGui::DragFloat3(label, xyz, 0.1f);
			if (changed)
			{
				v = Vector3(xyz[0], xyz[1], xyz[2]);
			}
			break;
		}

		case AttributeType::String:
		{
			String& s = std::get<String>(edited);
			if (!enumValues.empty())
			{
				std::vector<const char*> names;
				names.reserve(enumValues.size());
				for (const String& name : enumValues)
				{
					names.push_back(name.c_str());
				}

				const auto found = std::find(enumValues.begin(), enumValues.end(), s);
				int index = found == enumValues.end() ? -1 : static_cast<int>(found - enumValues.begin());
				const String id = String("##") + label;
				if (DrawEnumCombo(id.c_str(), label, index, names.data(), static_cast<int>(names.size()), nullptr, ImGui::CalcItemWidth())
					&& index >= 0 && index < static_cast<int>(enumValues.size()))
				{
					s = enumValues[index];
					changed = true;
				}
			}
			else if (StartsWith(widget, AssetListHintPrefix))
			{
				ImGui::TextUnformatted(label);
				ImGui::Indent();
				ImGui::PushID(label);
				changed = DrawWeightedAssetList(s, ParseWidgetExtensions(widget));
				ImGui::PopID();
				ImGui::Unindent();
			}
			else if (StartsWith(widget, AssetHintPrefix))
			{
				changed = AssetPickerWidget::Draw(label, s, ParseWidgetExtensions(widget), nullptr, nullptr, 0.0f);
			}
			else
			{
				changed = ImGui::InputText(label, &s);
			}
			break;
		}
		}

		if (changed)
		{
			value = std::move(edited);
		}
		return changed;
	}
}
