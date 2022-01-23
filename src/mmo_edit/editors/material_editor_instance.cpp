// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_editor_instance.h"


#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "imgui-node-editor/examples/blueprints-example/utilities/widgets.h"

static ImTextureID s_headerBackground = nullptr;

namespace ed = ax::NodeEditor;

enum class PinType
{
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

enum class PinKind
{
    Output,
    Input
};

enum class NodeType
{
    Blueprint,
    Simple,
    Tree,
    Comment,
    Houdini
};

struct Node;

struct Pin
{
    ed::PinId   ID;
    ::Node*     Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};

struct Node
{
    ed::NodeId ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    std::string State;
    std::string SavedState;

    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0)
    {
    }
};

static const int s_PinIconSize = 24;

ImColor GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow:     return ImColor(255, 255, 255);
        case PinType::Bool:     return ImColor(220,  48,  48);
        case PinType::Int:      return ImColor( 68, 201, 156);
        case PinType::Float:    return ImColor(147, 226,  74);
        case PinType::String:   return ImColor(124,  21, 153);
        case PinType::Object:   return ImColor( 51, 150, 215);
        case PinType::Function: return ImColor(218,   0, 183);
        case PinType::Delegate: return ImColor(255,  48,  48);
    }
};

void DrawPinIcon(const Pin& pin, bool connected, int alpha)
{
	ax::Drawing::IconType iconType;
    ImColor  color = GetIconColor(pin.Type);
    color.Value.w = alpha / 255.0f;
    switch (pin.Type)
    {
        case PinType::Flow:     iconType = ax::Drawing::IconType::Flow;   break;
        case PinType::Bool:     iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Int:      iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Float:    iconType = ax::Drawing::IconType::Circle; break;
        case PinType::String:   iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Object:   iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Function: iconType = ax::Drawing::IconType::Circle; break;
        case PinType::Delegate: iconType = ax::Drawing::IconType::Square; break;
        default:
            return;
    }

    ax::Widgets::Icon(ImVec2(s_PinIconSize, s_PinIconSize), iconType, connected, color, ImColor(32, 32, 32, alpha));
};

struct Link
{
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};

namespace ImGui
{
	bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
	{
	    using namespace ImGui;
	    ImGuiContext& g = *GImGui;
	    ImGuiWindow* window = g.CurrentWindow;
	    ImGuiID id = window->GetID("##Splitter");
	    ImRect bb;
	    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
	    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
	    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
	}
}
namespace mmo
{
	static ed::EditorContext* s_context = nullptr;


	void MaterialEditorInstance::Draw()
	{
        if (!s_context)
        {
		    ed::Config editorConfig;
		    editorConfig.SettingsFile = "Simple.json";
		    s_context = ed::CreateEditor(&editorConfig);
		}

		ImGui::Columns(2, "HorizontalSplitter");
		
        ImGui::Splitter(false, 2.0f, &m_previewSize, &m_detailsSize, 100.0f, 100.0f);

        if (ImGui::BeginChild("preview", ImVec2(0, m_previewSize)))
        {
			ImGui::Text("Preview");
        
        }
		ImGui::EndChild();
        
        if (ImGui::BeginChild("details", ImVec2(0, m_detailsSize)))
        {
			ImGui::Text("Details");
        }
		ImGui::EndChild();
        
		ImGui::NextColumn();

        Pin pin(0, "time", PinType::Float);
		pin.Kind = PinKind::Output;
        
        Pin albedoPin(0, "Albedo", PinType::Object);
		albedoPin.Kind = PinKind::Input;
        Pin roughnessPin(0, "Roughness", PinType::Object);
		roughnessPin.Kind = PinKind::Input;
        Pin metallicPin(0, "Metallic", PinType::Object);
		metallicPin.Kind = PinKind::Input;
        Pin specularPin(0, "Specular", PinType::Object);
		specularPin.Kind = PinKind::Input;

		// Add the viewport
	    ed::SetCurrentEditor(s_context);
	    ed::Begin("My Editor", ImVec2(0.0, 0.0f));
	    int uniqueId = 1;
	    // Start drawing nodes.
	    ed::BeginNode(uniqueId++);
	        ImGui::Text("Material");
	        ed::BeginPin(uniqueId++, ed::PinKind::Input);
	            DrawPinIcon(albedoPin, false, 255);
                ImGui::SameLine();
				ImGui::Text(albedoPin.Name.c_str());
	        ed::EndPin();
			ed::BeginPin(uniqueId++, ed::PinKind::Input);
	            DrawPinIcon(roughnessPin, false, 255);
				ImGui::SameLine();
				ImGui::Text(roughnessPin.Name.c_str());
	        ed::EndPin();
			ed::BeginPin(uniqueId++, ed::PinKind::Input);
	            DrawPinIcon(metallicPin, false, 255);
				ImGui::SameLine();
				ImGui::Text(metallicPin.Name.c_str());
	        ed::EndPin();
			ed::BeginPin(uniqueId++, ed::PinKind::Input);
	            DrawPinIcon(specularPin, false, 255);
				ImGui::SameLine();
				ImGui::Text(specularPin.Name.c_str());
	        ed::EndPin();
	        //ImGui::SameLine();
	        //ed::BeginPin(uniqueId++, ed::PinKind::Output);
			//ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			//ed::PinPivotSize(ImVec2(0, 0));
			//DrawPinIcon(pin, false, 255);
	        //ed::EndPin();
	    ed::EndNode();
	    ed::End();
	    ed::SetCurrentEditor(nullptr);
	}
}
