// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_system_editor.h"

#include "particle_system_editor_instance.h"
#include "log/default_log_levels.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	static const String ParticleSystemExtension = ".hpar";

	ParticleSystemEditor::ParticleSystemEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	void ParticleSystemEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	bool ParticleSystemEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ParticleSystemExtension;
	}

	void ParticleSystemEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Particle System"))
		{
			m_showNameDialog = true;
		}
	}

	void ParticleSystemEditor::DrawImpl()
	{
		if (m_showNameDialog)
		{
			ImGui::OpenPopup("Create New Particle System");
			m_showNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Particle System", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new particle system:");

			ImGui::InputText("##field", &m_particleSystemName);
			ImGui::SameLine();
			ImGui::Text(ParticleSystemExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewParticleSystem();
				ImGui::CloseCurrentPopup();
			}
			
			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	std::shared_ptr<EditorInstance> ParticleSystemEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<ParticleSystemEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open particle system editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void ParticleSystemEditor::CreateNewParticleSystem()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_particleSystemName + ParticleSystemExtension;
		m_particleSystemName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new particle system file");
			return;
		}

		// Create default particle emitter parameters
		ParticleEmitterParameters params;
		// params already has sensible defaults from its constructor

		// Serialize to file
		io::StreamSink sink(*file);
		io::Writer writer(sink);

		ParticleEmitterSerializer serializer;
		serializer.Serialize(params, writer);

		file->flush();

		// Notify that asset was created
		m_host.assetImported(m_host.GetCurrentPath());
	}
}
