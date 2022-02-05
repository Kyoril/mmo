// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_node.h"

#include "imgui_node_editor.h"
#include "material_graph.h"
#include "log/default_log_levels.h"

static ImTextureID s_headerBackground = nullptr;

namespace mmo
{
	const uint32 ConstFloatNode::Color = ImColor(0.57f, 0.88f, 0.29f, 0.25f);
	const uint32 ConstVectorNode::Color = ImColor(0.88f, 0.88f, 0.29f, 0.25f);
	const uint32 MaterialNode::Color = ImColor(114.0f / 255.0f, 92.0f / 255.0f, 71.0f / 255.0f, 0.50f);
	const uint32 TextureNode::Color = ImColor(0.29f, 0.29f, 0.88f, 0.25f);
	const uint32 TextureCoordNode::Color = ImColor(0.88f, 0.0f, 0.0f, 0.25f);

	Pin::Pin(Node* node, const PinType type, const std::string_view name)
		: m_id(node ? node->GetMaterial()->MakePinId(this) : 0)
        , m_node(node)
        , m_type(type)
        , m_name(name)
	{
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

	bool AnyPin::SetValueType(PinType type)
	{
        if (GetValueType() == type)
	        return true;

	    if (m_innerPin)
	    {
	        m_node->GetMaterial()->ForgetPin(m_innerPin.get());
	        m_innerPin.reset();
	    }

	    if (type == PinType::Any)
	        return true;

	    m_innerPin = m_node->CreatePin(type);

	    if (const auto link = GetLink())
	    {
	        if (link->GetValueType() != type)
	        {
	            Unlink();
	            LinkTo(*link);
	        }
	    }

        const auto linkedToSet = m_node->GetMaterial()->FindPinsLinkedTo(*this);
	    for (const auto linkedTo : linkedToSet)
	    {
	        if (linkedTo->GetValueType() == type)
	            continue;

	        linkedTo->Unlink();
	        linkedTo->LinkTo(*this);
	    }

	    return true;
	}

	bool AnyPin::SetValue(PinValue value)
	{
		if (!m_innerPin)
		{
			return false;
        }

		return m_innerPin->SetValue(std::move(value));
	}

	Node::Node(MaterialGraph& material)
		: m_id(material.MakeNodeId(this))
		, m_material(&material)
	{
	}

	std::unique_ptr<Pin> Node::CreatePin(const PinType pinType, std::string_view name)
	{
        switch (pinType)
	    {
	        case PinType::Any:      return make_unique<AnyPin>(this, name);
	        case PinType::Bool:     return make_unique<BoolPin>(this, name);
			case PinType::Int32:    return make_unique<Int32Pin>(this, name);
	        case PinType::Float:    return make_unique<FloatPin>(this, name);
	        case PinType::String:   return make_unique<StringPin>(this, name);
	    }

	    return nullptr;
	}

	std::string_view Node::GetName() const
	{
		return GetTypeInfo().displayName;
	}

	LinkQueryResult Node::AcceptLink(const Pin& receiver, const Pin& provider) const
	{
        if (receiver.GetNode() == provider.GetNode())
	        return { false, "Pins of same node cannot be connected"};
        
	    if (receiver.IsInput() && provider.IsInput())
	        return { false, "Input pins cannot be linked together"};

	    if (receiver.IsOutput() && provider.IsOutput())
	        return { false, "Output pins cannot be linked together"};
        
	    if (provider.GetValueType() != receiver.GetValueType() && (provider.GetType() != PinType::Any && receiver.GetType() != PinType::Any))
	        return { false, "Incompatible types"};

	    return {true};
	}

	std::optional<uint32> Node::GetPinIndex(const Pin& pin)
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

	ExpressionIndex MaterialNode::Compile(MaterialCompiler& compiler)
	{
		compiler.SetLit(m_lit);
		compiler.SetDepthWriteEnabled(m_depthWrite);
		compiler.SetDepthTestEnabled(m_depthTest);

		if (m_baseColor.IsLinked())
		{
			const ExpressionIndex baseColorExpression = m_baseColor.GetLink()->GetNode()->Compile(compiler);
			compiler.SetBaseColorExpression(baseColorExpression);
		}

		return IndexNone;
	}

	ExpressionIndex ConstFloatNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddExpression(std::to_string(m_value));
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex ConstVectorNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			std::ostringstream strm;
			strm << "float4(";
			strm << m_value.GetRed() << ", " << m_value.GetGreen() << ", " << m_value.GetBlue() << ", " << m_value.GetAlpha();
			strm << ")";
			strm.flush();

			m_compiledExpressionId = compiler.AddExpression(strm.str());
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex AddNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]));
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]));
			}

			m_compiledExpressionId = compiler.AddAddition(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex MultiplyNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]));
			}

			ExpressionIndex secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]));
			}

			m_compiledExpressionId = compiler.AddMultiply(firstExpression, secondExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex MaskNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression!");
				return IndexNone;
			}

			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler);
			
			m_compiledExpressionId = compiler.AddMask(inputExpression, m_channels[0], m_channels[1], m_channels[2], m_channels[3]);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex DotNode::Compile(MaterialCompiler& compiler)
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
			
			firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			
			m_compiledExpressionId = compiler.AddDot(firstExpression, secondExpression);
		}
		
		return m_compiledExpressionId;
	}

	ExpressionIndex OneMinusNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex inputExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing input expression!");
				return IndexNone;
			}
			
			inputExpression = m_input.GetLink()->GetNode()->Compile(compiler);
			
			m_compiledExpressionId = compiler.AddOneMinus(inputExpression);
		}
		
		return m_compiledExpressionId;
	}

	ExpressionIndex ClampNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex valueExpression = IndexNone;
			if (!m_input.IsLinked())
			{
				ELOG("Missing base value expression for clamp");
				return IndexNone;
			}
			
			valueExpression = m_input.GetLink()->GetNode()->Compile(compiler);

			ExpressionIndex minExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				minExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				minExpression = compiler.AddExpression(std::to_string(m_values[0]));
			}

			ExpressionIndex maxExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				maxExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				maxExpression = compiler.AddExpression(std::to_string(m_values[1]));
			}

			m_compiledExpressionId = compiler.AddClamp(valueExpression, minExpression, maxExpression);
		}

		return m_compiledExpressionId;
	}

	ExpressionIndex PowerNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			ExpressionIndex baseExpression = IndexNone;
			if (!m_input1.IsLinked())
			{
				ELOG("Missing base expression");
				return IndexNone;
			}

			baseExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			
			ExpressionIndex exponentExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				exponentExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				exponentExpression = compiler.AddExpression(std::to_string(m_exponent));
			}

			m_compiledExpressionId = compiler.AddPower(baseExpression, exponentExpression);
		}

		return m_compiledExpressionId;
	}

	int32 LerpNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			int32 firstExpression = IndexNone;
			if (m_input1.IsLinked())
			{
				firstExpression = m_input1.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				firstExpression = compiler.AddExpression(std::to_string(m_values[0]));
			}

			int32 secondExpression = IndexNone;
			if (m_input2.IsLinked())
			{
				secondExpression = m_input2.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				secondExpression = compiler.AddExpression(std::to_string(m_values[1]));
			}

			int32 alphaExpression = IndexNone;
			if (m_input3.IsLinked())
			{
				alphaExpression = m_input3.GetLink()->GetNode()->Compile(compiler);
			}
			else
			{
				alphaExpression = compiler.AddExpression(std::to_string(m_values[2]));
			}

			m_compiledExpressionId = compiler.AddLerp(firstExpression, secondExpression, alphaExpression);
		}

		return m_compiledExpressionId;
	}

	int32 TextureCoordNode::Compile(MaterialCompiler& compiler)
	{
		compiler.NotifyTextureCoordinateIndex(m_uvCoordIndex);

		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddTextureCoordinate(m_uvCoordIndex);
		}

		return m_compiledExpressionId;
	}

	int32 WorldPositionNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			m_compiledExpressionId = compiler.AddWorldPosition();
		}

		return m_compiledExpressionId;
	}

	int32 TextureNode::Compile(MaterialCompiler& compiler)
	{
		if (m_compiledExpressionId == IndexNone)
		{
			int32 uvExpression = IndexNone;

			if (m_uvs.IsLinked())
			{
				uvExpression = m_uvs.GetLink()->GetNode()->Compile(compiler);
			}

			m_compiledExpressionId = compiler.AddTextureSample(m_texturePath.GetPath(), uvExpression);
		}

		return m_compiledExpressionId;
	}

	ImColor GetIconColor(const PinType type)
	{
	    switch (type)
	    {
	        default:
	        case PinType::Bool:     return ImColor(220,  48,  48);
	        case PinType::Int32:    return ImColor( 68, 201, 156);
	        case PinType::Float:    return ImColor(147, 226,  74);
	        case PinType::String:   return ImColor(124,  21, 153);
	        case PinType::Material: return ImColor(255,   255, 255);
	    }
	}
}
