// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include "luabind/luabind.hpp"
#include "xml_handler/xml_handler.h"

#include <functional>
#include <vector>

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
		Repeat,
		Up
	};

	class Bindings final
	{
	public:
		using KeyCaptureCallback = std::function<void(const String&)>;

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

		/// Returns the map of all registered bindable actions.
		const std::map<String, Binding>& GetAllBindings() const { return m_bindings; }

		/// Returns the map of key name → action name for all active key assignments.
		const std::map<String, String>& GetAllKeyBindings() const { return m_inputActionBindings; }

		/// Returns all key names currently assigned to the given action.
		std::vector<String> GetKeysForAction(const String& actionName) const;

		/// Removes the binding for the given key (if any).
		void UnbindKey(const String& keyName);

		/// Writes all current key bindings to Config/Bindings.cfg.
		void SaveBindings();

		/// Enters key-capture mode. The next key or mouse-button event calls callback(keyName).
		void StartKeyCapture(KeyCaptureCallback callback);

		/// Cancels key-capture mode without firing the callback.
		void StopKeyCapture();

		/// Returns true while waiting for a captured key.
		bool IsCapturing() const { return m_capturePending; }

		/// Returns the global Bindings instance (set during Initialize / cleared during Shutdown).
		static Bindings* GetCurrent() { return s_instance; }

	private:
		static Bindings* s_instance;

	private:
		std::map<String, Binding> m_bindings;
		std::map<String, String> m_inputActionBindings;
		IInputControl* m_inputControl{ nullptr };

		bool m_capturePending{ false };
		KeyCaptureCallback m_captureCallback;
	};

	class BindingXmlLoader final : public XmlHandler
	{
	public:
		explicit BindingXmlLoader(Bindings& bindings)
			: m_bindings(bindings)
		{
		}
		~BindingXmlLoader() override = default;

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
