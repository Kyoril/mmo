#pragma once

#include "character_customization_property.h"

#include <memory>
#include <vector>

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/macros.h"

namespace mmo
{
	namespace model_data_flags
	{
		enum Type
		{
			None = 0,

			IsCustomizable = 1 << 0,

			IsPlayerCharacter = 1 << 1,
		};
	}

	class AvatarConfiguration;
	class VisibilitySetPropertyGroup;
	class MaterialOverridePropertyGroup;
	class ScalarParameterPropertyGroup;

	struct VisibilitySetValue
	{
		std::string valueId;                // e.g. "LongHair", "ShortHair", etc.
		std::vector<std::string> visibleSubEntities;
	};

	struct MaterialOverrideValue
	{
		std::string valueId; // e.g. "DarkSkin", "FairSkin"

		// For each sub-entity, which material do we apply?
		// Could be a map or a vector of pairs.
		std::unordered_map<std::string, std::string> subEntityToMaterial;
	};

	class CustomizationPropertyGroupApplier
	{
	public:
		virtual ~CustomizationPropertyGroupApplier() = default;

	public:
		virtual void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) = 0;

		virtual void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) = 0;

		virtual void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) = 0;
	};

	class CustomizationPropertyGroup
	{
	public:
		explicit CustomizationPropertyGroup(const std::string& name) : m_name(name) {}
		virtual ~CustomizationPropertyGroup() = default;

	public:
		[[nodiscard]] virtual CharacterCustomizationPropertyType GetType() const = 0;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		void SetName(const std::string& newName) { m_name = newName; }

		virtual void Apply(CustomizationPropertyGroupApplier& applier, const AvatarConfiguration& configuration) = 0;

	protected:
		std::string m_name;
	};

	// One derived type
	class VisibilitySetPropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit VisibilitySetPropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name)
		{
		}

		[[nodiscard]] CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::VisibilitySet;
		}

		void Apply(CustomizationPropertyGroupApplier& applier, const AvatarConfiguration& configuration) override
		{
			applier.Apply(*this, configuration);
		}

	public:
		std::string subEntityTag;

		std::vector<VisibilitySetValue> possibleValues;
	};

	// Another derived type
	class MaterialOverridePropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit MaterialOverridePropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name)
		{
		}

		[[nodiscard]] CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::MaterialOverride;
		}

		void Apply(CustomizationPropertyGroupApplier& applier, const AvatarConfiguration& configuration) override
		{
			applier.Apply(*this, configuration);
		}

	public:
		std::vector<MaterialOverrideValue> possibleValues;
	};

	// Another derived type
	class ScalarParameterPropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit ScalarParameterPropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name), minValue(0.0f), maxValue(1.0f)
		{
		}

		virtual CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::ScalarParameter;
		}

		void Apply(CustomizationPropertyGroupApplier& applier, const AvatarConfiguration& configuration) override
		{
			applier.Apply(*this, configuration);
		}

	public:
		float minValue;
		float maxValue;
	};

	/// Definition of a customizable avatar.
	class CustomizableAvatarDefinition final
		: public ChunkReader
	{
	public:
		typedef std::vector<std::unique_ptr<CustomizationPropertyGroup>> Properties;
		typedef Properties::iterator PropertyIterator;
		typedef Properties::const_iterator ConstPropertyIterator;

	public:
		explicit CustomizableAvatarDefinition(String baseMesh);

		explicit CustomizableAvatarDefinition();

		~CustomizableAvatarDefinition() override = default;

	public:
		/// Apply all properties to the visitor.
		void AddProperty(std::unique_ptr<CustomizationPropertyGroup> property);

		void RemovePropertyByIndex(const size_t index)
		{
			auto it = m_properties.begin();
			std::advance(it, index);

			m_properties.erase(it);
		}

		void Serialize(io::Writer& writer) const;

	protected:
		bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadAvatarDefinitionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadPropertyGroupChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	public:
		[[nodiscard]] ConstPropertyIterator begin() const
		{
			return m_properties.begin();
		}

		[[nodiscard]] ConstPropertyIterator end() const
		{
			return m_properties.end();
		}

		[[nodiscard]] PropertyIterator begin()
		{
			return m_properties.begin();
		}

		[[nodiscard]] PropertyIterator end()
		{
			return m_properties.end();
		}

		[[nodiscard]] const String& GetBaseMesh() const
		{
			return m_baseMesh;
		}

		void SetBaseMesh(String baseMesh)
		{
			m_baseMesh = std::move(baseMesh);
		}

	private:
		String m_baseMesh;

		Properties m_properties;
	};

	class AvatarConfiguration
	{
	public:
		void Apply(CustomizationPropertyGroupApplier& applier, const CustomizableAvatarDefinition& definition)
		{
			for (const auto& group : definition)
			{
				// If it's a "scalar" group, read config.scalarValues[group.groupName]
				// If it's a "visibility" or "material" group, we look up which valueId 
				// is chosen in config.chosenOptionPerGroup[group.groupName]
				// Then find that value in group.possibleValues, 
				// apply it via your visitor or switch logic.

				group->Apply(applier, *this);
			}
		}


	public:
		// For each property group that is a "dropdown" (VisibilitySet or MaterialOverride),
		// store the chosen "valueId"
		std::unordered_map<std::string, std::string> chosenOptionPerGroup;

		// For each property group that is a scalar (like Height),
		// store the chosen float 
		std::unordered_map<std::string, float> scalarValues;
	};
}
