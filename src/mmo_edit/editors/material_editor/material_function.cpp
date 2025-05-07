#include "material_function.h"
#include "graphics/material_compiler.h"

namespace mmo
{
    MaterialFunction::MaterialFunction(std::string_view name)
        : m_name(name)
    {
    }

    MaterialFunctionParam& MaterialFunction::AddInputParam(std::string_view name, MaterialFunctionParamType type)
    {
        m_inputParams.push_back({ String(name), type, IndexNone });
        return m_inputParams.back();
    }

    MaterialFunctionParam& MaterialFunction::AddOutputParam(std::string_view name, MaterialFunctionParamType type)
    {
        m_outputParams.push_back({ String(name), type, IndexNone });
        return m_outputParams.back();
    }

    ExpressionIndex MaterialFunction::Compile(MaterialCompiler& compiler, const String& output)
    {
        // Find the output
		auto it = std::find_if(m_outputParams.begin(), m_outputParams.end(), [&output](const MaterialFunctionParam& param) {
			return param.name == output;
			});

		if (it == m_outputParams.end())
		{
			return IndexNone;
		}

		if (it->expressionId != IndexNone)
		{
			return it->expressionId;
		}

        // Otherwise, return a default value
        ExpressionType exprType;
        switch (it->type)
        {
        case MaterialFunctionParamType::Float:
            exprType = ExpressionType::Float_1;
            break;
        case MaterialFunctionParamType::Float2:
            exprType = ExpressionType::Float_2;
            break;
        case MaterialFunctionParamType::Float3:
        case MaterialFunctionParamType::Texture:
            exprType = ExpressionType::Float_3;
            break;
        case MaterialFunctionParamType::Float4:
            exprType = ExpressionType::Float_4;
            break;
        default:
            exprType = ExpressionType::Float_3;
            break;
        }

        // Create a default value for the output
        return compiler.AddExpression("float3(0.0, 0.0, 0.0)", exprType);
    }
}