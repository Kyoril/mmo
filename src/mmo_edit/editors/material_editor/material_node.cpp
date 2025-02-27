// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_node.h"

#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.h"
#include "material_graph.h"
#include "reader.h"
#include "writer.h"
#include "log/default_log_levels.h"

static ImTextureID s_headerBackground = nullptr;

namespace mmo
{
	const uint32 ConstFloatNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 ScalarParameterNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 IfNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 ConstVectorNode::Color = ImColor(0.88f, 0.88f, 0.29f, 0.25f);
	const uint32 VectorParameterNode::Color = ImColor(0.88f, 0.88f, 0.29f, 0.25f);
	const uint32 MaterialNode::Color = ImColor(114.0f / 255.0f, 92.0f / 255.0f, 71.0f / 255.0f, 0.50f);
	const uint32 TextureNode::Color = ImColor(0.29f, 0.29f, 0.88f, 0.25f);
	const uint32 TextureParameterNode::Color = ImColor(0.29f, 0.29f, 0.88f, 0.25f);
	const uint32 TextureCoordNode::Color = ImColor(0.88f, 0.0f, 0.0f, 0.25f);
	const uint32 MaterialFunctionOutputNode::Color = ImColor(0.29f, 0.29f, 0.88f, 0.25f);
	const uint32 MaterialFunctionNode::Color = ImColor(0.29f, 0.29f, 0.88f, 0.25f);
	const uint32 SineNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 CosineNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 TangentNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 ArcTangent2Node::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 FracNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);

	Pin::Pin(GraphNode* node, const PinType type, const std::string_view name)
		: m_id(node ? node->GetMaterial()->MakePinId(this) : 0)
        , m_node(node)
        , m_type(type)
        , m_name(name)
	{
	}

	Pin::~Pin()
	{
		if (m_node)
		{
			m_node->GetMaterial()->ForgetPin(this);
		}
	}

	io::Writer& Pin::Serialize(io::Writer& writer)
	{
		writer
			<< io::write<uint32>(m_id)
			<< io::write<uint32>(m_link ? m_link->GetId() : 0);

		return writer;
	}

	io::Reader& Pin::Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context)
	{
		uint32 id, link;
		if (!(reader
			>> io::read<uint32>(id)
			>> io::read<uint32>(link)))
		{
			ELOG("Unable to deserialize pin");
			return reader;
		}

		m_id = id;
		if (link != 0)
		{
			context.AddPostLoadAction([this, link]()
			{
				const auto pin = m_node->GetMaterial()->FindPin(link);
				if (!pin)
				{
					WLOG("Unable to find target pin for pin " << m_id);
				}
				else
				{
					if (!LinkTo(*pin))
					{
						WLOG("Unable to link pin " << m_id << " to target pin " << link);
					}
				}

				return true;	
			});
		}

		return reader;
	}

	LinkQueryResult Pin::CanLinkTo(const Pin& pin) const
	{
        auto result = m_node->AcceptLink(*this, pin);
	    if (!result)
	    {
	    	return result;
        }

	    auto result2 = pin.m_node->AcceptLink(*this, pin);
	    if (!result2)
	    {
	    	return result2;
        }

	    if (result.GetReason().empty())
        {
	    	return result2;
        }

	    return result;
	}

	bool Pin::LinkTo(const Pin& pin)
	{
        if (!CanLinkTo(pin))
        {
        	return false;
        }

	    if (m_link)
	    {
		    Unlink();
	    }

	    m_link = &pin;
		pin.m_link = this;

	    m_node->WasLinked(*this, pin);
	    pin.m_node->WasLinked(*this, pin);

	    return true;
    }

	void Pin::Unlink()
	{
        if (!m_link)
        {
	        return;   
        }

	    const auto link = m_link;
	    m_link = nullptr;
		link->m_link = nullptr;

	    m_node->WasUnlinked(*this, *link);
	    link->m_node->WasUnlinked(*this, *link);
	}

	bool Pin::IsInput() const
	{
        for (const auto pin : m_node->GetInputPins())
        {
	        if (pin->m_id == m_id)
	            return true;
        }

	    return false;
	}

	bool Pin::IsOutput() const
	{
        for (const auto pin : m_node->GetOutputPins())
        {
	        if (pin->m_id == m_id)
	            return true;
        }

	    return false;
	}
	
	io::Writer& BoolProperty::Serialize(io::Writer& writer)
	{
		return writer << io::write<uint8>(*GetValueAs<bool>());
	}

	io::Reader& BoolProperty::Deserialize(io::Reader& reader)
	{
		bool value;
		if (!(reader >> io::read<uint8>(value)))
		{
			ELOG("Unable to read value of bool property " << m_name);
			return reader;
		}

		SetValue(value);
		return reader;
	}

	io::Writer& FloatProperty::Serialize(io::Writer& writer)
	{
		return writer << io::write<float>(*GetValueAs<float>());
	}

	io::Reader& FloatProperty::Deserialize(io::Reader& reader)
	{
		float value;
		if (!(reader >> io::read<float>(value)))
		{
			ELOG("Unable to read value of float property " << m_name);
			return reader;
		}

		SetValue(value);
		return reader;
	}

	io::Writer& ColorProperty::Serialize(io::Writer& writer)
	{
		const Color* value = GetValueAs<Color>();

		return writer
			<< io::write<float>(value->GetRed())
			<< io::write<float>(value->GetGreen())
			<< io::write<float>(value->GetBlue())
			<< io::write<float>(value->GetAlpha());
	}

	io::Reader& ColorProperty::Deserialize(io::Reader& reader)
	{
		float r, g, b, a;
		if (!(reader 
			>> io::read<float>(r)
			>> io::read<float>(g)
			>> io::read<float>(b)
			>> io::read<float>(a)))
		{
			ELOG("Unable to read value of color property " << m_name);
			return reader;
		}

		SetValue(Color(r, g, b, a));
		return reader;
	}

	io::Writer& IntProperty::Serialize(io::Writer& writer)
	{
		return writer << io::write<int32>(*GetValueAs<int32>());
	}

	io::Reader& IntProperty::Deserialize(io::Reader& reader)
	{
		int32 value;
		if (!(reader >> io::read<int32>(value)))
		{
			ELOG("Unable to read value of int property " << m_name);
			return reader;
		}

		SetValue(value);
		return reader;
	}

	io::Writer& StringProperty::Serialize(io::Writer& writer)
	{
		return writer << io::write_dynamic_range<uint32>(*GetValueAs<String>());
	}

	io::Reader& StringProperty::Deserialize(io::Reader& reader)
	{
		String value;
		if (!(reader >> io::read_container<uint32>(value)))
		{
			ELOG("Unable to read value of string property " << m_name);
			return reader;
		}

		SetValue(value);
		return reader;
	}

	io::Writer& AssetPathProperty::Serialize(io::Writer& writer)
	{
		return writer << io::write_dynamic_range<uint16>(
			String(GetValueAs<AssetPathValue>()->GetPath()));
	}

	io::Reader& AssetPathProperty::Deserialize(io::Reader& reader)
	{
		String path;
		if (!(reader >> io::read_container<uint16>(path)))
		{
			ELOG("Unable to read value of string property " << m_name);
			return reader;
		}

		const auto* value = GetValueAs<AssetPathValue>();
		SetValue(AssetPathValue(path, value->GetFilter()));

		return reader;
	}

	GraphNode::GraphNode(MaterialGraph& graph)
		: m_id(graph.MakeNodeId(this))
		, m_material(&graph)
	{
	}

	io::Writer& GraphNode::Serialize(io::Writer& writer)
	{
		writer
			<< io::write<uint32>(m_id);

		float posX = 0.0f, posY = 0.0f, sizeX = 0.0f, sizeY = 0.0f;
		const auto editorContext = reinterpret_cast<ed::Detail::EditorContext*>(ed::GetCurrentEditor());
		if (editorContext)
		{
			ed::Detail::EditorState& state = editorContext->GetState();
			const auto nodeStateIt = state.m_NodesState.m_Nodes.find(m_id);
			if (nodeStateIt != state.m_NodesState.m_Nodes.end())
			{
				posX = nodeStateIt->second.m_Location.x;
				posY = nodeStateIt->second.m_Location.y;
				sizeX = nodeStateIt->second.m_Size.x;
				sizeY = nodeStateIt->second.m_Size.y;
			}
			else
			{
				WLOG("Node state not found, empty state will be saved");
			}
		}
		else
		{
			WLOG("No editor context given, node state won't be saved");
		}

		writer
			<< io::write<float>(posX)
			<< io::write<float>(posY)
			<< io::write<float>(sizeX)
			<< io::write<float>(sizeY);

		writer
			<< io::write<uint8>(GetInputPins().size());
		for(const auto& pin : GetInputPins())
		{
			pin->Serialize(writer);
		}

		writer
			<< io::write<uint8>(GetOutputPins().size());
		for(const auto& pin : GetOutputPins())
		{
			pin->Serialize(writer);
		}
		
		writer
			<< io::write<uint8>(GetProperties().size());
		for(const auto& prop : GetProperties())
		{
			prop->Serialize(writer);
		}
		
		return writer;
	}

	io::Reader& GraphNode::Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context)
	{
		uint8 numInputPins, numOutputPins, numProperties;
		float positionX, positionY, sizeX, sizeY;
		if (!(reader 
			>> io::read<uint32>(m_id)
			>> io::read<float>(positionX)
			>> io::read<float>(positionY)
			>> io::read<float>(sizeX)
			>> io::read<float>(sizeY)
			>> io::read<uint8>(numInputPins)))
		{
			ELOG("Unable to deserialize " << GetTypeInfo().displayName << " node");
			return reader;
		}

		const auto editorContext = reinterpret_cast<ed::Detail::EditorContext*>(ed::GetCurrentEditor());
		if (editorContext)
		{
			ed::Detail::EditorState& state = editorContext->GetState();
			const auto nodeStateIt = state.m_NodesState.m_Nodes.find(m_id);
			if (nodeStateIt == state.m_NodesState.m_Nodes.end())
			{
				state.m_NodesState.m_Nodes.emplace(m_id, ed::Detail::NodeState{ImVec2{positionX, positionY}, ImVec2{sizeX, sizeY}, ImVec2{0.0f, 0.0f}});
			}
			else
			{
				nodeStateIt->second.m_Location = ImVec2{positionX, positionY};
				nodeStateIt->second.m_Size = ImVec2{sizeX, sizeY};
			}
		}
		else
		{
			DLOG("No editor context given, node state won't be restored");
		}

		for (uint32 i = 0; i < GetInputPins().size() && i < numInputPins; ++i)
		{
			if (!(GetInputPins()[i]->Deserialize(reader, context)))
			{
				return reader;
			}
		}

		if (!(reader >> io::read<uint8>(numOutputPins)))
		{
			return reader;
		}
		
		for (uint32 i = 0; i < GetOutputPins().size() && i < numOutputPins; ++i)
		{
			if (!(GetOutputPins()[i]->Deserialize(reader, context)))
			{
				return reader;
			}
		}
		
		if (!(reader >> io::read<uint8>(numProperties)))
		{
			return reader;
		}
		
		for (uint32 i = 0; i < GetProperties().size() && i < numProperties; ++i)
		{
			if (!(GetProperties()[i]->Deserialize(reader)))
			{
				return reader;
			}
		}
		
		return reader;
	}

	std::unique_ptr<Pin> GraphNode::CreatePin(const PinType pinType, std::string_view name)
	{
        switch (pinType)
	    {
	        case PinType::Material: return make_unique<MaterialPin>(this, name);
	    }

	    return nullptr;
	}

	std::string_view GraphNode::GetName() const
	{
		return GetTypeInfo().displayName;
	}

	LinkQueryResult GraphNode::AcceptLink(const Pin& receiver, const Pin& provider) const
	{
        if (receiver.GetNode() == provider.GetNode())
	        return { false, "Pins of same node cannot be connected"};
        
	    if (receiver.IsInput() && provider.IsInput())
	        return { false, "Input pins cannot be linked together"};

	    if (receiver.IsOutput() && provider.IsOutput())
	        return { false, "Output pins cannot be linked together"};
        
	    if (provider.GetValueType() != receiver.GetValueType())
	        return { false, "Incompatible types"};

	    return {true};
	}

	std::optional<uint32> GraphNode::GetPinIndex(const Pin& pin)
	{
		uint32 index = 0;

		for (const auto& inputPin : GetInputPins())
		{
			if (inputPin == &pin)
			{
				return index;
			}
		}

		index = 0;

		for (const auto& outputPin : GetOutputPins())
		{
			if (outputPin == &pin)
			{
				return index;
			}
		}

		return {};
	}

	std::span<Pin*> MaterialNode::GetInputPins()
	{
		return m_surfaceInputPins;
	}

	ExpressionIndex MaterialNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		compiler.SetLit(m_lit);
		compiler.SetTranslucent(m_translucent);
		compiler.SetDepthWriteEnabled(m_depthWrite);
		compiler.SetDepthTestEnabled(m_depthTest);
		compiler.SetTwoSided(m_isTwoSided);
		compiler.SetIsUserInterface(m_userInterface);
		
		if (m_baseColor.IsLinked())
		{
			const ExpressionIndex baseColorExpression = m_baseColor.GetLink()->GetNode()->Compile(compiler, m_baseColor.GetLink());
			compiler.SetBaseColorExpression(baseColorExpression);
		}
		
		if (m_normal.IsLinked())
		{
			const ExpressionIndex normalExpression = m_normal.GetLink()->GetNode()->Compile(compiler, m_normal.GetLink());
			compiler.SetNormalExpression(normalExpression);
		}

		if (m_specular.IsLinked())
		{
			const ExpressionIndex specularExpression = m_specular.GetLink()->GetNode()->Compile(compiler, m_specular.GetLink());
			compiler.SetSpecularExpression(specularExpression);
		}

		if (m_roughness.IsLinked())
		{
			const ExpressionIndex roughnessExpression = m_roughness.GetLink()->GetNode()->Compile(compiler, m_roughness.GetLink());
			compiler.SetRoughnessExpression(roughnessExpression);
		}

		if (m_metallic.IsLinked())
		{
			const ExpressionIndex metallicExpression = m_metallic.GetLink()->GetNode()->Compile(compiler, m_metallic.GetLink());
			compiler.SetMetallicExpression(metallicExpression);
		}

		if (m_opacity.IsLinked())
		{
			const ExpressionIndex opacityExpression = m_opacity.GetLink()->GetNode()->Compile(compiler, m_opacity.GetLink());
			compiler.SetOpacityExpression(opacityExpression);
		}

		return IndexNone;
	}

	std::span<PropertyBase*> MaterialNode::GetProperties()
	{
		if (m_userInterface)
		{
			return m_userInterfaceProperties;
		}

		return m_surfaceProperties;
	}

	ExpressionIndex ConstFloatNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddExpression(std::to_string(m_value), ExpressionType::Float_1);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex SineNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			const ExpressionIndex inputExpression = m_inputPin.GetLink()->GetNode()->Compile(compiler, m_inputPin.GetLink());
			if (inputExpression == IndexNone)
			{
				ELOG("Missing input for sine node!");
				return IndexNone;
			}

			m_compiledExpressionId = compiler.AddExpression("sin(expr_" + std::to_string(inputExpression) + ")", compiler.GetExpressionType(inputExpression));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex CosineNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			const ExpressionIndex inputExpression = m_inputPin.GetLink()->GetNode()->Compile(compiler, m_inputPin.GetLink());
			if (inputExpression == IndexNone)
			{
				ELOG("Missing input for cosine node!");
				return IndexNone;
			}

			m_compiledExpressionId = compiler.AddExpression("cos(expr_" + std::to_string(inputExpression) + ")", compiler.GetExpressionType(inputExpression));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex TangentNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			const ExpressionIndex inputExpression = m_inputPin.GetLink()->GetNode()->Compile(compiler, m_inputPin.GetLink());
			if (inputExpression == IndexNone)
			{
				ELOG("Missing input for tangent node!");
				return IndexNone;
			}

			m_compiledExpressionId = compiler.AddExpression("tan(expr_" + std::to_string(inputExpression) + ")", compiler.GetExpressionType(inputExpression));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex ArcTangent2Node::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			const ExpressionIndex xExpression = m_xPin.GetLink()->GetNode()->Compile(compiler, m_xPin.GetLink());
			if (xExpression == IndexNone)
			{
				ELOG("Missing input x for arctan2 node!");
				return IndexNone;
			}

			const ExpressionIndex yExpression = m_yPin.GetLink()->GetNode()->Compile(compiler, m_yPin.GetLink());
			if (yExpression == IndexNone)
			{
				ELOG("Missing input y for arctan2 node!");
				return IndexNone;
			}

			if (compiler.GetExpressionType(xExpression) != compiler.GetExpressionType(yExpression))
			{
				ELOG("Input x and y for arctan2 node must be the same!");
				return IndexNone;
			}

			m_compiledExpressionId = compiler.AddExpression("atan2(expr_" + std::to_string(yExpression) + ", expr_" + std::to_string(xExpression) + ")", compiler.GetExpressionType(xExpression));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex FracNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			const ExpressionIndex inputExpression = m_inputPin.GetLink()->GetNode()->Compile(compiler, m_inputPin.GetLink());
			if (inputExpression == IndexNone)
			{
				ELOG("Missing input for frac node!");
				return IndexNone;
			}

			m_compiledExpressionId = compiler.AddExpression("frac(expr_" + std::to_string(inputExpression) + ")", compiler.GetExpressionType(inputExpression));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex ScalarParameterNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddScalarParameterExpression(m_name, m_value);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex IfNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			std::ostringstream strm;

			if (!m_aPin.IsLinked())
			{
				ELOG("'if' node requires expression for input 'A'!");
				return IndexNone;
			}

			if (!m_bPin.IsLinked())
			{
				ELOG("'if' node requires expression for input 'B'!");
				return IndexNone;
			}

			const ExpressionIndex aExpression = m_aPin.GetLink()->GetNode()->Compile(compiler, m_aPin.GetLink());
			const ExpressionIndex bExpression = m_bPin.GetLink()->GetNode()->Compile(compiler, m_bPin.GetLink());

			const auto aType = compiler.GetExpressionType(aExpression);
			const auto bType = compiler.GetExpressionType(bExpression);
			const uint32 aSize = GetExpressionTypeComponentCount(aType);
			const uint32 bSize = GetExpressionTypeComponentCount(bType);

			if (aSize != bSize)
			{
				WLOG("input size of A and B does not equal!");
			}

			if (m_equalsPin.IsLinked())
			{
				const ExpressionIndex greaterExpression = m_greaterPin.GetLink()->GetNode()->Compile(compiler, m_greaterPin.GetLink());
				const ExpressionIndex lessExpression = m_lessPin.GetLink()->GetNode()->Compile(compiler, m_lessPin.GetLink());
				const ExpressionIndex equalExpression = m_equalsPin.GetLink()->GetNode()->Compile(compiler, m_equalsPin.GetLink());

				strm << "select((abs(expr_" << aExpression << " - expr_" << bExpression << ") > " << m_threshold << "), select((expr_" << aExpression << " >= expr_" << bExpression << "), expr_" << greaterExpression << ", expr_" << lessExpression << "), expr_" << equalExpression << ")";
			}
			else
			{
				const ExpressionIndex greaterExpression = m_greaterPin.GetLink()->GetNode()->Compile(compiler, m_greaterPin.GetLink());
				const ExpressionIndex lessExpression = m_lessPin.GetLink()->GetNode()->Compile(compiler, m_lessPin.GetLink());
				strm << "select((expr_" << aExpression << " >= expr_" << bExpression << "), expr_" << greaterExpression << ", expr_" << lessExpression << ")";
			}

			m_compiledExpressionId = compiler.AddExpression(strm.str(), aSize < bSize ? aType : bType);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex ConstVectorNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			std::ostringstream strm;
			strm << "float4(";
			strm << m_value.GetRed() << ", " << m_value.GetGreen() << ", " << m_value.GetBlue() << ", " << m_value.GetAlpha();
			strm << ")";
			strm.flush();

			m_compiledExpressionId = compiler.AddExpression(strm.str(), ExpressionType::Float_4);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex VectorParameterNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddVectorParameterExpression(m_name, Vector4(m_value.GetRed(), m_value.GetGreen(), m_value.GetBlue(), m_value.GetAlpha()));
		}

		if (outputPin && outputPin != &m_argb)
		{
			if (outputPin == &m_a)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, false, true);
			}
			if (outputPin == &m_r)
			{
				return compiler.AddMask(m_compiledExpressionId, true, false, false, false);
			}
			if (outputPin == &m_g)
			{
				return compiler.AddMask(m_compiledExpressionId, false, true, false, false);
			}
			if (outputPin == &m_b)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, true, false);
			}
			if (outputPin == &m_rgb)
			{
				return compiler.AddMask(m_compiledExpressionId, true, true, true, false);
			}
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex AddNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddAddition(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex MultiplyNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddMultiply(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex MaskNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression!");
				return IndexNone;
			}

			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());
			
			m_compiledExpressionId = compiler.AddMask(inputExpression, m_channels[0], m_channels[1], m_channels[2], m_channels[3]);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex DotNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (!m_input1.IsLinked())
			{
				ELOG("Missing A expression!");
				return IndexNone;
			}

			ExpressionIndex secondExpression = IndexNone;
			if (!m_input2.IsLinked())
			{
				ELOG("Missing B expression!");
				return IndexNone;
			}
			
			firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			
			m_compiledExpressionId = compiler.AddDot(firstExpression, secondExpression);
		}
		
		return m_compiledExpressionId;
	}

	ExpressionIndex OneMinusNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression!");
				return IndexNone;
			}
			
			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());
			
			m_compiledExpressionId = compiler.AddOneMinus(inputExpression);
		}
		
		return m_compiledExpressionId;
	}

	ExpressionIndex ClampNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex valueExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing base value expression for clamp");
				return IndexNone;
			}
			
			valueExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());

			ExpressionIndex minExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				minExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				minExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex maxExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				maxExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				maxExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddClamp(valueExpression, minExpression, maxExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex PowerNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex baseExpression = IndexNone;
			if (!m_input1.IsLinked())
			{
				ELOG("Missing base expression");
				return IndexNone;
			}

			baseExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			
			ExpressionIndex exponentExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				exponentExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				exponentExpression = compiler.AddExpression(std::to_string(m_exponent), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddPower(baseExpression, exponentExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex LerpNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			ExpressionIndex alphaExpression = IndexNone;
			if (m_input3.IsLinked())
			{
				alphaExpression = m_input3.GetLink()->GetNode()->Compile(compiler, m_input3.GetLink());
			}
			else
			{
				alphaExpression = compiler.AddExpression(std::to_string(m_values[2]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddLerp(firstExpression, secondExpression, alphaExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex TextureCoordNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		compiler.NotifyTextureCoordinateIndex(m_uvCoordIndex);

		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddTextureCoordinate(m_uvCoordIndex);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex WorldPositionNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddWorldPosition();
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex CameraVectorNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddCameraVector();
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex VertexNormalNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddVertexNormal();
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex VertexColorNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddVertexColor();
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex AbsNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression");
				return IndexNone;
			}

			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());
			m_compiledExpressionId = compiler.AddAbs(inputExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex DivideNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddDivide(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex SubtractNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler, m_input1.GetLink());
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]), ExpressionType::Float_1);
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler, m_input2.GetLink());
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]), ExpressionType::Float_1);
			}

			m_compiledExpressionId = compiler.AddSubtract(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex WorldToTangentNormalNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (m_input.IsLinked())
			{
				inputExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());
			}
			
			m_compiledExpressionId = compiler.AddTransform(inputExpression, Space::World, Space::Tangent);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex NormalizeNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression for Normalize!");
				return IndexNone;
			}
			
			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler, m_input.GetLink());

			m_compiledExpressionId = compiler.AddNormalize(inputExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex AppendNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputAExpression = IndexNone;
			if (!m_inputA.IsLinked())
			{
				ELOG("Missing input A expression for append!");
				return IndexNone;
			}
			
			ExpressionIndex inputBExpression = IndexNone;
			if (!m_inputA.IsLinked())
			{
				ELOG("Missing input B expression for append!");
				return IndexNone;
			}

			inputAExpression = m_inputA.GetLink()->GetNode()->Compile(compiler, m_inputA.GetLink());
			inputBExpression = m_inputB.GetLink()->GetNode()->Compile(compiler, m_inputB.GetLink());

			m_compiledExpressionId = compiler.AddAppend(inputAExpression, inputBExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex TextureNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			int32 uvExpression = IndexNone;

			if (m_uvs.IsLinked())
			{
				uvExpression = m_uvs.GetLink()->GetNode()->Compile(compiler, m_uvs.GetLink());
			}

			m_compiledExpressionId = compiler.AddTextureSample(m_texturePath.GetPath(), uvExpression, false);
		}

		if (outputPin && outputPin != &m_rgba)
		{
			if (outputPin == &m_a)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, false, true);
			}
			if (outputPin == &m_r)
			{
				return compiler.AddMask(m_compiledExpressionId, true, false, false, false);
			}
			if (outputPin == &m_g)
			{
				return compiler.AddMask(m_compiledExpressionId, false, true, false, false);
			}
			if (outputPin == &m_b)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, true, false);
			}
			if (outputPin == &m_rgb)
			{
				return compiler.AddMask(m_compiledExpressionId, true, true, true, false);
			}
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex TextureParameterNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			int32 uvExpression = IndexNone;

			if (m_uvs.IsLinked())
			{
				uvExpression = m_uvs.GetLink()->GetNode()->Compile(compiler, m_uvs.GetLink());
			}

			m_compiledExpressionId = compiler.AddTextureParameterSample(m_textureName, m_texturePath.GetPath(), uvExpression, false);
		}

		if (outputPin && outputPin != &m_rgba)
		{
			if (outputPin == &m_a)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, false, true);
			}
			if (outputPin == &m_r)
			{
				return compiler.AddMask(m_compiledExpressionId, true, false, false, false);
			}
			if (outputPin == &m_g)
			{
				return compiler.AddMask(m_compiledExpressionId, false, true, false, false);
			}
			if (outputPin == &m_b)
			{
				return compiler.AddMask(m_compiledExpressionId, false, false, true, false);
			}
			if (outputPin == &m_rgb)
			{
				return compiler.AddMask(m_compiledExpressionId, true, true, true, false);
			}
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex MaterialFunctionOutputNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		// TODO!

		return IndexNone;
	}

	std::span<Pin*> MaterialFunctionNode::GetInputPins()
	{
		return {};
	}

	std::span<Pin*> MaterialFunctionNode::GetOutputPins()
	{
		return {};
	}

	std::string_view MaterialFunctionNode::GetName() const
	{
		return m_name;
	}

	ExpressionIndex MaterialFunctionNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		// TODO: Add material function ref

		return m_compiledExpressionId;
	}

	ImColor GetIconColor(const PinType type)
	{
	    switch (type)
	    {
	        default:
	        case PinType::Material: return ImColor(255,   255, 255);
	    }
	}
}
