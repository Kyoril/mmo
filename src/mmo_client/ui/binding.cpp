
#include "binding.h"

#include "assets/asset_registry.h"
#include "expat/lib/expat.h"
#include "log/default_log_levels.h"

namespace mmo
{
	/// Executed when an element is started.
	static void StartElement(void* userData, const XML_Char* name, const XML_Char** atts)
	{
		Bindings* bindings = static_cast<Bindings*>(userData);

		// Is this a binding element
		if (strcmp(name, "Binding") != 0)
		{
			ELOG("Unsupported xml element '" << name << "' found!");
			return;
		}


	}

	/// Executed when an event ended.
	static void EndElement(void* userData, const XML_Char* name)
	{
		Bindings* bindings = static_cast<Bindings*>(userData);

	}

	/// Executed whenever there is text. Concurrent text data should be concatenated
	/// if there is no other element in between.
	static void CharacterHandler(void* userData, const XML_Char* s, int len)
	{
		Bindings* bindings = static_cast<Bindings*>(userData);
		if (len > 0)
		{
			const std::string strContent{ s, static_cast<const size_t>(len) };

		}
	}

	void Bindings::Load(const String& bindingsFile)
	{
		// Load the bindings file
		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(bindingsFile);
		if (!file)
		{
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

		// A depth counter to account the xml layer depth
		int32 depth = 0;
		XML_SetUserData(parser, this);

		// Set the element handler
		XML_SetElementHandler(parser, StartElement, EndElement);
		XML_SetCharacterDataHandler(parser, CharacterHandler);

		// Parse the file contents
		if (XML_Parse(parser, buffer.data(), buffer.size(), XML_TRUE) == XML_STATUS_ERROR)
		{
			XML_ParserFree(parser);
			ELOG("Xml Error: " << XML_ErrorString(XML_GetErrorCode(parser)) << " - File '" << bindingsFile << "', Line " << XML_GetErrorLineNumber(parser));
			return;
		}

		XML_ParserFree(parser);
	}
}
