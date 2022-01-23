
#include "editor_base.h"
#include "editor_host.h"

#include "imgui.h"

namespace mmo
{
	EditorBase::EditorBase(EditorHost& host)
		: m_host(host)
	{
	}

	void EditorBase::Draw()
	{
		if (m_instances.empty())
		{
			return;
		}

		// Hacky iteration over m_instances which remove if no longer visible
		std::erase_if(m_instances, [&](const std::shared_ptr<EditorInstance>& instance)
		{
			ImGuiWindowFlags flags = ImGuiWindowFlags_None;
			
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
			}

			return !visible;
		});
	}

	bool EditorBase::OpenAsset(const Path& asset)
	{
		auto instance = OpenAssetImpl(asset);
		if (!instance)
		{
			return false;
		}

		m_instances.emplace_back(std::move(instance));
		return true;
	}
}
