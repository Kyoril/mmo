#pragma once

#include "base/typedefs.h"

#include "luabind/luabind.hpp"
#include "xml_handler/xml_handler.h"

namespace mmo
{
	class IInputControl;

	struct Binding
	{
		String name;
		String description;
		String category;
		luabind::object script;
	};

	enum class BindingKeyState
	{
		Down,
		Up
	};

	class Bindings final
	{
	public:
		~Bindings();

	public:
		void Initialize(IInputControl& inputControl);

		void Shutdown();

		void Load(const String& bindingsFile);

		void Unload();

		void Bind(const String& keyName, const String& command);

		bool ExecuteKey(const String& keyName, BindingKeyState keyState);

		bool HasBinding(const String& name) const;

		const Binding& GetBinding(const String& name) const;

		void AddBinding(const Binding& binding);

		void RemoveBinding(const String& name);

	private:
		std::map<String, Binding> m_bindings;
		std::map<String, String> m_inputActionBindings;
		IInputControl* m_inputControl{ nullptr };
	};

	class BindingXmlLoader final : public XmlHandler
	{
	public:
		explicit BindingXmlLoader(Bindings& bindings)
			: m_bindings(bindings)
		{
		}
		~BindingXmlLoader() noexcept override = default;

	public:
		void ElementStart(const std::string& element, const XmlAttributes& attributes) override;

		void ElementEnd(const std::string& element) override;

		void Text(const std::string& text) override;

	private:
		void ElementBindingsStart(const XmlAttributes& attributes);
		void ElementBindingsEnd();
		void ElementBindingStart(const XmlAttributes& attributes);
		void ElementBindingEnd();

	private:
		Bindings& m_bindings;

		bool m_hasRootElement{ false };

		std::unique_ptr<Binding> m_currentBinding;

		String m_bindingScript;
	};
}
