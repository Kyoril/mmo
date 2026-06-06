// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "global_shader_parameters_editor_instance.h"
#include "editor_host.h"

#include <imgui.h>

#include "imgui/misc/cpp/imgui_stdlib.h"
#include "log/default_log_levels.h"

namespace mmo
{
	GlobalShaderParametersEditorInstance::GlobalShaderParametersEditorInstance(EditorHost& host, const Path& assetPath)
		: EditorInstance(host, assetPath)
	{
		// Loading the asset populates the live runtime registry; mirror it into the working set.
		GlobalShaderParameters::Get().LoadFromAsset(assetPath.string());
		RefreshFromRegistry();
	}

	void GlobalShaderParametersEditorInstance::RefreshFromRegistry()
	{
		m_working = GlobalShaderParameters::Get().GetParameters();
	}

	void GlobalShaderParametersEditorInstance::SyncToRegistry()
	{
		auto& registry = GlobalShaderParameters::Get();
		registry.Clear();

		for (const auto& param : m_working)
		{
			if (param.name.empty())
			{
				continue;
			}

			if (param.type == global_shader_parameter_type::Vector)
			{
				registry.DefineVector(param.name, param.defaultValue);
			}
			else
			{
				registry.DefineScalar(param.name, param.defaultValue.x);
			}
		}
	}

	void GlobalShaderParametersEditorInstance::Draw()
	{
		ImGui::TextWrapped("Global shader variables are shared by every material. Change a value here "
			"(or from engine code via GlobalShaderParameters::Get()) and all materials referencing it "
			"update at once. Reference them in a material with the 'Global Scalar/Vector Parameter' nodes.");
		ImGui::Separator();

		// Creation controls
		ImGui::SetNextItemWidth(180.0f);
		ImGui::InputText("##newName", &m_newParamName);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		const char* types[] = { "Scalar", "Vector" };
		ImGui::Combo("##newType", &m_newParamType, types, IM_ARRAYSIZE(types));
		ImGui::SameLine();
		if (ImGui::Button("Add Parameter"))
		{
			if (!m_newParamName.empty())
			{
				const bool exists = std::any_of(m_working.begin(), m_working.end(),
					[this](const GlobalShaderParameter& p) { return p.name == m_newParamName; });
				if (exists)
				{
					WLOG("A global shader parameter named '" << m_newParamName << "' already exists");
				}
				else
				{
					GlobalShaderParameter param;
					param.name = m_newParamName;
					param.type = (m_newParamType == 1) ? global_shader_parameter_type::Vector : global_shader_parameter_type::Scalar;
					param.value = (param.type == global_shader_parameter_type::Vector) ? Vector4(1.0f, 1.0f, 1.0f, 1.0f) : Vector4(0.0f, 0.0f, 0.0f, 0.0f);
					param.defaultValue = param.value;
					m_working.emplace_back(std::move(param));
					m_newParamName.clear();
					m_modified = true;
				}
			}
		}

		ImGui::Separator();

		int removeIndex = -1;
		bool structureChanged = false;
		bool valueChanged = false;

		if (ImGui::BeginTable("##globalParams", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Default Value", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			for (int i = 0; i < static_cast<int>(m_working.size()); ++i)
			{
				GlobalShaderParameter& param = m_working[i];
				ImGui::PushID(i);
				ImGui::TableNextRow();

				// Name
				ImGui::TableSetColumnIndex(0);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputText("##name", &param.name))
				{
					structureChanged = true;
				}

				// Type (read-only label)
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(param.type == global_shader_parameter_type::Vector ? "Vector" : "Scalar");

				// Default value editor
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (param.type == global_shader_parameter_type::Vector)
				{
					float col[4] = { param.defaultValue.x, param.defaultValue.y, param.defaultValue.z, param.defaultValue.w };
					if (ImGui::ColorEdit4("##value", col, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar))
					{
						param.defaultValue = Vector4(col[0], col[1], col[2], col[3]);
						param.value = param.defaultValue;
						valueChanged = true;
					}
				}
				else
				{
					float value = param.defaultValue.x;
					if (ImGui::DragFloat("##value", &value, 0.01f))
					{
						param.defaultValue.x = value;
						param.value.x = value;
						valueChanged = true;
					}
				}

				// Actions
				ImGui::TableSetColumnIndex(3);
				if (ImGui::Button("Remove"))
				{
					removeIndex = i;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (removeIndex >= 0)
		{
			m_working.erase(m_working.begin() + removeIndex);
			structureChanged = true;
		}

		if (structureChanged)
		{
			// The set / names / order changed: rebuild the registry layout.
			m_modified = true;
			SyncToRegistry();
		}
		else if (valueChanged)
		{
			// Value-only change: update defaults in place without rebuilding the layout, so the
			// shared GPU buffer is just re-uploaded (live preview) instead of recreated.
			m_modified = true;
			auto& registry = GlobalShaderParameters::Get();
			for (const auto& param : m_working)
			{
				if (!param.name.empty())
				{
					registry.SetDefault(param.name, param.defaultValue);
				}
			}
		}
	}

	bool GlobalShaderParametersEditorInstance::Save()
	{
		if (!m_modified)
		{
			return true;
		}

		SyncToRegistry();

		if (!GlobalShaderParameters::Get().SaveToAsset(GetAssetPath().string()))
		{
			ELOG("Failed to save global shader parameters to " << GetAssetPath());
			return false;
		}

		m_modified = false;
		return true;
	}
}
