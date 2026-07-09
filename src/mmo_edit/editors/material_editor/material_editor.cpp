// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_editor.h"

#include "material_editor_instance.h"
#include "log/default_log_levels.h"

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/memory_source.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/material_serializer.h"
#include "graphics/shader_compiler.h"
#include "scene_graph/material_instance_serializer.h"
#include "editors/material_editor/material_graph.h"
#include "editors/material_editor/material_node.h"
#include "base/chunk_writer.h"
#include "preview_providers/preview_provider_manager.h"

#include "imgui_node_editor.h"

#include <chrono>

namespace mmo
{
	/// @brief Default file extension for material files.
	static const String MaterialExtension = ".hmat";
	static const String MaterialFunctionExtension = ".hmf";
	
	bool MaterialEditor::CanLoadAsset(const String& extension) const
	{
		return extension == MaterialExtension || extension == MaterialFunctionExtension;
	}

	void MaterialEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Material"))
		{
			m_showMaterialNameDialog = true;
		}
		if (ImGui::MenuItem("Create New Material Function"))
		{
			m_showMaterialFunctionNameDialog = true;
		}
	}

	void MaterialEditor::AddToolMenuItems()
	{
		if (ImGui::MenuItem("Rebuild All Materials"))
		{
			RebuildAllMaterials();
		}
	}

	void MaterialEditor::RebuildAllMaterials()
	{
		if (m_rebuildInProgress)
		{
			return;
		}

		m_rebuildSucceeded = 0;
		m_rebuildFailed.clear();
		m_rebuildQueue = AssetRegistry::ListFiles(MaterialExtension);
		m_rebuildIndex = 0;
		m_rebuildInProgress = !m_rebuildQueue.empty();

		// Throwaway node-editor context: graph deserialization restores node state into the current
		// editor context if one exists. It is never rendered and never serialized from — the original
		// graph chunk is copied back verbatim — but its presence avoids per-node warnings.
		if (m_rebuildInProgress && !m_rebuildEdContext)
		{
			ax::NodeEditor::Config editorConfig;
			editorConfig.SettingsFile = nullptr;
			m_rebuildEdContext = ax::NodeEditor::CreateEditor(&editorConfig);
		}

		ILOG("Rebuilding " << m_rebuildQueue.size() << " materials ...");

		if (!m_rebuildInProgress)
		{
			// Nothing to do: show the (empty) result right away.
			m_showRebuildResultPopup = true;
		}
	}

	bool MaterialEditor::RebuildMaterial(const std::string& assetPath)
	{
		// Load the material and its node graph, mirroring MaterialEditorInstance's load path.
		const auto material = std::make_shared<Material>(assetPath);

		MaterialGraph graph;
		graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
		graph.CreateNode<MaterialNode>(true);

		ExecutableMaterialGraphLoadContext context;

		// Raw bytes of the file's GRPH chunk. Node positions live in the node-editor context during
		// serialization, which this headless rebuild does not have (nodes are never rendered), so
		// re-serializing the graph would zero all node positions. Instead the original chunk is
		// written back verbatim below — the rebuild only refreshes the material/bytecode chunks.
		std::vector<char> rawGraphChunk;

		{
			const auto file = AssetRegistry::OpenFile(assetPath);
			if (!file)
			{
				ELOG("Failed to open material file " << assetPath << " for reading!");
				return false;
			}

			io::StreamSource source{ *file };
			io::Reader reader{ source };

			MaterialDeserializer deserializer{ *material };
			deserializer.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [&graph, &context, &rawGraphChunk](io::Reader& reader, uint32, uint32 chunkSize) -> bool
				{
					// Capture the raw chunk bytes, then deserialize the graph from the copy (the
					// graph itself is only needed to compile fresh shader code).
					rawGraphChunk.resize(chunkSize);
					if (reader.getSource()->read(rawGraphChunk.data(), chunkSize) != chunkSize)
					{
						return false;
					}

					io::MemorySource memorySource{ rawGraphChunk };
					io::Reader memoryReader{ memorySource };
					return graph.Deserialize(memoryReader, context);
				});

			if (!deserializer.Read(reader) || !context.PerformAfterLoadActions())
			{
				ELOG("Unable to read material file " << assetPath << "!");
				return false;
			}
		}

		// Recompile the graph into fresh shader bytecode. No Material::Update() here: the GPU shader
		// objects are not needed to re-save the asset, and skipping them keeps the rebuild fast.
		material->SetName(assetPath);

		const auto materialCompiler = GraphicsDevice::Get().CreateMaterialCompiler();
		graph.Compile(*materialCompiler);

		const auto shaderCompiler = GraphicsDevice::Get().CreateShaderCompiler();
		materialCompiler->Compile(*material, *shaderCompiler);

		// Re-save the material chunks, then append the original graph chunk unchanged.
		const auto outFile = AssetRegistry::CreateNewFile(assetPath);
		if (!outFile)
		{
			ELOG("Failed to open material file " << assetPath << " for writing!");
			return false;
		}

		io::StreamSink sink{ *outFile };
		io::Writer writer{ sink };

		MaterialSerializer serializer{};
		serializer.Export(*material, writer);

		if (!rawGraphChunk.empty())
		{
			ChunkWriter graphChunk{ ChunkMagic({ 'G', 'R', 'P', 'H' }), writer };
			writer << io::write_range(rawGraphChunk);
			graphChunk.Finish();
		}

		outFile->flush();

		m_previewManager.InvalidatePreview(assetPath);
		return true;
	}

	void MaterialEditor::TickMaterialRebuild()
	{
		if (!m_rebuildInProgress)
		{
			return;
		}

		// Activate the throwaway node-editor context while deserializing graphs (restore afterwards
		// so open material editor instances are unaffected).
		ax::NodeEditor::EditorContext* previousContext = ax::NodeEditor::GetCurrentEditor();
		ax::NodeEditor::SetCurrentEditor(static_cast<ax::NodeEditor::EditorContext*>(m_rebuildEdContext));

		// Process materials until the frame budget is exhausted (at least one per frame). Shader
		// compilation dominates the cost, so cheap materials batch up while expensive ones get a
		// frame to themselves — the UI keeps redrawing and the progress bar stays live.
		constexpr auto frameBudget = std::chrono::milliseconds(50);
		const auto tickStart = std::chrono::steady_clock::now();

		do
		{
			const std::string& assetPath = m_rebuildQueue[m_rebuildIndex];
			if (RebuildMaterial(assetPath))
			{
				++m_rebuildSucceeded;
			}
			else
			{
				m_rebuildFailed.push_back(assetPath);
			}

			++m_rebuildIndex;
		} while (m_rebuildIndex < m_rebuildQueue.size() && (std::chrono::steady_clock::now() - tickStart) < frameBudget);

		ax::NodeEditor::SetCurrentEditor(previousContext);

		if (m_rebuildIndex >= m_rebuildQueue.size())
		{
			m_rebuildInProgress = false;
			m_rebuildQueue.clear();

			if (m_rebuildEdContext)
			{
				ax::NodeEditor::DestroyEditor(static_cast<ax::NodeEditor::EditorContext*>(m_rebuildEdContext));
				m_rebuildEdContext = nullptr;
			}

			ILOG("Material rebuild finished: " << m_rebuildSucceeded << " succeeded, " << m_rebuildFailed.size() << " failed");
			m_showRebuildResultPopup = true;
		}
	}

	void MaterialEditor::AddAssetActions(const String& asset)
	{
		// TODO: this should probably be moved to the MaterialEditorInstance class but the editor interface is not yet compatible with injecting
		// actions from other editor instances to different asset types.
		if (ImGui::MenuItem("Create Material Instance"))
		{
			m_selectedMaterial = MaterialManager::Get().Load(asset);

			m_materialInstanceName = Path(asset).filename().replace_extension().string() + "_Instance";
			m_showMaterialInstanceDialog = true;
		}
	}

	void MaterialEditor::DrawImpl()
	{
		if (m_showMaterialNameDialog)
		{
			ImGui::OpenPopup("Create New Material");
			m_showMaterialNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material:");

			ImGui::InputText("##field", &m_materialName);
			ImGui::SameLine();
			ImGui::Text(MaterialExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewMaterial();
				ImGui::CloseCurrentPopup();
			}
			
			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (m_showMaterialFunctionNameDialog)
		{
			ImGui::OpenPopup("Create New Material Function");
			m_showMaterialFunctionNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Material Function", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material function:");

			ImGui::InputText("##field", &m_materialFunctionName);
			ImGui::SameLine();
			ImGui::Text(MaterialFunctionExtension.c_str());

			if (ImGui::Button("Create"))
			{
				CreateNewMaterialFunction();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		// Incremental material rebuild: process a slice of the queue each frame and show progress.
		TickMaterialRebuild();

		if (m_rebuildInProgress && !ImGui::IsPopupOpen("Rebuilding Materials"))
		{
			ImGui::OpenPopup("Rebuilding Materials");
		}

		if (ImGui::BeginPopupModal("Rebuilding Materials", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		{
			const size_t total = m_rebuildQueue.size();
			const float fraction = (total > 0) ? static_cast<float>(m_rebuildIndex) / static_cast<float>(total) : 1.0f;

			ImGui::Text("Recompiling materials, please wait ...");
			ImGui::ProgressBar(fraction, ImVec2(350.0f, 0.0f));

			if (m_rebuildIndex < total)
			{
				ImGui::TextUnformatted(m_rebuildQueue[m_rebuildIndex].c_str());
			}

			if (!m_rebuildInProgress)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (m_showRebuildResultPopup)
		{
			ImGui::OpenPopup("Material Rebuild Result");
			m_showRebuildResultPopup = false;
		}

		if (ImGui::BeginPopupModal("Material Rebuild Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Successfully rebuilt %u materials.", m_rebuildSucceeded);

			if (!m_rebuildFailed.empty())
			{
				ImGui::Separator();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%zu materials failed to rebuild:", m_rebuildFailed.size());

				if (ImGui::BeginChild("##failedMaterials", ImVec2(400.0f, 150.0f), true))
				{
					for (const String& failed : m_rebuildFailed)
					{
						ImGui::TextUnformatted(failed.c_str());
					}
				}
				ImGui::EndChild();
			}

			if (ImGui::Button("OK"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (m_showMaterialInstanceDialog)
		{
			ImGui::OpenPopup("Create Material Instance");
			m_showMaterialInstanceDialog = false;
		}

		if (ImGui::BeginPopupModal("Create Material Instance", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material instance:");

			ImGui::InputText("##field", &m_materialInstanceName);
			ImGui::SameLine();
			ImGui::Text(".hmi");

			if (ImGui::Button("Create"))
			{
				CreateNewMaterialInstance();
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

	std::shared_ptr<EditorInstance> MaterialEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<MaterialEditorInstance>(*this, m_host, asset));
		if (!instance.second)
		{
			ELOG("Failed to open model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void MaterialEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void MaterialEditor::CreateNewMaterial()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialName + MaterialExtension;
		m_materialName.clear();
		
		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material");
			return;
		}

		const std::shared_ptr<Material> material = std::make_shared<Material>(currentPath.string());
		material->SetType(MaterialType::Opaque);
		material->SetCastShadows(true);
		material->SetReceivesShadows(true);
		material->SetTwoSided(false);

		const auto materialCompiler = GraphicsDevice::Get().CreateMaterialCompiler();
		const auto shaderCompiler = GraphicsDevice::Get().CreateShaderCompiler();
		materialCompiler->Compile(*material, *shaderCompiler);
		material->Update();

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialSerializer serializer;
		serializer.Export(*material, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	void MaterialEditor::CreateNewMaterialFunction()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialFunctionName + MaterialFunctionExtension;
		m_materialFunctionName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material function");
			return;
		}

		// TODO: Add material function creation logic here

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	void MaterialEditor::CreateNewMaterialInstance()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialInstanceName + ".hmi";
		m_materialInstanceName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material instance");
			return;
		}

		const auto instance = std::make_shared<MaterialInstance>(currentPath.string(), m_selectedMaterial);
		instance->SetName(currentPath.string());
		instance->DerivePropertiesFromParent();
		instance->RefreshParametersFromBase();
		instance->Update();

		io::StreamSink sink(*file);
		io::Writer writer{sink};

		MaterialInstanceSerializer serializer;
		serializer.Export(*instance, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}
}
