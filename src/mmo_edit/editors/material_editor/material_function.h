// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "base/typedefs.h"
#include "graphics/material.h"
#include "graphics/material_compiler.h"

namespace mmo
{
    class MaterialCompiler;

    /// @brief Describes the data type of a material function parameter
    enum class MaterialFunctionParamType
    {
        Float,
        Float2,
        Float3,
        Float4,
        Texture,
    };

    /// @brief Describes a material function input parameter
    struct MaterialFunctionParam
    {
        String name;
        MaterialFunctionParamType type;
        ExpressionIndex expressionId;
    };

    /// @brief This class stores the data for a material function
    class MaterialFunction
    {
    public:
        /// @brief Creates a new instance of the MaterialFunction class.
        explicit MaterialFunction(std::string_view name);
        
        virtual ~MaterialFunction() = default;

    public:
        /// @brief Gets the name of this material function.
        [[nodiscard]] std::string_view GetName() const { return m_name; }

        /// @brief Sets the name of this material function.
        void SetName(std::string_view name) { m_name = name; }

        /// @brief Adds an input parameter to the material function.
        /// @param name Name of the input parameter.
        /// @param type The data type of the parameter.
        /// @return A reference to the created parameter.
        MaterialFunctionParam& AddInputParam(std::string_view name, MaterialFunctionParamType type);

        /// @brief Gets all input parameters.
        [[nodiscard]] const std::vector<MaterialFunctionParam>& GetInputParams() const { return m_inputParams; }

        /// @brief Gets the output parameter.
        [[nodiscard]] const std::vector<MaterialFunctionParam>& GetOutputs() const { return m_outputParams; }

        /// @brief Sets the output parameter.
        /// @param name Name of the output parameter.
        /// @param type The data type of the parameter.
        MaterialFunctionParam& AddOutputParam(std::string_view name, MaterialFunctionParamType type);

        /// @brief Sets the material graph data for this function.
        /// @param graphData Binary data containing the serialized material graph.
        void SetGraphData(std::vector<uint8> data) { m_graphData = std::move(data); }

        /// @brief Gets the material graph data.
        [[nodiscard]] const std::vector<uint8>& GetGraphData() const { return m_graphData; }

        /// @brief Compiles this material function with the given compiler
        /// @param compiler The compiler to use
        /// @return Expression index of the function output
        ExpressionIndex Compile(MaterialCompiler& compiler, const String& output);

    private:
        String m_name;
        std::vector<MaterialFunctionParam> m_inputParams;
        std::vector<MaterialFunctionParam> m_outputParams;
        std::vector<uint8> m_graphData;
    };

    typedef std::shared_ptr<MaterialFunction> MaterialFunctionPtr;
}
