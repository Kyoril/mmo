// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <sstream>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	class ShaderCompiler;
	class Material;
	
	enum { IndexNone = -1 };

	/// @brief Typedef for a material expression index.
	using ExpressionIndex = int32;

	/// @brief Base class of a material compiler which is expected to generate high level shader code
	///	       based on material expressions, which should then be able to be sent to a shader compiler
	///		   to create actual binary shader code.
	class MaterialCompiler
	{
	protected:
		/// @brief Default constructor protected to make this class abstract.
		MaterialCompiler() = default;

	public:
		/// @brief Virtual default destructor because of inheritance.
		virtual ~MaterialCompiler() = default;

	public:
		/// @brief Generates shader code.
		/// @param material 
		/// @param shaderCompiler 
		void Compile(Material& material, ShaderCompiler& shaderCompiler);

	public:
		/// @brief Gets the generated high level vertex shader code.
		[[nodiscard]] const String& GetVertexShaderCode() const noexcept { return m_vertexShaderCode; }

		/// @brief Gets the generated high level pixel shader code.
		[[nodiscard]] const String& GetPixelShaderCode() const noexcept { return m_pixelShaderCode; }

	public:
		/// @brief Adds a global shader function.
		/// @param name Name of the function to execute.
		/// @param code Code of the function.
		virtual void AddGlobalFunction(std::string_view name, std::string_view code) = 0;

		/// @brief Adds a custom expression.
		/// @param code Code of the expression.
		/// @return Expression index or IndexNone in case of an error.
		virtual ExpressionIndex AddExpression(std::string_view code) = 0;

		/// @brief Notifies the compiler that a certain texture coordinate index is required by the material.
		/// @param textureCoordinateIndex The texture coordinate index required.
		virtual void NotifyTextureCoordinateIndex(uint32 textureCoordinateIndex) = 0;

		/// @brief Sets the index of the material's base color expression or IndexNone to not use a custom base color
		///	       expression at all.
		/// @param expression Index of the base color expression to use or IndexNone to not use a base color expression at all.
		virtual void SetBaseColorExpression(ExpressionIndex expression) = 0;

		/// @brief Adds a texture coordinate expression.
		/// @param coordinateIndex The texture coordinate index used.
		/// @return Index of the new texture coordinate expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTextureCoordinate(int32 coordinateIndex) = 0;

		/// @brief Adds a texture sample expression.
		/// @param texture The texture file name to be sampled.
		/// @param coordinates The texture coordinate expression to use for the sample.
		/// @return Index of the texture sample expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTextureSample(std::string_view texture, ExpressionIndex coordinates) = 0;

		/// @brief Adds a multiply expression.
		/// @param first The first expression for the multiply (left side).
		/// @param second The second expression for the multiply (right side).
		/// @return Index of the texture sample or IndexNone in case of an error.
		virtual ExpressionIndex AddMultiply(ExpressionIndex first, ExpressionIndex second) = 0;

		/// @brief Adds an addition expression.
		/// @param first The first expression of the addition (left side).
		/// @param second The second expression of the addition (right side).
		/// @return Index of the expression or IndexNone in case of an error.
		virtual ExpressionIndex AddAddition(ExpressionIndex first, ExpressionIndex second) = 0;
		
		/// @brief Adds a dot expression.
		/// @param first The first expression for the multiply (left side).
		/// @param second The second expression for the multiply (right side).
		/// @return Index of the dot expression or IndexNone in case of an error.
		virtual ExpressionIndex AddDot(ExpressionIndex first, ExpressionIndex second) = 0;
		
		/// @brief Adds a clamp expression.
		/// @param value The value expression for the clamp.
		/// @param min The min value expression for the clamp.
		/// @param max The max value expression for the clamp.
		/// @return Index of the clamp expression or IndexNone in case of an error.
		virtual ExpressionIndex AddClamp(ExpressionIndex value, ExpressionIndex min, ExpressionIndex max) = 0;
		
		/// @brief Adds a clamp expression.
		/// @param input The expression whose values should be subtracted from one.
		/// @return Index of the one minus expression or IndexNone in case of an error.
		virtual ExpressionIndex AddOneMinus(ExpressionIndex input) = 0;
		
		/// @brief Adds a power expression.
		/// @param base The expression whose value should be the base.
		/// @param exponent The expression whose value should be the exponent.
		/// @return Index of the power expression or IndexNone in case of an error.
		virtual ExpressionIndex AddPower(ExpressionIndex base, ExpressionIndex exponent) = 0;

		/// @brief Adds a linear interpolation expression.
		/// @param first Index of the source value expression of the linear interpolation.
		/// @param second Index of the target value expression of the linear interpolation.
		/// @param alpha Index of the alpha value expression of the linear interpolation.
		/// @return Index of the expression or IndexNone in case of an error.
		virtual ExpressionIndex AddLerp(ExpressionIndex first, ExpressionIndex second, ExpressionIndex alpha) = 0;

	public:
		void SetDepthTestEnabled(const bool enable) noexcept { m_depthTest = enable; }

		void SetDepthWriteEnabled(const bool enable) noexcept { m_depthWrite = enable; }

	protected:
		/// @brief Called to generate the vertex shader code.
		virtual void GenerateVertexShaderCode() = 0;

		/// @brief Called to generate the pixel shader code.
		virtual void GeneratePixelShaderCode() = 0;

	protected:
		std::vector<std::string> m_textures;
		uint32 m_numTexCoordinates { 0 };
		
		std::map<String, String> m_globalFunctions;
		std::vector<String> m_expressions;
		int32 m_baseColorExpression { IndexNone };

		Material* m_material { nullptr };
		String m_vertexShaderCode;
		String m_pixelShaderCode;
		std::ostringstream m_vertexShaderStream;
		std::ostringstream m_pixelShaderStream;

		bool m_depthTest { true };
		bool m_depthWrite { true };
	};
}
