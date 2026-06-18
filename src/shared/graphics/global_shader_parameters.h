// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "shader_base.h"
#include "constant_buffer.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "math/vector4.h"

namespace mmo
{
	class GraphicsDevice;

	namespace global_shader_parameter_type
	{
		/// @brief Enumerates the supported value types of a global shader parameter.
		enum Type : uint8
		{
			/// @brief A single float value (stored in the x component).
			Scalar,

			/// @brief A four component float vector / color value.
			Vector
		};
	}

	typedef global_shader_parameter_type::Type GlobalShaderParameterType;

	/// @brief A single global shader variable: its definition (name + type + default) and its
	///        current runtime value. Scalars use only the x component of the value vector.
	struct GlobalShaderParameter
	{
		String name;
		GlobalShaderParameterType type { global_shader_parameter_type::Scalar };
		Vector4 value { 0.0f, 0.0f, 0.0f, 0.0f };
		Vector4 defaultValue { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	/// @brief Reserved pixel shader constant buffer register used for the global parameter buffer.
	///        It is deliberately chosen high enough so it never collides with the dynamically
	///        assigned per-material parameter buffers (Matrices=b0, CameraParameters=b1, then the
	///        per-material scalar/vector buffers at b2/b3).
	constexpr uint32 kGlobalShaderParametersPsSlot = 13;

	/// @brief Canonical asset path of the project-wide global shader parameter registry.
	constexpr const char* GlobalShaderParametersAssetPath = "Config/GlobalShaderParameters.hgsp";

	/// @brief Project-wide registry of global shader variables.
	///
	/// Global shader variables are shared by every material. They live in a single GPU constant
	/// buffer (bound at @ref kGlobalShaderParametersPsSlot) that every generated shader can
	/// reference by name, so changing a value here updates all materials at once.
	///
	/// The registry owns the canonical, ordered list of definitions and the backing GPU buffer.
	/// Both the material compiler (which emits the matching `cbuffer GlobalParameters`) and this
	/// class build the buffer layout with **all vectors first, then all scalars** so the packed
	/// CPU layout matches HLSL constant buffer packing rules.
	class GlobalShaderParameters final : public NonCopyable
	{
	private:
		GlobalShaderParameters() = default;

	public:
		/// @brief Singleton accessor.
		static GlobalShaderParameters& Get();

	public:
		/// @brief Removes all parameter definitions.
		void Clear();

		/// @brief Defines a new scalar global. Fails if the name already exists.
		/// @return true if the parameter was added.
		bool DefineScalar(std::string_view name, float defaultValue);

		/// @brief Defines a new vector/color global. Fails if the name already exists.
		/// @return true if the parameter was added.
		bool DefineVector(std::string_view name, const Vector4& defaultValue);

		/// @brief Removes a parameter definition by name.
		bool Remove(std::string_view name);

		/// @brief Renames an existing parameter. Fails if the new name already exists.
		bool Rename(std::string_view oldName, std::string_view newName);

	public:
		/// @brief Sets a scalar value by name. This is the "change anywhere in the engine" API.
		/// @return true if the parameter exists and is a scalar.
		bool SetScalar(std::string_view name, float value);

		/// @brief Sets a vector/color value by name. This is the "change anywhere in the engine" API.
		/// @return true if the parameter exists and is a vector.
		bool SetVector(std::string_view name, const Vector4& value);

		/// @brief Gets the current scalar value of a parameter.
		bool GetScalar(std::string_view name, float& out_value) const;

		/// @brief Gets the current vector value of a parameter.
		bool GetVector(std::string_view name, Vector4& out_value) const;

		/// @brief Sets the persisted default value (and the live value) of a parameter. Primarily used
		///        by the editor when authoring defaults. Does not change the layout.
		/// @return true if the parameter exists.
		bool SetDefault(std::string_view name, const Vector4& value);

	public:
		/// @brief Gets all parameter definitions in their canonical order.
		[[nodiscard]] const std::vector<GlobalShaderParameter>& GetParameters() const { return m_parameters; }

		/// @brief Determines whether a parameter with the given name exists.
		[[nodiscard]] bool Has(std::string_view name) const;

		/// @brief Finds a parameter definition by name or nullptr if it doesn't exist.
		[[nodiscard]] const GlobalShaderParameter* Find(std::string_view name) const;

	public:
		/// @brief Lazily creates / updates and returns the backing GPU constant buffer.
		/// The buffer is rebuilt when the layout changes and re-uploaded when a value changes.
		ConstantBufferPtr GetBuffer(GraphicsDevice& device);

	public:
		/// @brief Loads the registry from an asset. A missing file simply leaves the registry empty.
		/// @return true if the asset was found and read successfully.
		bool LoadFromAsset(std::string_view assetPath);

		/// @brief Saves the registry to an asset.
		/// @return true on success.
		bool SaveToAsset(std::string_view assetPath) const;

	public:
		/// @brief Fired whenever the set, order or type of parameters changes (not on value-only
		///        changes). Consumers (e.g. the editor) use this to recompile affected materials.
		signal<void()> OnLayoutChanged;

	private:
		GlobalShaderParameter* FindMutable(std::string_view name);
		void MarkLayoutDirty();

		/// @brief Builds the packed CPU buffer blob (vectors first, then scalars, padded to 16 bytes).
		[[nodiscard]] std::vector<uint8> BuildBufferData() const;

		/// @brief Computes the padded byte size of the GPU buffer (never zero).
		[[nodiscard]] size_t ComputeBufferSize() const;

	private:
		std::vector<GlobalShaderParameter> m_parameters;
		ConstantBufferPtr m_buffer;
		bool m_layoutDirty { true };
		bool m_dataDirty { true };
	};
}
