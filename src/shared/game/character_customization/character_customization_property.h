#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class VisibilitySetCustomizationProperty;
	class MaterialOverrideCustomizationProperty;
	class ScalarParameterCustomizationProperty;

	/// Visitor interface for character customization properties.
	class CharacterCustomizationPropertyVisitor
	{
	public:
		virtual ~CharacterCustomizationPropertyVisitor() = default;

	public:
		virtual void AcceptProperty(VisibilitySetCustomizationProperty& property) = 0;

		virtual void AcceptProperty(MaterialOverrideCustomizationProperty& property) = 0;

		virtual void AcceptProperty(ScalarParameterCustomizationProperty& property) = 0;
	};

	/// Enumerates possible character customization property types.
	enum class CharacterCustomizationPropertyType : uint8
	{
		MaterialOverride,

		VisibilitySet,

		ScalarParameter,
	};

	/// Base class for character customization properties.
	class CharacterCustomizationProperty
	{
	public:
		explicit CharacterCustomizationProperty(String name)
			: m_name(std::move(name))
		{
		}

		virtual ~CharacterCustomizationProperty() = default;

	public:
		[[nodiscard]] virtual CharacterCustomizationPropertyType GetType() const = 0;

		virtual void Accept(CharacterCustomizationPropertyVisitor& visitor) = 0;

		[[nodiscard]] const String& GetName() const
		{
			return m_name;
		}

	protected:
		String m_name;
	};

	class VisibilitySetCustomizationProperty final : public CharacterCustomizationProperty
	{
	public:
		explicit VisibilitySetCustomizationProperty(String name)
			: CharacterCustomizationProperty(std::move(name))
		{
		}

		~VisibilitySetCustomizationProperty() override = default;

	public:
		[[nodiscard]] CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::VisibilitySet;
		}

		void Accept(CharacterCustomizationPropertyVisitor& visitor) override
		{
			visitor.AcceptProperty(*this);
		}
	};

	class MaterialOverrideCustomizationProperty final : public CharacterCustomizationProperty
	{
	public:
		explicit MaterialOverrideCustomizationProperty(String name)
			: CharacterCustomizationProperty(std::move(name))
		{
		}

		~MaterialOverrideCustomizationProperty() override = default;

	public:
		[[nodiscard]] CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::MaterialOverride;
		}

		void Accept(CharacterCustomizationPropertyVisitor& visitor) override
		{
			visitor.AcceptProperty(*this);
		}
	};

	class ScalarParameterCustomizationProperty final : public CharacterCustomizationProperty
	{
	public:
		explicit ScalarParameterCustomizationProperty(String name)
			: CharacterCustomizationProperty(std::move(name))
		{
		}

		~ScalarParameterCustomizationProperty() override = default;

	public:
		[[nodiscard]] CharacterCustomizationPropertyType GetType() const override
		{
			return CharacterCustomizationPropertyType::MaterialOverride;
		}

		void Accept(CharacterCustomizationPropertyVisitor& visitor) override
		{
			visitor.AcceptProperty(*this);
		}
	};
}
