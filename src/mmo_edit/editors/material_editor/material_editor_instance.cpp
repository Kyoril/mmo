// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_editor_instance.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.inl"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "item_builder.h"
#include "material_graph.h"
#include "material_node.h"
#include "node_layout.h"
#include "node_pin_icons.h"
#include "node_header_renderer.h"

#include <cinttypes>

#include "assets/asset_registry.h"
#include "graphics/d3d11/shader_compiler_d3d11.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_deserializer.h"


namespace ImGui
{
	bool Splitter(const bool splitVertically, const float thickness, float* size1, float* size2, const float minSize1, const float minSize2, const float splitterLongAxisSize = -1.0f)
	{
	    using namespace ImGui;
	    ImGuiContext& g = *GImGui;
	    ImGuiWindow* window = g.CurrentWindow;
	    const ImGuiID id = window->GetID("##Splitter");
	    ImRect bb;
	    bb.Min = window->DC.CursorPos + (splitVertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
	    bb.Max = bb.Min + CalcItemSize(splitVertically ? ImVec2(thickness, splitterLongAxisSize) : ImVec2(splitterLongAxisSize, thickness), 0.0f, 0.0f);
	    if (splitVertically)
	    {
		    return SplitterBehavior(bb, id, ImGuiAxis_X, size1, size2, minSize1, minSize2, 0.0f);
	    }

        return SplitterBehavior(bb, id, ImGuiAxis_Y, size1, size2, minSize1, minSize2, 0.0f);
	}
}

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

	static ed::EditorContext* s_context = nullptr;
	
	IconType PinTypeToIconType(const PinType pinType)
	{
	    switch (pinType)
	    {
	        case PinType::Any:      return IconType::Circle;
	        case PinType::Material: return IconType::Circle;
	        case PinType::Bool:     return IconType::Circle;
	        case PinType::Int32:    return IconType::Circle;
	        case PinType::Float:    return IconType::Circle;
	        case PinType::String:   return IconType::Circle;
	    }

	    return IconType::Circle;
	}

	ImVec4 PinTypeToColor(const PinType pinType)
	{
	    switch (pinType)
	    {
	        case PinType::Any:      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	        case PinType::Material: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	        case PinType::Bool:     return ImVec4(220 / 255.0f,  48 / 255.0f,  48 / 255.0f, 1.0f);
	        case PinType::Int32:    return ImVec4( 68 / 255.0f, 201 / 255.0f, 156 / 255.0f, 1.0f);
	        case PinType::Float:    return ImVec4(147 / 255.0f, 226 / 255.0f,  74 / 255.0f, 1.0f);
	        case PinType::String:   return ImVec4(124 / 255.0f,  21 / 255.0f, 153 / 255.0f, 1.0f);
	    }

	    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	bool DrawPinValue(const PinValue& value)
	{
	    switch (value.GetType())
	    {
	        case PinType::Any:
	            return false;
	        case PinType::Material:
	            return false;
	        case PinType::Bool:
	            if (value.As<bool>())
	                ImGui::TextUnformatted("true");
	            else
	                ImGui::TextUnformatted("false");
	            return true;
	        case PinType::Int32:
	            ImGui::Text("%d", value.As<int32_t>());
	            return true;
	        case PinType::Float:
	            ImGui::Text("%g", value.As<float>());
	            return true;
	        case PinType::String:
	            ImGui::Text("%s", value.As<std::string>().c_str());
	            return true;
	    }

	    return false;
	}

	bool EditPinValue(Pin& pin)
	{
	    ScopedItemWidth scopedItemWidth{120};

	    auto pinValue = pin.GetValue();

	    switch (pinValue.GetType())
	    {
	        case PinType::Any:
	            return true;
	        case PinType::Material:
	            return true;
	        case PinType::Bool:
	            pin.SetValue(!pinValue.As<bool>());
	            return true;
	        case PinType::Int32:
	            {
	                auto value = pinValue.As<int32_t>();
	                if (ImGui::InputInt("##editor", &value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
	                {
	                    pin.SetValue(value);
	                    return true;
	                }
	            }
	            return false;
	        case PinType::Float:
	            {
	                auto value = pinValue.As<float>();
	                if (ImGui::InputFloat("##editor", &value, 1, 100, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
	                {
	                    pin.SetValue(value);
	                    return true;
	                }
	            }
	            return false;
	        case PinType::String:
	            {
	                auto value = pinValue.As<std::string>();
	                if (ImGui::InputText("##editor", (char*)value.data(), value.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
	                {
	                    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
	                    {
	                        auto& stringValue = *static_cast<std::string*>(data->UserData);
	                        ImVector<char>* my_str = (ImVector<char>*)data->UserData;
	                        IM_ASSERT(stringValue.data() == data->Buf);
	                        stringValue.resize(data->BufSize); // NB: On resizing calls, generally data->BufSize == data->BufTextLen + 1
	                        data->Buf = (char*)stringValue.data();
	                    }
	                    return 0;
	                }, &value))
	                {
	                    value.resize(strlen(value.c_str()));
	                    pin.SetValue(value);
	                    return true;
	                }
	            }
	            return false;
	    }

	    return true;
	}

	void DrawPinValueWithEditor(Pin& pin)
	{
	    auto storage = ImGui::GetStateStorage();
	    auto activePinId = storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), false);

	    if (activePinId == pin.GetId())
	    {
	        if (EditPinValue(pin))
	        {
	            ax::NodeEditor::EnableShortcuts(true);
	            activePinId = 0;
	        }
	    }
	    else
	    {
	        // Draw pin value
	        //PinValueBackgroundRenderer bg;
	        if (!DrawPinValue(pin.GetValue()))
	        {
	            //bg.Discard();
	            return;
	        }

	        // Draw invisible button over pin value which triggers an editor if clicked
	        auto itemMin = ImGui::GetItemRectMin();
	        auto itemMax = ImGui::GetItemRectMax();
	        auto itemSize = itemMax - itemMin;
	        itemSize.x = ImMax(itemSize.x, 1.0f);
	        itemSize.y = ImMax(itemSize.y, 1.0f);

	        ImGui::SetCursorScreenPos(itemMin);

	        if (ImGui::InvisibleButton("###pin_value_editor", itemSize))
	        {
	            activePinId = pin.GetId();
	            ax::NodeEditor::EnableShortcuts(false);
	        }
	    }

	    storage->SetInt(ImGui::GetID("PinValueEditor_ActivePinId"), activePinId);
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

            Grid layout;
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

                // [3] - Show value/editor when pin is not linked to anything
                if (!material.HasPinAnyLink(*pin))
                {
                    ImGui::SameLine();
                    DrawPinValueWithEditor(*pin);
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

	static std::unique_ptr<MaterialGraph> s_material;

	void CreateNodeDialog::Open(Pin* fromPin)
	{
		auto storage = ImGui::GetStateStorage();
	    storage->SetVoidPtr(ImGui::GetID("##create_node_pin"), fromPin);
	    ImGui::OpenPopup("##create_node");

	    m_SortedNodes.clear();
	}

	void CreateNodeDialog::Show(MaterialGraph& material)
	{
		if (!ImGui::IsPopupOpen("##create_node"))
			return;

	    auto storage = ImGui::GetStateStorage();
	    auto fromPin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##create_node_pin")));

	    if (!ImGui::BeginPopup("##create_node"))
	        return;

	    auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();

	    if (m_SortedNodes.empty())
	    {
	        auto nodeRegistry = material.GetNodeRegistry();

	        auto types = nodeRegistry->GetTypes();

	        m_SortedNodes.assign(types.begin(), types.end());
	        std::sort(m_SortedNodes.begin(), m_SortedNodes.end(), [](const auto& lhs, const auto& rhs)
	        {
	            return std::lexicographical_compare(
	                lhs->displayName.begin(), lhs->displayName.end(),
	                rhs->displayName.begin(), rhs->displayName.end());
	        });
	    }

	    for (auto nodeTypeInfo : m_SortedNodes)
	    {
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
	        }
	    }

	    ImGui::EndPopup();
	}

	std::vector<Pin*> CreateNodeDialog::CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin)
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

	MaterialEditorInstance::MaterialEditorInstance(EditorHost& host, const Path& assetPath)
		: EditorInstance(host, assetPath)
	{
		m_material = std::make_shared<Material>("");

		const auto file = AssetRegistry::OpenFile(assetPath.string());
		if (!file)
		{
			ELOG("Unable to open material asset " << assetPath);
			return;
		}

		io::StreamSource source { *file };
		io::Reader reader { source };

		MaterialDeserializer deserializer { *m_material };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to read material file " << assetPath);
			return;
		}
	}
		
	static void ShowPlaceholderObject(const char* prefix, int uid)
	{
	    // Use object uid as identifier. Most commonly you could also use the object pointer as a base ID.
	    ImGui::PushID(uid);

	    // Text and Tree nodes are less high than framed widgets, using AlignTextToFramePadding() we add vertical spacing to make the tree lines equal high.
	    ImGui::TableNextRow();
	    ImGui::TableSetColumnIndex(0);
	    ImGui::AlignTextToFramePadding();
	    bool node_open = ImGui::TreeNode("Object", "%s_%u", prefix, uid);
	    ImGui::TableSetColumnIndex(1);
	    ImGui::Text("my sailor is rich");

	    if (node_open)
	    {
			ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
            ImGui::TreeNodeEx("Field", flags, "Texture");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("test", "(None)"))
			{
				const auto files = AssetRegistry::ListFiles();
				for (auto& file : files)
				{
					if (!file.ends_with(".htex"))
						continue;
					
					ImGui::PushID(file.c_str());
					if (ImGui::Selectable(file.c_str()))
					{
						DLOG("Selected texture " << file);
					}
					ImGui::PopID();
				}
				
				ImGui::EndCombo();
			}
            ImGui::NextColumn();

	        /*static float placeholder_members[8] = { 0.0f, 0.0f, 1.0f, 3.1416f, 100.0f, 999.0f };
	        for (int i = 0; i < 8; i++)
	        {
	            ImGui::PushID(i); // Use field index as identifier.
	            if (i < 2)
	            {
	                ShowPlaceholderObject("Child", 424242);
	            }
	            else
	            {
	                // Here we use a TreeNode to highlight on hover (we could use e.g. Selectable as well)
	                ImGui::TableNextRow();
	                ImGui::TableSetColumnIndex(0);
	                ImGui::AlignTextToFramePadding();
	                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
	                ImGui::TreeNodeEx("Field", flags, "Field_%d", i);

	                ImGui::TableSetColumnIndex(1);
	                ImGui::SetNextItemWidth(-FLT_MIN);
	                if (i >= 5)
	                    ImGui::InputFloat("##value", &placeholder_members[i], 1.0f);
	                else
	                    ImGui::DragFloat("##value", &placeholder_members[i], 0.01f);
	                ImGui::NextColumn();
	            }
	            ImGui::PopID();
	        }*/
	        ImGui::TreePop();
	    }
	    ImGui::PopID();
	}

	void MaterialEditorInstance::Draw()
	{
        if (!s_context)
        {
		    ed::Config editorConfig;
		    editorConfig.SettingsFile = "Simple.json";
		    s_context = ed::CreateEditor(&editorConfig);
            
			ed::SetCurrentEditor(s_context);

			s_material = std::make_unique<MaterialGraph>();
		}
		
		// Add the viewport
	    ed::SetCurrentEditor(s_context);

		ImGui::Columns(2, "HorizontalSplitter");
        {
			ImVec2 size = ImGui::GetWindowContentRegionMax();
			m_previewSize = size.y / 2.0f;
			m_detailsSize = m_previewSize;

	        ImGui::Splitter(false, 2.0f, &m_previewSize, &m_detailsSize, 100.0f, 100.0f);

	        if (ImGui::BeginChild("preview", ImVec2(0, m_previewSize)))
	        {
				ImGui::Text("Preview");

				if (ImGui::Button("Compile"))
				{
					MaterialCompiler compiler;
					s_material->Compile(compiler);

					std::unique_ptr<ShaderCompiler> shaderCompiler = std::make_unique<ShaderCompilerD3D11>();
					compiler.GenerateShaderCode(*m_material, *shaderCompiler);
				}
	        }

			ImGui::EndChild();
	        
	        if (ImGui::BeginChild("details", ImVec2(0, m_detailsSize)))
	        {
				ImGui::Text("Details");
							
			    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			    if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
			    {
					ed::NodeId selectedNode;
					if (ed::GetSelectedNodes(&selectedNode, 1) > 0)
					{
						auto* node = s_material->FindNode(selectedNode.Get());
						if (node)
						{
							for (auto* prop : node->GetProperties())
							{
								ImGui::TableNextRow();
					            ImGui::TableSetColumnIndex(0);
					            ImGui::AlignTextToFramePadding();
					            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
					            ImGui::TreeNodeEx("Field", flags, prop->GetName().data());

					            ImGui::TableSetColumnIndex(1);
					            ImGui::SetNextItemWidth(-FLT_MIN);

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
								else if (const auto* pathValue = prop->GetValueAs<AssetPathValue>())
								{
									if (ImGui::BeginCombo(prop->GetName().data(), !pathValue->GetPath().empty() ? pathValue->GetPath().data() : "(None)"))
									{
										const auto files = AssetRegistry::ListFiles();
										for (auto& file : files)
										{
											if (!pathValue->GetFilter().empty())
											{
												if (!file.ends_with(pathValue->GetFilter()))
													continue;
											}
											
											ImGui::PushID(file.c_str());
											if (ImGui::Selectable(file.c_str()))
											{
												prop->SetValue(AssetPathValue(file, pathValue->GetFilter()));
											}
											ImGui::PopID();
										}
										
										ImGui::EndCombo();
									}
								}
							}
						}
					}
					
			        ImGui::EndTable();
			    }
			    ImGui::PopStyleVar();
	        }

			ImGui::EndChild();
        }
		ImGui::NextColumn();
        

	    ed::Begin("My Editor", ImVec2(0.0, 0.0f));
		
		CommitMaterialNodes(*s_material);

		HandleCreateAction(*s_material);

		
        ed::Suspend();
        m_createDialog.Show(*s_material);
        ed::Resume();
		
	    ed::End();

	    ed::SetCurrentEditor(nullptr);
	}
}
