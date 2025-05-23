// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_editor_instance.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.inl"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "node_editor/item_builder.h"
#include "node_editor/item_deleter.h"
#include "editors/material_editor/material_graph.h"
#include "editors/material_editor/material_node.h"
#include "node_editor/node_header_renderer.h"
#include "node_editor/node_layout.h"
#include "node_editor/node_pin_icons.h"

#include <cinttypes>

#include "material_editor.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_serializer.h"
#include "scene_graph/material_manager.h"
#include "graphics/shader_compiler.h"
#include "preview_providers/preview_provider_manager.h"
#include "node_editor/node_builder.h"
#include "node_editor/link_builder.h"

// Add namespace alias for the node editor
namespace ed = ax::NodeEditor;

namespace mmo
{
	class ScopedItemWidth final
	{
	public:
		ScopedItemWidth(const float width)
	    {
			ImGui::PushItemWidth(width);
	    }
	    ~ScopedItemWidth()
	    {
			Release();
	    }

	public:
	    void Release()
	    {
		    if (m_released)
		    {
		    	return;
			}

		    ImGui::PopItemWidth();
		    m_released = true;
	    }

	private:
	    bool m_released = false;
	};
	
	IconType PinTypeToIconType(const PinType pinType)
	{
	    switch (pinType)
	    {
	        case PinType::Material: return IconType::Circle;
	    }

	    return IconType::Circle;
	}

	ImVec4 PinTypeToColor(const PinType pinType)
	{
	    switch (pinType)
	    {
		case PinType::Material: return ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
	    }

		return ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
	}
		
	void CommitMaterialNodes(MaterialGraph& material)
    {
        const auto iconSize = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());
        
        // Commit all nodes to editor
        for (auto& node : material.GetNodes())
        {
            ed::BeginNode(node->GetId());

            // General node layout:
            //
            // +-----------------------------------+
            // | Title                             |
            // | +-----------[ Dummy ]-----------+ |
            // | +---------------+   +-----------+ |
            // | | o Pin         |   |   Out B o | |
            // | | o Pin <Value> |   |   Out A o | |
            // | | o Pin         |   |           | |
            // | +---------------+   +-----------+ |
            // +-----------------------------------+

            // Show title if node has one.
            auto nodeName = node->GetName();
            if (!nodeName.empty())
            {
                auto headerBackgroundRenderer = NodeHeaderRenderer([node](ImDrawList* drawList)
                {
                    const auto border   = ed::GetStyle().NodeBorderWidth;
                    const auto rounding = ed::GetStyle().NodeRounding;

                    auto itemMin = ImGui::GetItemRectMin();
                    auto itemMax = ImGui::GetItemRectMax();

                    const auto nodeStart = ed::GetNodePosition(node->GetId());
                    const auto nodeSize  = ed::GetNodeSize(node->GetId());

                    itemMin   = nodeStart;
                    itemMin.x = itemMin.x + border - 0.5f;
                    itemMin.y = itemMin.y + border - 0.5f;
                    itemMax.x = nodeStart.x + nodeSize.x - border + 0.5f;
                    itemMax.y = itemMax.y + ImGui::GetStyle().ItemSpacing.y + 0.5f;
					
                    drawList->AddRectFilled(itemMin, itemMax, node->GetColor(), rounding, ImDrawCornerFlags_Top);
                });

                //ImGui::PushFont(HeaderFont());
                ImGui::TextUnformatted(nodeName.data(), nodeName.data() + nodeName.size());
                //ImGui::PopFont();
                
                ImGui::Spacing();
            }

            ImGui::Dummy(ImVec2(100.0f, 0.0f)); // For minimum node width

			GridLayout layout;
            layout.Begin(node->GetId(), 2, 100.0f);
            layout.SetColumnAlignment(0.0f);

            // Draw column with input pins.
            for (const auto& pin : node->GetInputPins())
            {
                // Add a bit of spacing to separate pins and make value not cramped
                ImGui::Spacing();

                // Input pin layout:
                //
                //     +-[1]---+-[2]------+-[3]----------+
                //     |       |          |              |
                //    [X] Icon | Pin Name | Value/Editor |
                //     |       |          |              |
                //     +-------+----------+--------------+

                ed::BeginPin(pin->GetId(), ed::PinKind::Input);
                // [X] - Tell editor to put pivot point in the middle of
                //       the left side of the pin. This is the point
                //       where link will be hooked to.
                //
                //       By default pivot is in pin center point which
                //       does not look good for blueprint nodes.
                ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));

                // [1] - Icon
                Icon(iconSize,
                    PinTypeToIconType(pin->GetType()),
                    material.HasPinAnyLink(*pin),
                    PinTypeToColor(pin->GetValueType()));

                // [2] - Show pin name if it has one
                if (!pin->GetName().empty())
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(pin->GetName().data(), pin->GetName().data() + pin->GetName().size());
                }
				
                ed::EndPin();
                
                layout.NextRow();
            }

            layout.SetColumnAlignment(1.0f);
            layout.NextColumn();

            // Draw column with output pins.
            for (const auto& pin : node->GetOutputPins())
            {
                // Add a bit of spacing to separate pins and make value not cramped
                ImGui::Spacing();

                // Output pin layout:
                //
                //    +-[1]------+-[2]---+
                //    |          |       |
                //    | Pin Name | Icon [X]
                //    |          |       |
                //    +----------+-------+

                ed::BeginPin(pin->GetId(), ed::PinKind::Output);

                // [X] - Tell editor to put pivot point in the middle of
                //       the right side of the pin. This is the point
                //       where link will be hooked to.
                //
                //       By default pivot is in pin center point which
                //       does not look good for blueprint nodes.
                ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));

                // [1] - Show pin name if it has one
                if (!pin->GetName().empty())
                {
                    ImGui::TextUnformatted(pin->GetName().data(), pin->GetName().data() + pin->GetName().size());
                    ImGui::SameLine();
                }

                // [2] - Show icon
                Icon(iconSize,
                    PinTypeToIconType(pin->GetType()),
                    material.HasPinAnyLink(*pin),
                    PinTypeToColor(pin->GetValueType()));

                ed::EndPin();
				
                layout.NextRow();
            }

            layout.End();
			
            ed::EndNode();
        }

        // Commit all links to editor
        for (const auto& pin : material.GetPins())
        {
            if (!pin->GetLink())
                continue;

            // To keep things simple, link id is same as pin id.
            ed::Link(pin->GetId(), pin->GetId(), pin->GetLink()->GetId(), PinTypeToColor(pin->GetValueType()));
        }
    }
	
    void MaterialEditorInstance::HandleCreateAction(MaterialGraph& material)
    {
        ItemBuilder itemBuilder;
        if (!itemBuilder)
            return;

        if (const auto linkBuilder = itemBuilder.QueryNewLink())
        {
            auto startPin = material.FindPin(static_cast<uint32_t>(linkBuilder->startPinId.Get()));
            auto endPin   = material.FindPin(static_cast<uint32_t>(linkBuilder->endPinId.Get()));

            // Editor return pins in order draw by the user. It is up to the
            // user to determine if it is valid. In blueprints we accept only links
            // from receivers to providers. Other graph types may allow bi-directional
            // links between nodes and this ordering make this feature possible.
            if (endPin->IsInput() && startPin->IsOutput())
                ImSwap(startPin, endPin);

            if (const auto canLinkResult = startPin->CanLinkTo(*endPin))
            {
# define PRI_sv             ".*s"
# define FMT_sv(sv)         static_cast<int>((sv).size()), (sv).data()

# define PRI_pin            "s %" PRIu32 "%s%" PRI_sv "%s"
# define FMT_pin(pin)       "Pin", (pin)->GetId(), (pin)->GetName().empty() ? "" : " \"", FMT_sv((pin)->GetName()), (pin)->GetName().empty() ? "" : "\""

# define PRI_node           "s %" PRIu32 "%s%" PRI_sv "%s"
# define FMT_node(node)     "Node", (node)->GetId(), (node)->GetName().empty() ? "" : " \"", FMT_sv((node)->GetName()), (node)->GetName().empty() ? "" : "\""

                ed::Suspend();
                ImGui::BeginTooltip();
                ImGui::Text("Valid Link%s%s",
                    canLinkResult.GetReason().empty() ? "" : ": ",
                    canLinkResult.GetReason().empty() ? "" : canLinkResult.GetReason().data());
                ImGui::Separator();
                ImGui::TextUnformatted("From:");
                ImGui::Bullet(); ImGui::Text("%" PRI_pin, FMT_pin(startPin));
                ImGui::Bullet(); ImGui::Text("%" PRI_node, FMT_node(startPin->GetNode()));
                ImGui::TextUnformatted("To:");
                ImGui::Bullet(); ImGui::Text("%" PRI_pin, FMT_pin(endPin));
                ImGui::Bullet(); ImGui::Text("%" PRI_node, FMT_node(endPin->GetNode()));
                ImGui::EndTooltip();
                ed::Resume();

                if (linkBuilder->Accept())
                {
                    startPin->LinkTo(*endPin);
                }
            }
            else
            {
                ed::Suspend();
                ImGui::SetTooltip(
                    "Invalid Link: %s",
                    canLinkResult.GetReason().data()
                );
                ed::Resume();

                linkBuilder->Reject();
            }
        }
        else if (const auto nodeBuilder = itemBuilder.QueryNewNode())
        {
            // Arguably creation of node is simpler than a link.
            ed::Suspend();
            ImGui::SetTooltip("Create Node...");
            ed::Resume();

            // Node builder accept return true when user release mouse button.
            // When this happen we request CreateNodeDialog to open.
            if (nodeBuilder->Accept())
            {
                // Get node from which link was pulled (if any). After creating
                // node we will try to make link with first matching pin of the node.
                auto pin = material.FindPin(static_cast<uint32_t>(nodeBuilder->pinId.Get()));

                ed::Suspend();
				m_createDialog.Open(pin);
                ed::Resume();
            }
        }
    }

    void MaterialEditorInstance::RenderMaterialPreview()
    {
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_camera->InvalidateView();

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		
		m_viewportRT->Update();
    }

    void MaterialEditorInstance::HandleDeleteAction(MaterialGraph& material)
    {
		ItemDeleter itemDeleter;
        if (!itemDeleter)
        {
	        return;
        }
		
		std::vector<GraphNode*> nodesToDelete;
        uint32_t brokenLinkCount = 0;

        // Process all nodes marked for deletion
        while (auto* nodeDeleter = itemDeleter.QueryDeletedNode())
        {
			if (material.IsRootNode(nodeDeleter->nodeId.Get()))
			{
				nodeDeleter->Reject();
			}
			else
			{
				// Remove node, pass 'true' so links attached to node will also be queued for deletion.
	            if (nodeDeleter->Accept(true))
	            {
	                auto node = material.FindNode(static_cast<uint32_t>(nodeDeleter->nodeId.Get()));
	                if (node != nullptr)
	                {
	                    // Queue nodes for deletion. We need to serve links first to avoid crash.
	                    nodesToDelete.push_back(node);
					}
	            }
			}
        }

        // Process all links marked for deletion
        while (auto* linkDeleter = itemDeleter.QueryDeleteLink())
        {
            if (linkDeleter->Accept())
            {
	            const auto startPin = material.FindPin(static_cast<uint32_t>(linkDeleter->startPinId.Get()));
                if (startPin != nullptr && startPin->IsLinked())
                {
                    startPin->Unlink();
                    ++brokenLinkCount;
                }
            }
        }

        // After links was removed, now it is safe to delete nodes.
        for (const auto* node : nodesToDelete)
        {
            material.DeleteNode(node);
        }
    }
	
    void MaterialEditorInstance::HandleContextMenuAction(MaterialGraph& material)
    {
		if (ed::ShowBackgroundContextMenu())
        {
            ed::Suspend();
            m_createDialog.Open();
            ed::Resume();
        }

        ed::NodeId contextNodeId;
        if (ed::ShowNodeContextMenu(&contextNodeId))
        {
            auto node = material.FindNode(static_cast<uint32_t>(contextNodeId.Get()));

            ed::Suspend();
            //m_nodeContextMenu.Open(node);
            ed::Resume();
        }

        ed::PinId contextPinId;
        if (ed::ShowPinContextMenu(&contextPinId))
        {
            auto pin = material.FindPin(static_cast<uint32_t>(contextPinId.Get()));

            ed::Suspend();
            //m_pinContextMenu.Open(pin);
            ed::Resume();
        }

        ed::LinkId contextLinkId;
        if (ed::ShowLinkContextMenu(&contextLinkId))
        {
            auto pin = material.FindPin(static_cast<uint32_t>(contextLinkId.Get()));

            ed::Suspend();
            //m_linkContextMenu.Open(pin);
            ed::Resume();
        }
    }

    void CreateNodeDialog::Open(Pin* fromPin)
	{
		const auto storage = ImGui::GetStateStorage();
	    storage->SetVoidPtr(ImGui::GetID("##create_node_pin"), fromPin);
	    ImGui::OpenPopup("##create_node");

	    m_SortedNodes.clear();
	}

	void CreateNodeDialog::Show(MaterialGraph& material)
	{
		if (!ImGui::IsPopupOpen("##create_node"))                                 
		{
			return;
		}

		const auto storage = ImGui::GetStateStorage();
		const auto fromPin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##create_node_pin")));

		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 2.0f);

	    if (ImGui::BeginPopup("##create_node"))
	    {
			const auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();

		    if (m_SortedNodes.empty())
		    {
			    const auto nodeRegistry = material.GetNodeRegistry();

		        auto types = nodeRegistry->GetTypes();

		        m_SortedNodes.assign(types.begin(), types.end());
		        std::sort(m_SortedNodes.begin(), m_SortedNodes.end(), [](const auto& lhs, const auto& rhs)
		        {
		            return std::lexicographical_compare(
		                lhs->displayName.begin(), lhs->displayName.end(),
		                rhs->displayName.begin(), rhs->displayName.end());
		        });
		    }

			if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
			{
				ImGui::SetKeyboardFocusHere(0);
			}
			m_filter.Draw("Filter");

			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1019f, 0.1019f, 0.1019f, 1.0f));
			ImGui::BeginChild("scrolling", ImVec2(0, 400), false, ImGuiWindowFlags_HorizontalScrollbar);
			{
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

				if (m_filter.IsActive())
				{
				    for (const auto nodeTypeInfo : m_SortedNodes)
				    {
						const auto displayInfo = nodeTypeInfo->displayName;
						if (!m_filter.PassFilter(displayInfo.data()))
						{
							continue;
						}

						bool selected = false;
				        if (ImGui::Selectable(nodeTypeInfo->displayName.data(), &selected))
				        {
					        const auto node = material.CreateNode(nodeTypeInfo->id);
				            auto nodePosition = ax::NodeEditor::ScreenToCanvas(popupPosition);

				            ax::NodeEditor::SetNodePosition(node->GetId(), nodePosition);

				            ax::NodeEditor::SelectNode(node->GetId());

				            m_CreatedNode = node;
				            m_CreatedLinks.clear();

				            if (fromPin)
				            {
								CreateLinkToFirstMatchingPin(*node, *fromPin);
				            }

							m_filter.Clear();
							ImGui::CloseCurrentPopup();
							break;
				        }
					}
				}
				else
				{
					ImGuiListClipper clipper;
					clipper.Begin(m_SortedNodes.size());
					while (clipper.Step())
					{
						for (int lineNo = clipper.DisplayStart; lineNo < clipper.DisplayEnd; lineNo++)
						{
							const auto& nodeTypeInfo = m_SortedNodes[lineNo];
							
							bool selected = false;
					        if (ImGui::Selectable(nodeTypeInfo->displayName.data(), &selected))
					        {
					            auto node = material.CreateNode(nodeTypeInfo->id);
					            auto nodePosition = ax::NodeEditor::ScreenToCanvas(popupPosition);

					            ax::NodeEditor::SetNodePosition(node->GetId(), nodePosition);

					            ax::NodeEditor::SelectNode(node->GetId());

					            m_CreatedNode = node;
					            m_CreatedLinks.clear();

					            if (fromPin)
					            {
									CreateLinkToFirstMatchingPin(*node, *fromPin);
					            }
								
								m_filter.Clear();
								ImGui::CloseCurrentPopup();
								break;
					        }
						}
					}
					clipper.End();
				}

				ImGui::PopStyleVar();

				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				{
					ImGui::SetScrollHereY(1.0f);
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		
	    }

		ImGui::PopStyleVar();

	    ImGui::EndPopup();
	}

	std::vector<Pin*> CreateNodeDialog::CreateLinkToFirstMatchingPin(GraphNode& node, Pin& fromPin)
	{
		for (auto nodePin : node.GetInputPins())
	    {
	        if (nodePin->LinkTo(fromPin))
	            return { nodePin };

	        if (fromPin.LinkTo(*nodePin))
	            return { &fromPin };
	    }

	    for (auto nodePin : node.GetOutputPins())
	    {
	        if (nodePin->LinkTo(fromPin))
	            return { nodePin };

	        if (fromPin.LinkTo(*nodePin))
	            return { &fromPin };
	    }

	    return {};
	}

	MaterialEditorInstance::MaterialEditorInstance(MaterialEditor& editor, EditorHost& host, const Path& assetPath)
		: EditorInstance(host, assetPath)
		, m_editor(editor)
	{
		ed::Config editorConfig;
		editorConfig.SettingsFile = nullptr;
	    m_context = ed::CreateEditor(&editorConfig);
		ed::SetCurrentEditor(m_context);

		m_material = std::static_pointer_cast<Material>(MaterialManager::Get().CreateManual(assetPath.string()));
		m_graph = std::make_unique<MaterialGraph>();

		std::shared_ptr<NodeRegistry> registry = m_graph->GetNodeRegistry();
		ASSERT(registry);

		// Ensure material root node is created
		if (assetPath.extension() != ".hmf")
		{
			// Custom nodes only available in materials
			registry->RegisterNodeType(MaterialNode::GetStaticTypeInfo());

			m_graph->CreateNode<MaterialNode>(true);
		}
		else
		{
			// Custom nodes only available in material functions
			registry->RegisterNodeType(MaterialFunctionOutputNode::GetStaticTypeInfo());
			registry->RegisterNodeType(MaterialFunctionInputNode::GetStaticTypeInfo());

			m_graph->CreateNode<MaterialFunctionOutputNode>(false);
		}

		ExecutableMaterialGraphLoadContext context;

		if (const auto file = AssetRegistry::OpenFile(assetPath.string()))
		{
			io::StreamSource source { *file };
			io::Reader reader { source };

			if (assetPath.extension() != ".hmf")
			{
				MaterialDeserializer deserializer{ *m_material };
				deserializer.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [this, &context](io::Reader& reader, uint32, uint32) -> bool
					{
						return m_graph->Deserialize(reader, context);
					});

				if (!deserializer.Read(reader) || !context.PerformAfterLoadActions())
				{
					ELOG("Unable to read material file!");
				}
				else
				{
					m_material->Update();
				}
			}
			else
			{
				ChunkReader chunkReader;
				chunkReader.SetIgnoreUnhandledChunks(true);
				chunkReader.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [this, &context](io::Reader& reader, uint32, uint32) -> bool
					{
						return m_graph->Deserialize(reader, context);
					});

				if (!chunkReader.Read(reader) || !context.PerformAfterLoadActions())
				{
					ELOG("Unable to read material function file!");
				}
			}
		}
		else
		{
			ELOG("Unable to load file " << assetPath << ": File does not exist!");
		}
		
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));
		m_cameraAnchor->Yaw(Degree(-45.0f), TransformSpace::World);

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);
		
		m_entity = m_scene.CreateEntity(assetPath.string(), "Editor/Sphere.hmsh");
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);

			if (m_material->GetVertexShader(VertexShaderType::Default))
			{
				m_entity->SetMaterial(m_material);
			}
			else
			{
				m_entity->SetMaterial(m_scene.GetDefaultMaterial());
			}
		}

		m_renderConnection = host.beforeUiUpdate.connect(this, &MaterialEditorInstance::RenderMaterialPreview);
		m_material->SetName(assetPath.string());
	}

	MaterialEditorInstance::~MaterialEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}
		
		m_scene.Clear();
	}

	void MaterialEditorInstance::Compile() const
	{
		const auto materialCompiler = GraphicsDevice::Get().CreateMaterialCompiler();
		m_graph->Compile(*materialCompiler);

		const auto shaderCompiler = GraphicsDevice::Get().CreateShaderCompiler();
		materialCompiler->Compile(*m_material, *shaderCompiler);

		m_material->Update();

		if (m_entity)
		{
			m_entity->SetMaterial(m_material);
		}

		m_editor.GetPreviewManager().InvalidatePreview(m_assetPath.string());
	}

	bool MaterialEditorInstance::Save()
	{
		ed::SetCurrentEditor(m_context);
		
		m_material->SetName(GetAssetPath().string());

		// Ensure that the material is compiled
		Compile();

		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open material file " << GetAssetPath() << " for writing!");
			return false;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		// Ensure material root node is created
		if (GetAssetPath().extension() != ".hmf")
		{
			// This is a material, serialize it as such
			MaterialSerializer serializer{ };
			serializer.Export(*m_material, writer);
		}
		else
		{
			// Find the input and output nodes in the function graph
			std::map<String, MaterialFunctionInputNode*> inputNodes;
			std::map<String, MaterialFunctionOutputNode*> outputNodes;
			for (auto node : m_graph->GetNodes())
			{
				if (auto* inputNode = dynamic_cast<MaterialFunctionInputNode*>(node))
				{
					inputNodes[String(inputNode->GetName())] = inputNode;
				}
				else if (auto* outputNode = dynamic_cast<MaterialFunctionOutputNode*>(node))
				{
					outputNodes[String(outputNode->GetName())] = outputNode;
				}
			}

			{
				ChunkWriter inputChunk{ ChunkMagic({ 'I', 'N', 'P', 'S' }), writer };
				writer << io::write<uint32>(inputNodes.size());
				for (const auto& [name, inputNode] : inputNodes)
				{
					writer << io::write_dynamic_range<uint8>(name) << static_cast<uint8>(inputNode->GetParameterType());
				}
				inputChunk.Finish();
			}

			{
				ChunkWriter outputChunk{ ChunkMagic({ 'O', 'U', 'T', 'P' }), writer };
				writer << io::write<uint32>(outputNodes.size());
				for (const auto& [name, outputNode] : outputNodes)
				{
					writer << io::write_dynamic_range<uint8>(name) << static_cast<uint8>(outputNode->GetParameterType());
				}
				outputChunk.Finish();
			}
		}

		// Serialize the material graph as well
		m_graph->Serialize(writer);

		m_editor.GetPreviewManager().InvalidatePreview(m_assetPath.string());
		return true;
	}

	void MaterialEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("MaterialGraph");
		ImGui::DockSpace(dockspaceId, ImVec2(-1.0f, -1.0f), ImGuiDockNodeFlags_AutoHideTabBar);
		
		// Set the editor context for this material
		ed::SetCurrentEditor(m_context);

		const String previewId = "Preview##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String graphId = "Material Graph##" + GetAssetPath().string();

		DrawPreviewPanel(previewId);
		DrawDetailsPanel(detailsId);
		DrawGraphPanel(graphId);
		
		InitializeDockLayout(dockspaceId, previewId, detailsId, graphId);

		ed::SetCurrentEditor(nullptr);
		ImGui::PopID();
	}

	void MaterialEditorInstance::DrawPreviewPanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str()))
		{
			DrawPreviewToolbar();
			DrawPreviewViewport();
		}
		ImGui::End();
	}

	void MaterialEditorInstance::DrawPreviewToolbar()
	{
		if (ImGui::Button("Compile"))
		{
			Compile();
		}

		ImGui::SameLine();

		if (ImGui::Button("Save"))
		{
			Save();
		}
	}

	void MaterialEditorInstance::DrawPreviewViewport()
	{
		if (ImGui::BeginChild("previewPanel", ImVec2(-1, -1)))
		{
			// Determine the current viewport position
			auto viewportPos = ImGui::GetWindowContentRegionMin();
			viewportPos.x += ImGui::GetWindowPos().x;
			viewportPos.y += ImGui::GetWindowPos().y;

			// Determine the available size for the viewport window and either create the render target
			// or resize it if needed
			const auto availableSpace = ImGui::GetContentRegionAvail();
			
			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport_" + GetAssetPath().string(), std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y), RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the render target content into the window as image object
			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_leftButtonPressed = true;
			}
		}
		ImGui::EndChild();
	}

	void MaterialEditorInstance::DrawDetailsPanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str(), nullptr))
		{
			DrawPropertyTable();
		}
		ImGui::End();
	}

	void MaterialEditorInstance::DrawPropertyTable()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
		{
			ed::NodeId selectedNode;
			if (ed::GetSelectedNodes(&selectedNode, 1) > 0)
			{
				auto* node = m_graph->FindNode(selectedNode.Get());
				if (node)
				{
					DrawNodeProperties(node);
				}
			}
			
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
	}

	void MaterialEditorInstance::DrawNodeProperties(GraphNode* node)
	{
		for (auto* prop : node->GetProperties())
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
			ImGui::TreeNodeEx("Field", flags, prop->GetName().data());

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);

			DrawPropertyEditor(prop);
		}
	}

	void MaterialEditorInstance::DrawPropertyEditor(PropertyBase* prop)
	{
		if (const auto* floatValue = prop->GetValueAs<float>())
		{
			float value = *floatValue;
			
			if (ImGui::InputFloat(prop->GetName().data(), &value, 0.1f, 100))
			{
				prop->SetValue(value);
			}
		}
		else if (const auto* boolValue = prop->GetValueAs<bool>())
		{
			bool value = *boolValue;
			if (ImGui::Checkbox(prop->GetName().data(), &value))
			{
				prop->SetValue(value);
			}
		}
		else if (const auto* intValue = prop->GetValueAs<int32>())
		{
			int32 value = *intValue;
			if (ImGui::InputInt(prop->GetName().data(), &value, 1, 100, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank))
			{
				prop->SetValue(value);
			}
		}
		else if (const auto* strValue = prop->GetValueAs<String>())
		{
			String value = *strValue;
			if (ImGui::InputText(prop->GetName().data(), &value))
			{
				prop->SetValue(value);
			}
		}
		else if (const auto* colValue = prop->GetValueAs<Color>())
		{
			Color value = *colValue;
			if (ImGui::ColorEdit4(prop->GetName().data(), value))
			{
				prop->SetValue(value);
			}
			if (ImGui::InputFloat4(String(String("##") + prop->GetName().data()).c_str(), value))
			{
				prop->SetValue(value);
			}
		}
		else if (const auto* pathValue = prop->GetValueAs<AssetPathValue>())
		{
			if (ImGui::BeginCombo(prop->GetName().data(), !pathValue->GetPath().empty() ? pathValue->GetPath().data() : "(None)", ImGuiComboFlags_HeightLargest))
			{
				if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
				{
					ImGui::SetKeyboardFocusHere(0);
				}
				m_assetFilter.Draw("##asset_filter");

				if (ImGui::BeginChild("##asset_scroll_area", ImVec2(0, 400)))
				{
					const auto files = AssetRegistry::ListFiles();
					for (auto& file : files)
					{
						if (!pathValue->GetFilter().empty())
						{
							if (!file.ends_with(pathValue->GetFilter()))
								continue;
						}

						if (m_assetFilter.IsActive())
						{
							if (!m_assetFilter.PassFilter(file.c_str()))
							{
								continue;
							}
						}
						
						ImGui::PushID(file.c_str());
						if (ImGui::Selectable(Path(file).filename().string().c_str()))
						{
							prop->SetValue(AssetPathValue(file, pathValue->GetFilter()));

							m_assetFilter.Clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::PopID();
					}
				}
				ImGui::EndChild();
				
				ImGui::EndCombo();
			}
		}
	}

	void MaterialEditorInstance::DrawGraphPanel(const String& panelId)
	{
		if(ImGui::Begin(panelId.c_str()))
		{
			ed::Begin(GetAssetPath().string().c_str(), ImVec2(0.0, 0.0f));
			
			CommitMaterialNodes(*m_graph);

			HandleCreateAction(*m_graph);
			HandleDeleteAction(*m_graph);
			HandleContextMenuAction(*m_graph);
			
			ed::Suspend();
			m_createDialog.Show(*m_graph);
			ed::Resume();
			
			ed::End();
		}
		ImGui::End();
	}

	void MaterialEditorInstance::InitializeDockLayout(ImGuiID dockspaceId, const String& previewId, const String& detailsId, const String& graphId)
	{
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
			ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockspaceId;
			auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			auto sideTopId = ImGui::DockBuilderSplitNode(sideId, ImGuiDir_Up, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &sideId);
			
			ImGui::DockBuilderDockWindow(graphId.c_str(), mainId);

			ImGui::DockBuilderDockWindow(previewId.c_str(), sideTopId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void MaterialEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseButtonDown(button, x, y);
		
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void MaterialEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseButtonUp(button, x, y);
		
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
	}

	void MaterialEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		EditorInstance::OnMouseMoved(x, y);
		
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}
}
