#pragma once

#include "character_customization_property.h"

#include <memory>
#include <vector>

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/macros.h"

namespace mmo
{
	class CustomizationPropertyValue
	{
	public:
		explicit CustomizationPropertyValue(String valueId)
			: m_valueId(std::move(valueId))
		{
		}
		virtual ~CustomizationPropertyValue() = default;

	public:
		[[nodiscard]] const String& GetValueId() const
		{
			return m_valueId;
		}

		[[nodiscard]] CharacterCustomizationProperty& GetPropertyData() const
		{
			ASSERT(m_propertyData);
			return *m_propertyData;
		}

	protected:
		String m_valueId;

		std::unique_ptr<CharacterCustomizationProperty> m_propertyData;
	};

	class CustomizationPropertyGroup
	{
	protected:
		String m_name;

		CharacterCustomizationPropertyType m_type;

		std::vector<CustomizationPropertyValue> m_values;
	};

	/// Definition of a customizable avatar.
	class CustomizableAvatarDefinition final
		: public ChunkReader
	{
	public:
		typedef std::vector<std::unique_ptr<CharacterCustomizationProperty>> Properties;
		typedef Properties::iterator PropertyIterator;
		typedef Properties::const_iterator ConstPropertyIterator;

	public:
		explicit CustomizableAvatarDefinition(String baseMesh);

		~CustomizableAvatarDefinition() override = default;

	public:
		/// Apply all properties to the visitor.
		void Apply(CharacterCustomizationPropertyVisitor& visitor) const;

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

	protected:
		bool OnReadFinished() noexcept override;

	private:
		String m_baseMesh;

		std::vector<std::unique_ptr<CharacterCustomizationProperty>> m_properties;
	};
}
