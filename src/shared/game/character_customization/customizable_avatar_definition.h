#pragma once

#include "character_customization_property.h"

#include <memory>
#include <vector>

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/macros.h"

namespace mmo
{
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

	class CustomizationPropertyGroup
	{
	public:
		CustomizationPropertyGroup(const std::string& name) : m_name(name) {}
		virtual ~CustomizationPropertyGroup() = default;

		virtual CharacterCustomizationPropertyType GetType() const = 0;

		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& newName) { m_name = newName; }

	protected:
		std::string m_name;
	};

	// One derived type
	class VisibilitySetPropertyGroup : public CustomizationPropertyGroup
	{
	public:
		VisibilitySetPropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name)
		{
		}

		virtual CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::VisibilitySet;
		}

		// Example data: A “tag” plus a list of possible sub-entity sets
		std::string subEntityTag;

		// The list of discrete values
		std::vector<VisibilitySetValue> possibleValues;
	};

	// Another derived type
	class MaterialOverridePropertyGroup : public CustomizationPropertyGroup
	{
	public:
		MaterialOverridePropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name)
		{
		}

		virtual CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::MaterialOverride;
		}

		std::vector<MaterialOverrideValue> possibleValues;
	};

	// Another derived type
	class ScalarParameterPropertyGroup : public CustomizationPropertyGroup
	{
	public:
		ScalarParameterPropertyGroup(const std::string& name)
			: CustomizationPropertyGroup(name), minValue(0.0f), maxValue(1.0f) {
		}

		virtual CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::ScalarParameter;
		}

		float minValue;
		float maxValue;
		// Possibly discrete or continuous...
		// ...
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

		explicit CustomizableAvatarDefinition() = default;

		~CustomizableAvatarDefinition() override = default;

	public:
		/// Apply all properties to the visitor.
		void Apply(CharacterCustomizationPropertyVisitor& visitor) const;

		void AddProperty(std::unique_ptr<CustomizationPropertyGroup> property);

		void RemovePropertyByIndex(const size_t index)
		{
			auto it = m_properties.begin();
			std::advance(it, index);

			m_properties.erase(it);
		}

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

	protected:
		bool OnReadFinished() noexcept override;

	private:
		String m_baseMesh;

		Properties m_properties;
	};
}
