
#include "binding.h"

#include "assets/asset_registry.h"
#include "console/console.h"
#include "expat/lib/expat.h"
#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "xml_handler/xml_attributes.h"

namespace mmo
{
	namespace
	{
		/// Executed when an element is started.
		void StartElement(void* userData, const XML_Char* name, const XML_Char** attributes)
		{
			const auto loader = static_cast<BindingXmlLoader*>(userData);

			// Parse attribute map
			XmlAttributes attributeMap;
			while (attributes && *attributes)
			{
				const std::string key{ *(attributes++) };
				if (!*attributes) break;

				const std::string value{ *(attributes++) };
				attributeMap.Add(key, value);
			}

			loader->ElementStart(name, attributeMap);
		}

		void EndElement(void* userData, const XML_Char* name)
		{
			const auto loader = static_cast<BindingXmlLoader*>(userData);
			loader->ElementEnd(name);
		}

		void CharacterHandler(void* userData, const XML_Char* s, const int len)
		{
			const auto loader = static_cast<BindingXmlLoader*>(userData);
			loader->Text(std::string(s, len));
		}
	}

	Bindings::~Bindings()
	{
		Unload();
		Shutdown();
	}

	void Bindings::Initialize(IInputControl& inputControl)
	{
		m_inputControl = &inputControl;

		Console::RegisterCommand("bind", [this](const std::string& command, const std::string& args) {
			std::vector<std::string> arguments;
			TokenizeString(args, arguments);

			if (arguments.size() < 2)
			{
				ELOG("Invalid number of arguments provided! Usage: bind [key_name] [command]");
				return;
			}

			Bind(arguments[0], arguments[1]);
			}, ConsoleCommandCategory::Game, "Binds an input binding to a key.");
	}

	void Bindings::Shutdown()
	{
		Console::UnregisterCommand("bind");

		m_inputControl = nullptr;
	}

	void Bindings::Load(const String& bindingsFile)
	{
		// Load the bindings file
		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(bindingsFile);
		if (!file)
		{
			ELOG("Failed to open bindings file '" << bindingsFile << "'!");
			return;
		}

		// Load file content
		file->seekg(0, std::ios::end);
		const auto fileSize = file->tellg();
		file->seekg(0, std::ios::beg);

		// Allocate buffer
		std::vector<char> buffer;
		buffer.resize(fileSize);

		// Read file content
		file->read(buffer.data(), buffer.size());

		// Create the xml parser
		const XML_Parser parser = XML_ParserCreate(nullptr);

		// Create loader
		BindingXmlLoader loader(*this);
		XML_SetUserData(parser, &loader);

		// Set the element handler
		XML_SetElementHandler(parser, StartElement, EndElement);
		XML_SetCharacterDataHandler(parser, CharacterHandler);

		// Parse the file contents
		if (XML_Parse(parser, buffer.data(), buffer.size(), XML_TRUE) == XML_STATUS_ERROR)
		{
			ELOG("Xml Error: " << XML_ErrorString(XML_GetErrorCode(parser)) << " - File '" << bindingsFile << "', Line " << XML_GetErrorLineNumber(parser));
			XML_ParserFree(parser);
			return;
		}

		XML_ParserFree(parser);
	}

	void Bindings::Unload()
	{
		m_inputActionBindings.clear();
		m_bindings.clear();
	}

	void Bindings::Bind(const String& keyName, const String& command)
	{
		if (!HasBinding(command))
		{
			ELOG("Tried to bind non-existing binding '" << command << "'!");
			return;
		}

		// Warn if binding override
		if (m_inputActionBindings.find(keyName) != m_inputActionBindings.end())
		{
			WLOG("Key '" << keyName << "' was already bound to input action '" << m_inputActionBindings[keyName] << "', previous binding will be removed");
		}

		m_inputActionBindings[keyName] = command;
	}

	bool Bindings::ExecuteKey(const String& keyName, BindingKeyState keyState)
	{
		const auto it = m_inputActionBindings.find(keyName);
		if (it == m_inputActionBindings.end())
		{
			return false;
		}

		const auto bindingIt = m_bindings.find(it->second);
		if (bindingIt == m_bindings.end())
		{
			ELOG("Tried to execute non-existing binding '" << it->second << "'!");
			return false;
		}

		// Map key state to string for scripting
		String keyStateString = "NONE";
		switch (keyState)
		{
			case BindingKeyState::Down:
				keyStateString = "DOWN";
				break;
			case BindingKeyState::Up:
				keyStateString = "UP";
				break;
		}

		// Execute binding script
		if (!bindingIt->second.script.is_valid())
		{
			ELOG("Binding '" << bindingIt->second.name << "' has invalid script!");
			return false;
		}

		try
		{
			bindingIt->second.script(keyStateString);
		}
		catch(const luabind::error& e)
		{
			ELOG("Error executing binding '" << bindingIt->second.name << "': " << e.what());
			return false;
		}

		return true;
	}

	bool Bindings::HasBinding(const String& name) const
	{
		return m_bindings.find(name) != m_bindings.end();
	}

	const Binding& Bindings::GetBinding(const String& name) const
	{
		const auto it = m_bindings.find(name);
		if (it == m_bindings.end())
		{
			static const Binding emptyBinding;
			return emptyBinding;
		}

		return it->second;
	}

	void Bindings::AddBinding(const Binding& binding)
	{
		if (binding.name.empty())
		{
			ELOG("Tried to add binding without name to bindings!");
			return;
		}

		if (HasBinding(binding.name))
		{
			return;
		}

		// Copy binding object
		m_bindings[binding.name] = binding;
	}

	void Bindings::RemoveBinding(const String& name)
	{
		m_bindings.erase(name);
	}

	void BindingXmlLoader::ElementStart(const std::string& element, const XmlAttributes& attributes)
	{
		if (element == "Bindings")
		{
			ElementBindingsStart(attributes);
		}
		else if (element == "Binding")
		{
			ElementBindingStart(attributes);
		}
		else
		{
			ELOG("Unsupported xml element '" << element << "' found in bindings xml!");
		}
	}

	void BindingXmlLoader::ElementEnd(const std::string& element)
	{
		if (element == "Bindings")
		{
			ElementBindingsEnd();
		}
		else if (element == "Binding")
		{
			ElementBindingEnd();
		}
	}

	void BindingXmlLoader::Text(const std::string& text)
	{
		if (!m_currentBinding)
		{
			return;
		}

		// Append to binding script (in case script is read in multiple text chunks)
		m_bindingScript.append(text);
	}

	void BindingXmlLoader::ElementBindingsStart(const XmlAttributes& attributes)
	{
		if (m_hasRootElement)
		{
			ELOG("Bindings element is only allowed as root element, but we already have a root element in the bindings xml!");
			return;
		}

		m_hasRootElement = true;
	}

	void BindingXmlLoader::ElementBindingsEnd()
	{
		// Nothing to do here?
	}

	void BindingXmlLoader::ElementBindingStart(const XmlAttributes& attributes)
	{
		if (!m_hasRootElement)
		{
			ELOG("Binding element is only allowed inside the bindings element, but we don't have a bindings element in the bindings xml!");
			return;
		}

		if (m_currentBinding)
		{
			ELOG("Found nested binding element in bindings xml! Binding elements are only allowed in the root element!");
			return;
		}

		m_currentBinding = std::make_unique<Binding>();
		m_currentBinding->name = attributes.GetValueAsString("name");
		m_currentBinding->description = attributes.GetValueAsString("description");
		m_currentBinding->category = attributes.GetValueAsString("category", "Uncategorized");

		if (m_currentBinding->name.empty())
		{
			ELOG("Binding element without name attribute found in bindings xml!");
			m_currentBinding.reset();
			return;
		}
	}

	void BindingXmlLoader::ElementBindingEnd()
	{
		// No current binding element? Nothing to do
		if (!m_currentBinding)
		{
			return;
		}

		if (m_bindingScript.empty())
		{
			ELOG("Binding '" << m_currentBinding->name << "' without script found in bindings xml! Binding won't do anything");
			m_currentBinding.reset();
			return;
		}

		if (m_bindings.HasBinding(m_currentBinding->name))
		{
			ELOG("Binding '" << m_currentBinding->name << "' already exists in bindings! Only the first binding will be used!");
			m_currentBinding.reset();
			return;
		}

		// Setup script object for current binding
		m_currentBinding->script = FrameManager::Get().CompileFunction(m_currentBinding->name, m_bindingScript);
		m_bindings.AddBinding(*m_currentBinding);

		// Reset binding script
		m_bindingScript.clear();

		// Clear current binding
		m_currentBinding.reset();
	}
}
