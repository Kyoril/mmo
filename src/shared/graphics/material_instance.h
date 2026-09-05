// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <array>
#include <map>

#include "base/typedefs.h"

#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <span>
#include <memory>
#include <unordered_map>

#include "material.h"

namespace mmo
{
	/// @brief This class represents a material which describes how geometry in the scene
	///	       graph should be rendered.
	class MaterialInstance : public MaterialInterface
	{
	public:
		/// @brief Creates a new instance of the Material class and initializes it.
		explicit MaterialInstance(std::string_view name, MaterialPtr parentMaterial);
		
		virtual ~MaterialInstance() override = default;

	public:
		virtual std::shared_ptr<Material> GetBaseMaterial() override { return m_parent->GetBaseMaterial(); }

		/// @brief Sets whether this material should render geometry without backface culling.
		/// @param value True if both sides of geometry should be rendered, false to cull the back face.
		virtual void SetTwoSided(const bool value) override { m_twoSided = value; }

		/// @brief Gets whether this material should render geometry without backface culling.
		[[nodiscard]] bool IsTwoSided() const override { return m_twoSided; }

		/// @brief Sets whether this material casts shadows.
		/// @param value True if this material should cast shadows.
		void SetCastShadows(const bool value) override { m_castShadows = value; }

		/// @brief Gets whether this material casts shadows.
		[[nodiscard]] bool IsCastingShadows() const override { return m_castShadows; }

		/// @brief Sets whether this material should receive shadows.
		/// @param receive True if the material should receive shadows.
		void SetReceivesShadows(const bool receive) override { m_receiveShadows = receive; }

		/// @brief Gets whether this material is receiving shadows.
		[[nodiscard]] bool IsReceivingShadows() const override { return m_receiveShadows; }

		/// @brief Sets the type of the material.
		/// @param value The new material type.
		void SetType(const MaterialType value) override { m_type = value; }

		/// @brief Gets the type of this material.
		[[nodiscard]] MaterialType GetType() const override { return m_type; }

		/// @brief Gets whether this material is translucent.
		[[nodiscard]] bool IsTranslucent() const override { ASSERT(m_parent); return m_parent->IsTranslucent(); }

		/// @brief Gets whether this material is receiving light.
		[[nodiscard]] bool IsLit() const override { ASSERT(m_parent); return m_parent->IsLit(); }

		[[nodiscard]] bool IsDepthTestEnabled() const override { return m_depthTest; }

		void SetDepthTestEnabled(const bool enable) override { m_depthTest = enable; }

		[[nodiscard]] bool IsDepthWriteEnabled() const override { return m_depthWrite; }

		void SetDepthWriteEnabled(const bool enable) override { m_depthWrite = enable; }

		[[nodiscard]] std::string_view GetName() const override { return m_name; }

		/// @brief Sets the name of the material.
		/// @param name The material name.
		void SetName(const std::string_view name) { m_name = name; }

		void SetParent(MaterialPtr parent);

		[[nodiscard]] MaterialPtr GetParent() const { return m_parent; }

		std::shared_ptr<MaterialInstance> AsShared();

		void RefreshParametersFromBase();

	public:
		void DerivePropertiesFromParent();

	public:
		/// @brief Ensures that the material is loaded.
		void Update() override;

		ShaderPtr& GetVertexShader(VertexShaderType type) override { return m_parent->GetVertexShader(type); }

		ShaderPtr& GetPixelShader(PixelShaderType type = PixelShaderType::Forward) override { return m_parent->GetPixelShader(type); }

		void Apply(GraphicsDevice& device, MaterialDomain domain, PixelShaderType pixelShaderType = PixelShaderType::Forward) override;

		ConstantBufferPtr GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device) override;

		void ClearParameters() override;

		void AddScalarParameter(std::string_view name, float defaultValue) override;

		void SetScalarParameter(std::string_view name, float value) override;

		bool GetScalarParameter(std::string_view name, float& out_value) override;

		void AddVectorParameter(std::string_view name, const Vector4& defaultValue) override;

		void SetVectorParameter(std::string_view name, const Vector4& value) override;

		bool GetVectorParameter(std::string_view name, Vector4& out_value) override;

		void AddTextureParameter(std::string_view name, const String& defaultValue) override;

		void SetTextureParameter(std::string_view name, const String& value) override;

		void SetTextureParameter(std::string_view name, const TexturePtr& value) override;

		bool GetTextureParameter(std::string_view name, String& out_value) override;

		const std::vector<ScalarParameterValue>& GetScalarParameters() const override { return m_scalarParameters; }

		const std::vector<VectorParameterValue>& GetVectorParameters() const override { return m_vectorParameters; }

		const std::vector<TextureParameterValue>& GetTextureParameters() const override { return m_textureParameters; }

		bool IsWireframe() const override { return m_wireframe; }

		void SetWireframe(bool value) override { m_wireframe = value; }

	public:
		/// @brief Gets the effective foliage entries: the instance's override list when overriding,
		///        otherwise the parent material's effective list.
		[[nodiscard]] const std::vector<MaterialFoliageEntry>& GetFoliageEntries() const override
		{
			if (m_overrideFoliage)
			{
				return m_foliage;
			}

			ASSERT(m_parent);
			return m_parent->GetFoliageEntries();
		}

		/// @brief Gets mutable access to the instance's own (override) foliage entries.
		[[nodiscard]] std::vector<MaterialFoliageEntry>& GetOwnFoliageEntries() { return m_foliage; }

		/// @brief Replaces the instance's own (override) foliage entries.
		void SetOwnFoliageEntries(std::vector<MaterialFoliageEntry> entries) { m_foliage = std::move(entries); }

		/// @brief Whether this instance overrides the parent's foliage with its own list.
		[[nodiscard]] bool IsOverridingFoliage() const { return m_overrideFoliage; }

		/// @brief Enables or disables foliage override for this instance.
		void SetOverrideFoliage(const bool value) { m_overrideFoliage = value; }

	public:
		/// @brief Gets the effective base surface type: the instance's override when overriding,
		///        otherwise the parent material's surface type.
		[[nodiscard]] uint32 GetSurfaceTypeId() const override
		{
			if (m_overrideSurfaceTypes)
			{
				return m_surfaceTypeId;
			}

			return m_parent ? m_parent->GetSurfaceTypeId() : 0;
		}

		/// @brief Gets the effective layer surface type: the instance's override when overriding,
		///        otherwise the parent material's layer surface type.
		[[nodiscard]] uint32 GetLayerSurfaceTypeId(const uint8 layer) const override
		{
			if (m_overrideSurfaceTypes)
			{
				return layer < m_layerSurfaceTypeIds.size() ? m_layerSurfaceTypeIds[layer] : 0;
			}

			return m_parent ? m_parent->GetLayerSurfaceTypeId(layer) : 0;
		}

		/// @brief Whether this instance overrides the parent material's surface types.
		[[nodiscard]] bool IsOverridingSurfaceTypes() const { return m_overrideSurfaceTypes; }

		/// @brief Sets whether this instance overrides the parent material's surface types.
		void SetOverrideSurfaceTypes(const bool value) { m_overrideSurfaceTypes = value; }

		/// @brief Gets the instance-level base surface type id (only used when overriding).
		[[nodiscard]] uint32 GetOwnSurfaceTypeId() const { return m_surfaceTypeId; }

		/// @brief Sets the instance-level base surface type id.
		void SetOwnSurfaceTypeId(const uint32 id) { m_surfaceTypeId = id; }

		/// @brief Gets an instance-level layer surface type id (only used when overriding).
		[[nodiscard]] uint32 GetOwnLayerSurfaceTypeId(const uint8 layer) const { return layer < m_layerSurfaceTypeIds.size() ? m_layerSurfaceTypeIds[layer] : 0; }

		/// @brief Sets an instance-level layer surface type id.
		void SetOwnLayerSurfaceTypeId(const uint8 layer, const uint32 id)
		{
			if (layer < m_layerSurfaceTypeIds.size())
			{
				m_layerSurfaceTypeIds[layer] = id;
			}
		}

		/// @brief Gets the parent material's layer binding.
		/// @details Unlike foliage and surface types, layer bindings are deliberately NOT
		///          overridable per instance, and there is no MBND chunk in the .hmi format. A
		///          binding names shader parameters that exist only because the parent's
		///          compiled graph declared them, and an instance cannot add or rename a
		///          parameter (AddScalarParameter is a no-op by design). An instance-level
		///          binding could therefore only ever point at something the parent already has,
		///          i.e. it could only ever be wrong. Please do not "fix" this asymmetry.
		[[nodiscard]] const MaterialLayerBinding& GetLayerBinding(const uint8 layer) const override
		{
			static const MaterialLayerBinding s_empty{};
			return m_parent ? m_parent->GetLayerBinding(layer) : s_empty;
		}

		/// @copydoc MaterialInterface::GetParameterRevision
		[[nodiscard]] uint32 GetParameterRevision() const override
		{
			return m_parent ? m_parent->GetParameterRevision() : 0;
		}

		/// @brief Re-derives the parameter list from the parent if the parent has been
		///        recompiled with a different set of parameters since this instance copied it.
		/// @details Cheap: an integer compare in the common case. Called before the instance
		///          binds anything, because binding a stale list against a freshly compiled
		///          shader puts every resource past the first change into the wrong register.
		void SyncParametersIfStale()
		{
			if (!m_parent)
			{
				return;
			}

			const uint32 revision = m_parent->GetParameterRevision();
			if (revision == m_syncedParameterRevision)
			{
				return;
			}

			RefreshParametersFromBase();
			m_syncedParameterRevision = revision;
		}

		/// @copydoc MaterialInterface::GetLayerBlendSharpnessParam
		[[nodiscard]] const String& GetLayerBlendSharpnessParam() const override
		{
			static const String s_empty{};
			return m_parent ? m_parent->GetLayerBlendSharpnessParam() : s_empty;
		}

	private:
		String m_name;
		MaterialPtr m_parent;

		bool m_twoSided;
		bool m_castShadows;
		bool m_receiveShadows;
		MaterialType m_type;
		bool m_depthTest;
		bool m_depthWrite;
		bool m_wireframe;

		std::vector<ScalarParameterValue> m_scalarParameters;
		std::vector<VectorParameterValue> m_vectorParameters;
		std::vector<TextureParameterValue> m_textureParameters;
		std::unordered_map<String, TexturePtr> m_textureParamTextures;

		/// Instance-level foliage override. Only used when m_overrideFoliage is true; otherwise the
		/// parent material's foliage entries apply.
		std::vector<MaterialFoliageEntry> m_foliage;
		bool m_overrideFoliage = false;

		/// Instance-level surface type override. Only used when m_overrideSurfaceTypes is true;
		/// otherwise the parent material's surface types apply.
		uint32 m_surfaceTypeId = 0;
		std::array<uint32, 4> m_layerSurfaceTypeIds{};
		bool m_overrideSurfaceTypes = false;

		/// Parent parameter revision this instance's list was derived from.
		uint32 m_syncedParameterRevision { 0 };

		bool m_bufferLayoutDirty[3]{ true, true, true };
		bool m_bufferDataDirty[3]{ true, true, true };
		ConstantBufferPtr m_parameterBuffers[3]{ nullptr, nullptr, nullptr };
	};

}
