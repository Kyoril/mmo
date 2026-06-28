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

#include <algorithm>
#include <cinttypes>

#include "material_editor.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "editor_windows/asset_picker_widget.h"
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

	void MaterialEditorInstance::Compile()
	{
		const auto materialCompiler = GraphicsDevice::Get().CreateMaterialCompiler();

		// Always emit debug comments so the shader code viewer can map the generated numbered
		// expressions back to the nodes in the graph. Comments are stripped by the shader compiler
		// and have no effect on the produced binary shader code.
		materialCompiler->SetGenerateDebugComments(true);

		m_graph->Compile(*materialCompiler);

		// After the graph has been compiled, every node knows the expression index it produced.
		// Annotate those expressions with the node name and id so the generated HLSL can be aligned
		// with the visual node graph.
		for (const auto* node : m_graph->GetNodes())
		{
			const ExpressionIndex exprId = node->GetCompiledExpressionId();
			if (exprId == IndexNone)
			{
				continue;
			}

			std::ostringstream label;
			const auto nodeName = node->GetName();
			label << "Node \"" << (nodeName.empty() ? node->GetTypeInfo().displayName : nodeName)
				<< "\" (#" << node->GetId() << ")";
			materialCompiler->AnnotateExpression(exprId, label.str());
		}

		const auto shaderCompiler = GraphicsDevice::Get().CreateShaderCompiler();
		materialCompiler->Compile(*m_material, *shaderCompiler);

		// Keep the generated high level HLSL around so it can be inspected in the shader code panel.
		CaptureGeneratedShaderCode(*materialCompiler);

		m_material->Update();

		if (m_entity)
		{
			m_entity->SetMaterial(m_material);
		}

		m_editor.GetPreviewManager().InvalidatePreview(m_assetPath.string());
	}

	void MaterialEditorInstance::CaptureGeneratedShaderCode(const MaterialCompiler& compiler)
	{
		for (int i = 0; i < static_cast<int>(m_vertexShaderCode.size()); ++i)
		{
			m_vertexShaderCode[i] = compiler.GetVertexShaderCode(static_cast<VertexShaderType>(i));
		}

		for (int i = 0; i < static_cast<int>(m_pixelShaderCode.size()); ++i)
		{
			m_pixelShaderCode[i] = compiler.GetPixelShaderCode(static_cast<PixelShaderType>(i));
		}

		m_shaderCodeCaptured = true;
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
		const String shaderCodeId = "Shader Code##" + GetAssetPath().string();

		DrawPreviewPanel(previewId);
		DrawDetailsPanel(detailsId);
		DrawGraphPanel(graphId);
		DrawShaderCodePanel(shaderCodeId);

		InitializeDockLayout(dockspaceId, previewId, detailsId, graphId, shaderCodeId);

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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawPropertyTable();

			ImGui::Spacing();

			// Terrain foliage authoring is only relevant for materials (not material functions).
			if (m_material && GetAssetPath().extension() != ".hmf")
			{
				DrawFoliageSection();
			}

			ImGui::PopStyleVar(2);
		}
		ImGui::End();
	}

	void MaterialEditorInstance::DrawPropertyTable()
	{
		ed::NodeId selectedNodeId;
		const bool hasSelection = ed::GetSelectedNodes(&selectedNodeId, 1) > 0;
		GraphNode* node = hasSelection ? m_graph->FindNode(selectedNodeId.Get()) : nullptr;

		const String headerLabel = node
			? (String("Node Properties - ") + String(node->GetName().empty() ? node->GetTypeInfo().displayName : node->GetName()))
			: "Node Properties";

		if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (!node)
			{
				ImGui::TextDisabled("Select a node in the graph to edit its properties.");
			}
			else if (node->GetProperties().empty())
			{
				ImGui::TextDisabled("This node has no editable properties.");
			}
			else
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
				if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
				{
					DrawNodeProperties(node);
					ImGui::EndTable();
				}
				ImGui::PopStyleVar();
			}

			ImGui::Unindent();
		}
	}

	void MaterialEditorInstance::DrawFoliageSection()
	{
		static const std::set<String> s_meshExtensions = { ".hmsh" };
		if (!ImGui::CollapsingHeader("Terrain Foliage"))
		{
			return;
		}

		ImGui::TextWrapped("Foliage scattered on terrain tiles using this material. Each entry is bound to "
			"a terrain layer (1-4) and grows where that layer's coverage is high.");

		auto& entries = m_material->GetFoliageEntries();

		static const char* s_layerNames[] = { "Layer 1", "Layer 2", "Layer 3", "Layer 4" };

		int removeIndex = -1;
		for (int i = 0; i < static_cast<int>(entries.size()); ++i)
		{
			MaterialFoliageEntry& entry = entries[i];

			ImGui::PushID(i);

			const String headerLabel = "Foliage " + std::to_string(i + 1) +
				(entry.meshPath.empty() ? "" : (" - " + entry.meshPath));
			if (ImGui::TreeNodeEx("foliageEntry", ImGuiTreeNodeFlags_DefaultOpen, "%s", headerLabel.c_str()))
			{
				// Mesh field (accepts a .hmsh drag-drop payload).
				String meshPath = entry.meshPath;
				if (AssetPickerWidget::Draw("##mesh", meshPath, s_meshExtensions, &m_editor.GetPreviewManager(), nullptr, 64.0f))
				{
					entry.meshPath = meshPath;
				}


				int layerIndex = static_cast<int>(entry.layerIndex);
				if (ImGui::Combo("Terrain Layer", &layerIndex, s_layerNames, IM_ARRAYSIZE(s_layerNames)))
				{
					entry.layerIndex = static_cast<uint8>(std::clamp(layerIndex, 0, 3));
				}

				ImGui::DragFloat("Density", &entry.density, 0.05f, 0.0f, 100.0f);
				ImGui::SliderFloat("Min Coverage", &entry.minCoverage, 0.0f, 1.0f);
				ImGui::DragFloatRange2("Scale", &entry.minScale, &entry.maxScale, 0.01f, 0.01f, 10.0f);
				ImGui::SliderFloat("Max Slope", &entry.maxSlopeAngle, 0.0f, 90.0f);
				ImGui::DragFloatRange2("Height Range", &entry.minHeight, &entry.maxHeight, 1.0f, -10000.0f, 10000.0f);
				ImGui::DragFloatRange2("Fade Distance", &entry.fadeStartDistance, &entry.fadeEndDistance, 1.0f, 0.0f, 10000.0f);
				ImGui::Checkbox("Random Yaw", &entry.randomYaw);
				ImGui::SameLine();
				ImGui::Checkbox("Align To Normal", &entry.alignToNormal);
				ImGui::SameLine();
				ImGui::Checkbox("Cast Shadows", &entry.castShadows);

				if (ImGui::Button("Remove"))
				{
					removeIndex = i;
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
			ImGui::Separator();
		}

		if (removeIndex >= 0)
		{
			entries.erase(entries.begin() + removeIndex);
		}

		if (ImGui::Button("Add Foliage Entry"))
		{
			entries.emplace_back();
		}
	}

	void MaterialEditorInstance::DrawSamplerTypeEditor(PropertyBase* prop)
	{
		static const char* s_samplerNames[] = { "Color", "Normal Map" };

		const auto* intValue = prop->GetValueAs<int32>();
		if (!intValue)
		{
			return;
		}

		int current = std::clamp(*intValue, 0, static_cast<int>(IM_ARRAYSIZE(s_samplerNames)) - 1);
		if (ImGui::Combo("##samplerType", &current, s_samplerNames, IM_ARRAYSIZE(s_samplerNames)))
		{
			prop->SetValue(current);
		}
	}

	void MaterialEditorInstance::DrawNodeProperties(GraphNode* node)
	{
		// The "Get Variable" node selects its source from the set of variables currently declared in the
		// graph, so render its single property as a dropdown of declared names instead of a free text field.
		if (node->GetTypeInfo().id == NamedVariableGetNode::GetStaticTypeInfo().id)
		{
			DrawVariableSelector(static_cast<NamedVariableGetNode*>(node));
			return;
		}

		const bool isTextureNode =
			node->GetTypeInfo().id == TextureNode::GetStaticTypeInfo().id ||
			node->GetTypeInfo().id == TextureParameterNode::GetStaticTypeInfo().id;

		for (auto* prop : node->GetProperties())
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
			ImGui::TreeNodeEx("Field", flags, prop->GetName().data());

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (isTextureNode && prop->GetName() == "Sampler Type")
			{
				DrawSamplerTypeEditor(prop);
			}
			else
			{
				DrawPropertyEditor(prop);
			}
		}

		// The function input node's default value spans 1-4 components depending on the selected
		// parameter type, so it is rendered with a dedicated type-aware widget here rather than as a
		// plain float property in the loop above.
		if (node->GetTypeInfo().id == MaterialFunctionInputNode::GetStaticTypeInfo().id)
		{
			DrawFunctionInputDefaultValue(static_cast<MaterialFunctionInputNode*>(node));
		}
	}

	void MaterialEditorInstance::DrawFunctionInputDefaultValue(MaterialFunctionInputNode* node)
	{
		const int32 componentCount = node->GetDefaultValueComponentCount();
		if (componentCount <= 0)
		{
			// Texture inputs have no scalar default value to edit.
			return;
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
		ImGui::TreeNodeEx("Field", flags, "Default Value");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);

		float values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int32 i = 0; i < componentCount; ++i)
		{
			values[i] = node->GetDefaultValueComponent(i);
		}

		bool changed = false;
		switch (componentCount)
		{
		case 1:  changed = ImGui::InputFloat("##defaultValue", &values[0], 0.1f, 100.0f); break;
		case 2:  changed = ImGui::InputFloat2("##defaultValue", values); break;
		case 3:  changed = ImGui::InputFloat3("##defaultValue", values); break;
		case 4:  changed = ImGui::InputFloat4("##defaultValue", values); break;
		default: break;
		}

		if (changed)
		{
			for (int32 i = 0; i < componentCount; ++i)
			{
				node->SetDefaultValueComponent(i, values[i]);
			}
		}
	}

	void MaterialEditorInstance::DrawVariableSelector(NamedVariableGetNode* node)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
		ImGui::TreeNodeEx("Field", flags, "Variable");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);

		// Collect the names of all variables currently declared via Set Variable nodes.
		std::vector<String> names;
		const uint32 setTypeId = NamedVariableSetNode::GetStaticTypeInfo().id;
		for (GraphNode* graphNode : m_graph->GetNodes())
		{
			if (graphNode->GetTypeInfo().id != setTypeId)
			{
				continue;
			}

			const String& name = static_cast<NamedVariableSetNode*>(graphNode)->GetVariableName();
			if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
			{
				names.push_back(name);
			}
		}

		const String& current = node->GetVariableName();
		const char* preview = current.empty() ? "(None)" : current.c_str();
		if (ImGui::BeginCombo("##variable_selector", preview))
		{
			for (const String& name : names)
			{
				const bool selected = (name == current);
				if (ImGui::Selectable(name.c_str(), selected))
				{
					node->SetVariableName(name);
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
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
			std::vector<std::pair<int32, std::string_view>> enumOptions;
			if (prop->GetEnumOptions(enumOptions))
			{
				const int32 currentValue = *intValue;
				const char* preview = "Unknown";
				int currentIndex = -1;
				for (int i = 0; i < static_cast<int>(enumOptions.size()); ++i)
				{
					if (enumOptions[i].first == currentValue)
					{
						preview = enumOptions[i].second.data();
						currentIndex = i;
						break;
					}
				}

				const String comboId = String("##enum_") + prop->GetName().data();
				if (ImGui::BeginCombo(comboId.c_str(), preview))
				{
					for (int i = 0; i < static_cast<int>(enumOptions.size()); ++i)
					{
						const bool selected = (i == currentIndex);
						if (ImGui::Selectable(enumOptions[i].second.data(), selected))
						{
							prop->SetValue(enumOptions[i].first);
						}
						if (selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				int32 value = *intValue;
				if (ImGui::InputInt(prop->GetName().data(), &value, 1, 100, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank))
				{
					prop->SetValue(value);
				}
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
			String currentPath(pathValue->GetPath());
			const std::set<String> exts = { String(pathValue->GetFilter()) };
			const String hiddenLabel = String("##") + prop->GetName().data();
			if (AssetPickerWidget::Draw(hiddenLabel.c_str(), currentPath, exts, &m_editor.GetPreviewManager(), nullptr, 64.0f, [this](const std::string& path) { m_host.NavigateToAsset(path); }))
			{
				prop->SetValue(AssetPathValue(currentPath, pathValue->GetFilter()));
			}
		}
	}

	void MaterialEditorInstance::DrawGraphPanel(const String& panelId)
	{
		if (m_focusGraphPanel)
		{
			ImGui::SetWindowFocus(panelId.c_str());
			m_focusGraphPanel = false;
		}

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

	const String& MaterialEditorInstance::GetSelectedShaderCode() const
	{
		static const String empty;

		if (m_selectedShaderIndex < 0)
		{
			return empty;
		}

		if (m_selectedShaderIndex < static_cast<int>(m_vertexShaderCode.size()))
		{
			return m_vertexShaderCode[m_selectedShaderIndex];
		}

		const int pixelIndex = m_selectedShaderIndex - static_cast<int>(m_vertexShaderCode.size());
		if (pixelIndex >= 0 && pixelIndex < static_cast<int>(m_pixelShaderCode.size()))
		{
			return m_pixelShaderCode[pixelIndex];
		}

		return empty;
	}

	void MaterialEditorInstance::DrawShaderCodePanel(const String& panelId)
	{
		// Labels match the ordering of VertexShaderType (0-5) followed by PixelShaderType (0-3),
		// which is how the generated code is stored in m_vertexShaderCode / m_pixelShaderCode.
		static const char* shaderNames[] = {
			"Vertex Shader: Default",
			"Vertex Shader: Skinned (Low)",
			"Vertex Shader: Skinned (Medium)",
			"Vertex Shader: Skinned (High)",
			"Vertex Shader: UI",
			"Vertex Shader: Instanced",
			"Pixel Shader: Forward",
			"Pixel Shader: GBuffer (Deferred)",
			"Pixel Shader: Shadow Map",
			"Pixel Shader: UI"
		};

		if (ImGui::Begin(panelId.c_str()))
		{
			if (ImGui::Button("Compile"))
			{
				Compile();
			}

			if (!m_shaderCodeCaptured)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("Press Compile to generate and inspect the shader HLSL.");
			}
			else
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(320.0f);
				ImGui::Combo("##shaderSelect", &m_selectedShaderIndex, shaderNames, IM_ARRAYSIZE(shaderNames));

				const String& code = GetSelectedShaderCode();

				ImGui::SameLine();
				if (ImGui::Button("Copy to Clipboard"))
				{
					ImGui::SetClipboardText(code.c_str());
				}

				// Read-only multiline text box so the generated HLSL can be viewed, selected and copied.
				ImGui::InputTextMultiline("##shaderCode",
					const_cast<char*>(code.c_str()), code.size() + 1,
					ImVec2(-FLT_MIN, -FLT_MIN),
					ImGuiInputTextFlags_ReadOnly);
			}
		}
		ImGui::End();
	}

	void MaterialEditorInstance::InitializeDockLayout(ImGuiID dockspaceId, const String& previewId, const String& detailsId, const String& graphId, const String& shaderCodeId)
	{
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
			ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockspaceId;
			auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			auto sideTopId = ImGui::DockBuilderSplitNode(sideId, ImGuiDir_Up, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &sideId);

			ImGui::DockBuilderDockWindow(shaderCodeId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(graphId.c_str(), mainId);

			ImGui::DockBuilderDockWindow(previewId.c_str(), sideTopId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
			m_focusGraphPanel = true;
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
