// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <sstream>
#include <vector>

#include "shader_types.h"
#include "base/typedefs.h"
#include "math/vector4.h"
#include "graphics/material.h"

namespace mmo
{
	class ShaderCompiler;
	class Material;
	
	enum { IndexNone = -1 };

	/// @brief Enumerates available expression types.
	enum class ExpressionType : uint8
	{
		Unknown,

		/// @brief A single float expression (float).
		Float_1,

		/// @brief A two-float expression (float2).
		Float_2,

		/// @brief A three-float expression (float3).
		Float_3,

		/// @brief A four-float expression (float4).
		Float_4
	};

	enum class Space : uint8
	{
		Local,
		World,
		View,
		Screen,
		Tangent
	};

	enum class SamplerType : uint8
	{
		Color,
		Normal,

		Count
	};

	/// @brief Gets the amount of components for a given expression type. For example, Float4 has 4 components.
	/// @param type The expression type.
	/// @return The amount of components for a given expression type.
	uint32 GetExpressionTypeComponentCount(ExpressionType type);

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
		/// @brief Gets the generated high level vertex shader code of the last generated vertex shader type.
		[[nodiscard]] const String& GetVertexShaderCode() const { return m_vertexShaderCode; }

		/// @brief Gets the generated high level vertex shader code for a specific vertex shader type.
		///	       Only valid after a call to @ref Compile, which generates and stores all variants.
		[[nodiscard]] const String& GetVertexShaderCode(VertexShaderType type) const { return m_vertexShaderCodeByType[static_cast<int>(type)]; }

		/// @brief Gets the generated high level pixel shader code.
		[[nodiscard]] const String& GetPixelShaderCode(PixelShaderType type) const { return m_pixelShaderCode[(int)type]; }

	public:
		/// @brief Enables or disables generation of debug comments in the produced HLSL code.
		///	       When enabled, generated numbered expressions (expr_N) are annotated with the
		///		   node graph node they originated from (see @ref AnnotateExpression). This makes
		///		   it much easier to map the generated shader code back to the visual node graph.
		///		   Comments are stripped by the shader compiler and have no effect on the binary output.
		void SetGenerateDebugComments(const bool enable) { m_generateDebugComments = enable; }

		/// @brief Whether debug comment generation is currently enabled.
		[[nodiscard]] bool IsGeneratingDebugComments() const { return m_generateDebugComments; }

		/// @brief Associates a human readable comment with a generated expression index. Used to map
		///	       a numbered expression (expr_N) back to the node in the material graph that produced it.
		/// @param index The expression index to annotate.
		/// @param comment The comment text to associate with the expression.
		void AnnotateExpression(ExpressionIndex index, std::string_view comment);

		/// @brief Gets the comment associated with a given expression index, or nullptr if none was set.
		/// @param index The expression index to query.
		/// @return Pointer to the associated comment or nullptr if there is none.
		[[nodiscard]] const String* GetExpressionComment(ExpressionIndex index) const;

	public:
		/// @brief Adds a global shader function.
		/// @param name Name of the function to execute.
		/// @param code Code of the function.
		virtual void AddGlobalFunction(std::string_view name, std::string_view code) = 0;

		/// @brief Adds a custom expression.
		/// @param code Code of the expression.
		/// @param type Type of the expression.
		/// @return Expression index or IndexNone in case of an error.
		virtual ExpressionIndex AddExpression(std::string_view code, ExpressionType type = ExpressionType::Float_4) = 0;

		/// @brief Gets the expression type.
		/// @param index 
		/// @return 
		virtual ExpressionType GetExpressionType(ExpressionIndex index);

		/// @brief Notifies the compiler that a certain texture coordinate index is required by the material.
		/// @param textureCoordinateIndex The texture coordinate index required.
		virtual void NotifyTextureCoordinateIndex(uint32 textureCoordinateIndex) = 0;

		/// @brief Sets the index of the material's base color expression or IndexNone to not use a custom base color
		///	       expression at all.
		/// @param expression Index of the base color expression to use or IndexNone to not use a base color expression at all.
		virtual void SetBaseColorExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's emissive color expression or IndexNone to not use a custom base color
		///	       expression at all.
		/// @param expression Index of the emissive color expression to use or IndexNone to not use a emissive color expression at all.
		virtual void SetEmissiveExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's metallic expression or IndexNone to not use a custom metallic
		///	       expression at all.
		/// @param expression Index of the metallic expression to use or IndexNone to not use a metallic expression at all.
		virtual void SetMetallicExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's roughness expression or IndexNone to not use a custom base color
		///	       expression at all.
		/// @param expression Index of the roughness expression to use or IndexNone to not use a roughness expression at all.
		virtual void SetRoughnessExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's roughness expression or IndexNone to not use a custom specular
		///	       expression at all.
		/// @param expression Index of the specular expression to use or IndexNone to not use a specular expression at all.
		virtual void SetSpecularExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's normal expression or IndexNone to not use a custom normal
		///	       expression at all.
		/// @param expression Index of the normal expression to use or IndexNone to not use a normal expression at all.
		virtual void SetNormalExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's ambient occlusion expression or IndexNone to not use a custom ambient occlusion
		///	       expression at all.
		/// @param expression Index of the ambient occlusion expression to use or IndexNone to not use a ambient occlusion expression at all.
		virtual void SetAmbientOcclusionExpression(ExpressionIndex expression);

		/// @brief Sets the index of the material's opacity expression or IndexNone to not use a custom opacity
		///	       expression at all.
		/// @param expression Index of the opacity expression to use or IndexNone to not use a opacity expression at all.
		virtual void SetOpacityExpression(ExpressionIndex expression);

		/// @brief Adds a texture coordinate expression.
		/// @param coordinateIndex The texture coordinate index used.
		/// @return Index of the new texture coordinate expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTextureCoordinate(int32 coordinateIndex) = 0;

		/// @brief Adds a texture sample expression.
		/// @param texture The texture file name to be sampled.
		/// @param coordinates The texture coordinate expression to use for the sample.
		/// @param srgb Whether the image uses srgb.
		/// @return Index of the texture sample expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTextureSample(std::string_view texture, ExpressionIndex coordinates, bool srgb, SamplerType type) = 0;

		virtual ExpressionIndex AddTextureParameterSample(std::string_view name, std::string_view texture, ExpressionIndex coordinates, bool srgb, SamplerType type) = 0;

		virtual ExpressionIndex AddScalarParameterExpression(std::string_view name, float defaultValue) = 0;

		virtual ExpressionIndex AddVectorParameterExpression(std::string_view name, const Vector4& defaultValue) = 0;

		/// @brief Adds a reference to a global scalar shader parameter (shared by all materials).
		/// @param name Name of the global parameter as defined in the GlobalShaderParameters registry.
		/// @return Index of the expression (Float_1) or IndexNone in case of an error.
		virtual ExpressionIndex AddGlobalScalarParameterExpression(std::string_view name) = 0;

		/// @brief Adds a reference to a global vector shader parameter (shared by all materials).
		/// @param name Name of the global parameter as defined in the GlobalShaderParameters registry.
		/// @return Index of the expression (Float_4) or IndexNone in case of an error.
		virtual ExpressionIndex AddGlobalVectorParameterExpression(std::string_view name) = 0;

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
		
		/// @brief Adds an subtract expression.
		/// @param first The first expression of the addition (left side).
		/// @param second The second expression of the addition (right side).
		/// @return Index of the expression or IndexNone in case of an error.
		virtual ExpressionIndex AddSubtract(ExpressionIndex first, ExpressionIndex second) = 0;
		
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

		/// @brief Adds a world position expression.
		/// @return The world position expression index or IndexNone in case of an error.
		virtual ExpressionIndex AddWorldPosition() = 0;

		/// @brief Adds a camera world position expression.
		/// @return The camera world position expression index or IndexNone in case of an error.
		virtual ExpressionIndex AddCameraPosition() = 0;

		/// @brief Adds a camera vector (view direction) expression.
		/// @return The camera vector (view direction) expression index or IndexNone in case of an error.
		virtual ExpressionIndex AddCameraVector() = 0;

		/// @brief Adds a mask expression.
		/// @param input The input expression whose value should be used.
		/// @param r Whether the red channel will be included in the result.
		/// @param g Whether the green channel will be included in the result.
		/// @param b Whether the blue channel will be included in the result.
		/// @param a Whether the alpha channel will be included in the result.
		/// @return Index of the mask expression or IndexNone in case of an error.
		virtual ExpressionIndex AddMask(ExpressionIndex input, bool r, bool g, bool b, bool a) = 0;

		/// @brief Adds a vertex normal expression.
		/// @return Index of the vertex normal expression or IndexNone in case of an error.
		virtual ExpressionIndex AddVertexNormal() = 0;

		/// @brief Adds a divide expression.
		/// @param first Index of first expression. Formula is first / second.
		/// @param second Index of second expression. Formula is first / second.
		/// @return Index of the divide expression or IndexNone in case of an error.
		virtual ExpressionIndex AddDivide(ExpressionIndex first, ExpressionIndex second) = 0;

		/// @brief Adds an abs expression.
		/// @param input Input expression whose value the abs will be calculated from.
		/// @return Index of the abs expression or IndexNone in case of an error.
		virtual ExpressionIndex AddAbs(ExpressionIndex input) = 0;

		/// @brief Adds a normalize expression.
		/// @param input Input expression whose value will be normalized.
		/// @return Index of the normalize expression or IndexNone in case of an error.
		virtual ExpressionIndex AddNormalize(ExpressionIndex input) = 0;
		
		/// @brief Adds a vertex color expression.
		/// @return Index of the vertex color expression or IndexNone in case of an error.
		virtual ExpressionIndex AddVertexColor() = 0;

		/// @brief Adds an append expression.
		/// @param first Index of first expression.
		/// @param second Index of second expression.
		/// @return Index of the append expression or IndexNone in case of an error.
		virtual ExpressionIndex AddAppend(ExpressionIndex first, ExpressionIndex second) = 0;

		/// @brief Adds a transform expression.
		/// @param input The input expression whose value should be transformed.
		/// @param sourceSpace The source space.
		/// @param targetSpace The target space.
		/// @return Index of the transform expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTransform(ExpressionIndex input, Space sourceSpace, Space targetSpace) = 0;

		/// @brief Adds a time expression that returns the elapsed time in seconds since game start.
		/// @return Index of the time expression or IndexNone in case of an error.
		virtual ExpressionIndex AddTime() = 0;

		/// @brief Adds a rotator expression that rotates 2D texture coordinates around a center point.
		/// @param coordinates The input texture coordinate expression (float2).
		/// @param center The center of rotation expression (float2).
		/// @param rotation The rotation angle expression in radians (float1).
		/// @return Index of the rotated coordinates expression (float2) or IndexNone in case of an error.
		virtual ExpressionIndex AddRotator(ExpressionIndex coordinates, ExpressionIndex center, ExpressionIndex rotation) = 0;

		/// @brief Adds a Fresnel expression using the Schlick approximation.
		/// @param exponent The exponent expression controlling the falloff (float1).
		/// @param baseReflectFraction The base reflectivity at normal incidence (float1).
		/// @param normal The surface normal expression (float3). If IndexNone, uses the default vertex normal.
		/// @return Index of the Fresnel expression (float1) or IndexNone in case of an error.
		virtual ExpressionIndex AddFresnel(ExpressionIndex exponent, ExpressionIndex baseReflectFraction, ExpressionIndex normal) = 0;

		/// @brief Adds a pixel depth expression (linear view-space distance from camera to the current pixel).
		/// @return Index of the pixel depth expression (float1) or IndexNone in case of an error.
		virtual ExpressionIndex AddPixelDepth() = 0;

		/// @brief Adds a scene depth expression (linear depth of the opaque scene at this pixel).
		/// @details Requires the engine to bind the G-buffer normal render target at texture register t15.
		///          The depth value is stored in the alpha channel of that texture.
		/// @return Index of the scene depth expression (float1) or IndexNone in case of an error.
		virtual ExpressionIndex AddSceneDepth() = 0;

		/// @brief Adds a screen position expression (pixel-space screen coordinates of the current pixel).
		/// @return Index of the screen position expression (float2) or IndexNone in case of an error.
		virtual ExpressionIndex AddScreenPosition() = 0;

		/// @brief Adds a saturate expression that clamps the input to [0,1].
		/// @param input The input expression to saturate.
		/// @return Index of the saturate expression (same type as input) or IndexNone in case of an error.
		virtual ExpressionIndex AddSaturate(ExpressionIndex input) = 0;

		/// @brief Adds a scene color expression that samples the lit opaque scene behind this pixel.
		/// @details Requires the engine to bind the captured scene color at texture register t14.
		///          Used primarily for refraction by offsetting the lookup in screen pixels.
		/// @param screenOffset Optional screen-space offset in pixels (float2). IndexNone for no offset.
		/// @return Index of the scene color expression (float3) or IndexNone in case of an error.
		virtual ExpressionIndex AddSceneColor(ExpressionIndex screenOffset) = 0;

	public:
		void SetDepthTestEnabled(const bool enable) { m_depthTest = enable; }

		void SetDepthWriteEnabled(const bool enable) { m_depthWrite = enable; }

		void SetLit(const bool enable) { m_lit = enable; }

		void SetTranslucent(const bool enable) { m_translucent = enable; }

		void SetMasked(const bool enable) { m_masked = enable; }

		void SetTwoSided(const bool enable) { m_twoSided = enable; }

		void SetIsUserInterface(const bool enable) { m_userInterface = enable; }

	protected:
		/// @brief Called to generate the vertex shader code.
		virtual void GenerateVertexShaderCode(VertexShaderType type) = 0;

		/// @brief Called to generate the pixel shader code.
		virtual void GeneratePixelShaderCode(PixelShaderType type = PixelShaderType::Forward) = 0;

	protected:
		std::vector<std::string> m_textures;
		uint32 m_numTexCoordinates { 0 };
		
		std::map<String, String> m_globalFunctions;
		std::vector<String> m_expressions;
		std::vector<ExpressionType> m_expressionTypes;

		ExpressionIndex m_baseColorExpression { IndexNone };			// Float3
		ExpressionIndex m_emissiveExpression { IndexNone };				// Float3
		ExpressionIndex m_normalExpression { IndexNone };				// Float3 (Tangent Space)
		ExpressionIndex m_roughnessExpression { IndexNone };			// Float1 (0-1)
		ExpressionIndex m_specularExpression{ IndexNone };			// Float1 (0-1)
		ExpressionIndex m_ambientOcclusionExpression { IndexNone };	// Float3
		ExpressionIndex m_metallicExpression { IndexNone };			// Float1 (0-1)
		ExpressionIndex m_opacityExpression{ IndexNone };			// Float1 (0-1)

		Material* m_material { nullptr };
		String m_vertexShaderCode;
		String m_vertexShaderCodeByType[6];
		String m_pixelShaderCode[4];

		/// @brief Whether debug comments mapping expressions to graph nodes should be emitted.
		bool m_generateDebugComments { false };

		/// @brief Maps an expression index to a human readable comment (node name / id) for debugging.
		std::map<ExpressionIndex, String> m_expressionComments;
		//std::ostringstream m_vertexShaderStream;
		std::ostringstream m_pixelShaderStream;

		bool m_lit { true };
		bool m_depthTest { true };
		bool m_depthWrite { true };
		bool m_translucent{ false };
		bool m_masked{ false };
		bool m_twoSided { false };
		bool m_userInterface{ false };

		/// @brief Whether this material references at least one global shader parameter and thus
		///        needs the shared GlobalParameters constant buffer declared in its shader.
		bool m_usesGlobalParameters { false };

	};
}
