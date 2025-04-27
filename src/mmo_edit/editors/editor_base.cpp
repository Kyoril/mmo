
#include "editor_base.h"
#include "editor_host.h"

#include "imgui.h"
#include "log/default_log_levels.h"

namespace mmo
{
	EditorBase::EditorBase(EditorHost& host)
		: m_host(host)
	{
	}

	void EditorBase::Draw()
	{
		DrawImpl();

		if (m_instances.empty())
		{
			return;
		}

		// Hacky iteration over m_instances which remove if no longer visible
		std::erase_if(m_instances, [&](std::shared_ptr<EditorInstance>& instance)
		{
			constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_None;

			bool visible = true;
			if (ImGui::Begin(instance->GetAssetPath().filename().string().c_str(), &visible, flags | ImGuiWindowFlags_NoSavedSettings))
			{
				m_host.SetActiveEditorInstance(instance.get());
				instance->Draw();
			}
			ImGui::End();

			if (!visible)
			{
				m_host.EditorInstanceClosed(*instance);
				CloseInstanceImpl(instance);
			}

			return !visible;
		});
	}

	bool EditorBase::Save()
	{
		bool success = true;

		for (auto& instance : m_instances)
		{
			if (!instance->Save())
			{
				ELOG("Failed to save editor instance " << instance->GetAssetPath());
				success = false;
			}
		}

		return success;
	}

	bool EditorBase::OpenAsset(const Path& asset)
	{
		const auto instanceIt = std::find_if(m_instances.begin(), m_instances.end(), [&](const std::shared_ptr<EditorInstance>& instance)
			{
				return instance->GetAssetPath() == asset;
			});

		if (instanceIt != m_instances.end())
		{
			ImGui::SetWindowFocus(asset.filename().string().c_str());
			return true;
		}

		auto instance = OpenAssetImpl(asset);
		if (!instance)
		{
			return false;
		}

		m_instances.emplace_back(instance);
		return true;
	}
}
