#pragma once

#include "character_customization_property.h"

#include <memory>
#include <vector>

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/id_generator.h"
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
		uint32_t valueId;
		std::string valueName;                // e.g. "LongHair", "ShortHair", etc.
		std::vector<std::string> visibleSubEntities;
	};

	struct MaterialOverrideValue
	{
		uint32_t valueId;
		std::string valueName;

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
		explicit CustomizationPropertyGroup(uint32 id, const std::string& name) : m_id(id), m_name(name) {}
		virtual ~CustomizationPropertyGroup() = default;

	public:
		[[nodiscard]] virtual CharacterCustomizationPropertyType GetType() const = 0;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		void SetName(const std::string& newName) { m_name = newName; }

		virtual void Apply(CustomizationPropertyGroupApplier& applier, const AvatarConfiguration& configuration) = 0;

		[[nodiscard]] uint32 GetId() const { return m_id; }

		void SetId(const uint32 id) { m_id = id; }

	protected:
		uint32 m_id;

		std::string m_name;
	};

	// One derived type
	class VisibilitySetPropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit VisibilitySetPropertyGroup(uint32 id, const std::string& name)
			: CustomizationPropertyGroup(id, name)
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

		int32 GetPropertyValueIndex(const String& valueName) const
		{
			if (valueName.empty())
			{
				return -1;
			}

			for (size_t i = 0; i < possibleValues.size(); ++i)
			{
				if (possibleValues[i].valueName == valueName)
				{
					return static_cast<int32>(i);
				}
			}

			return -1;
		}

		int32 GetPropertyValueIndex(const uint32 valueId) const
		{
			for (size_t i = 0; i < possibleValues.size(); ++i)
			{
				if (possibleValues[i].valueId == valueId)
				{
					return static_cast<int32>(i);
				}
			}

			return -1;
		}

	public:
		std::string subEntityTag;

		std::vector<VisibilitySetValue> possibleValues;

		IdGenerator<uint32> idGenerator { 1 };
	};

	// Another derived type
	class MaterialOverridePropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit MaterialOverridePropertyGroup(uint32 id, const std::string& name)
			: CustomizationPropertyGroup(id, name)
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

		int32 GetPropertyValueIndex(const String& valueName) const
		{
			if (valueName.empty())
			{
				return possibleValues.empty() ? -1 : 0;
			}

			for (size_t i = 0; i < possibleValues.size(); ++i)
			{
				if (possibleValues[i].valueName == valueName)
				{
					return static_cast<int32>(i);
				}
			}

			return -1;
		}

		int32 GetPropertyValueIndex(const uint32 valueId) const
		{
			for (size_t i = 0; i < possibleValues.size(); ++i)
			{
				if (possibleValues[i].valueId == valueId)
				{
					return static_cast<int32>(i);
				}
			}

			return -1;
		}

	public:
		std::vector<MaterialOverrideValue> possibleValues;

		IdGenerator<uint32> idGenerator { 1 };
	};

	// Another derived type
	class ScalarParameterPropertyGroup final : public CustomizationPropertyGroup
	{
	public:
		explicit ScalarParameterPropertyGroup(uint32 id, const std::string& name)
			: CustomizationPropertyGroup(id, name), minValue(0.0f), maxValue(1.0f)
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

		CustomizationPropertyGroup* GetProperty(const std::string& name);

		uint32 GetNextPropertyId()
		{
			return m_propertyIdGenerator.GenerateId();
		}

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

		uint32 m_version;

		IdGenerator<uint32> m_propertyIdGenerator { 1 };
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
		std::unordered_map<std::string, uint32> chosenOptionPerGroup;

		// For each property group that is a scalar (like Height),
		// store the chosen float 
		std::unordered_map<std::string, float> scalarValues;
	};
}
