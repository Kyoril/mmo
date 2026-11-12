// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_editor.h"

#include "stream_sink.h"
#include "world_editor_instance.h"
#include "pcg/section_bake_job.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "proto_data/project.h"

namespace mmo
{
	static const String WorldModelFileExtension = ".hwld";

	WorldEditor::WorldEditor(EditorHost& host, proto::Project& project)
		: EditorBase(host)
		, m_project(project)
	{
	}

	void WorldEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void WorldEditor::CreateNewWorld()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_worldName + WorldModelFileExtension;
		m_worldName.clear();
		
		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new world");
			return;
		}

		const std::shared_ptr<Material> material = std::make_shared<Material>(currentPath.string());
		material->SetType(MaterialType::Opaque);
		material->SetCastShadows(true);
		material->SetReceivesShadows(true);
		material->SetTwoSided(false);

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		// TODO: Serialize world file contents

		constexpr uint32 versionHeader = 'MVER';
		constexpr uint32 meshHeader = 'MESH';
		constexpr uint32 entityHeader = 'MENT';

		// Start writing file
		writer
			<< io::write<uint32>(versionHeader)
			<< io::write<uint32>(sizeof(uint32))
			<< io::write<uint32>(0x0001);

		// Write mesh names
		writer
			<< io::write<uint32>(meshHeader)
			<< io::write<uint32>(0);

		// TODO
		sink.Flush();


		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	bool WorldEditor::CanLoadAsset(const String& extension) const
	{
		return extension == WorldModelFileExtension;
	}

	void WorldEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New World"))
		{
			m_worldName.clear();
			m_showWorldNameDialog = true;
		}

		if (ImGui::MenuItem("New Section..."))
		{
			m_sectionName.clear();
			m_sectionMaterial = DefaultSectionTerrainMaterial;
			m_sectionError.clear();
			m_showSectionDialog = true;
		}
	}

	void WorldEditor::DrawNewSectionDialog()
	{
		if (m_showSectionDialog)
		{
			ImGui::OpenPopup("New Section");
			m_showSectionDialog = false;
		}

		if (!ImGui::BeginPopupModal("New Section", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		ImGui::TextUnformatted("Creates a section world for building a road or path section template.");
		ImGui::TextDisabled("Stored in %s/<Name>/ (editor only).", SectionWorldFolder);

		ImGui::InputText("Name", &m_sectionName);
		ImGui::InputText("Terrain material", &m_sectionMaterial);

		if (!m_sectionError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_sectionError.c_str());
		}

		if (ImGui::Button("Create"))
		{
			if (CreateSectionWorld(m_sectionName, m_sectionMaterial, m_sectionError))
			{
				const String worldPath = GetSectionWorldPath(m_sectionName);
				m_host.assetImported(Path(worldPath).parent_path());
				m_host.OpenAsset(Path(worldPath));
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void WorldEditor::DrawImpl()
	{
		if (m_showWorldNameDialog)
		{
			ImGui::OpenPopup("Create New World");
			m_showWorldNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New World", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new world:");

			ImGui::InputText("##field", &m_worldName);
			ImGui::SameLine();
			ImGui::Text(WorldModelFileExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewWorld();
				ImGui::CloseCurrentPopup();
			}
			
			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		DrawNewSectionDialog();
	}

	void WorldEditor::SetPendingCameraTarget(const Path& asset, const Vector3& worldLocation)
	{
		m_pendingCameraTarget = std::make_pair(asset, worldLocation);
	}

	std::shared_ptr<EditorInstance> WorldEditor::OpenAssetImpl(const Path& asset)
	{
		std::shared_ptr<EditorInstance> result;

		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			result = it->second;
		}
		else
		{
			const auto instance = m_instances.emplace(asset, std::make_shared<WorldEditorInstance>(m_host, *this, asset));
			if (!instance.second)
			{
				ELOG("Failed to open model editor instance");
				return nullptr;
			}

			result = instance.first->second;
		}

		// Apply a pending camera target if one was requested for this asset (e.g. from the map editor's
		// minimap location picker).
		if (m_pendingCameraTarget && m_pendingCameraTarget->first == asset)
		{
			if (auto* worldInstance = static_cast<WorldEditorInstance*>(result.get()))
			{
				worldInstance->MoveCameraToWorldPosition(m_pendingCameraTarget->second.x, m_pendingCameraTarget->second.z);
			}
			m_pendingCameraTarget.reset();
		}

		return result;
	}
}
