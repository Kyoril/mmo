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

		/// Registers a binding action at runtime (from a UI module) instead of Bindings.xml.
		///
		/// UI modules load before the bindings do, so registrations are kept and applied when the
		/// bindings are (re)loaded, and immediately if they already are. Registering an existing
		/// name replaces its script, which keeps UI reloads working. The default key is bound only
		/// if the action has no key yet and the key is free, and it is not written to Bindings.cfg
		/// unless the player changes it.
		/// @param binding The action (name, description, category, script).
		/// @param defaultKey Key to bind by default, or empty for none.
		static void RegisterRuntimeBinding(const Binding& binding, const String& defaultKey);

		/// Forgets every runtime binding registration (they hold script references).
		static void ClearRuntimeBindings();

	private:
		static Bindings* s_instance;

		struct RuntimeBinding
		{
			Binding binding;
			String defaultKey;
		};

		static std::map<String, RuntimeBinding> s_runtimeBindings;

		/// Adds or replaces a runtime binding on this instance; with applyDefault, binds its default key.
		void ApplyRuntimeBinding(const RuntimeBinding& runtime, bool applyDefault);

	private:
		std::map<String, Binding> m_bindings;
		std::map<String, String> m_inputActionBindings;
		/// Keys bound only because they are a runtime binding's default (key -> action); not saved.
		std::map<String, String> m_runtimeDefaultKeys;
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
