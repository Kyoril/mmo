// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "assets/asset_registry.h"

#include "base/macros.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"
#include "frame_ui/frame_texture.h"
#include "frame_ui/frame_font_string.h"
#include "frame_ui/frame_mgr.h"
#include "graphics/texture_mgr.h"

#include "asio.hpp"
#include "expat/lib/expat.h"

#include <set>


namespace mmo
{
	const std::string LoginState::Name = "login";

	// Forward declaration for detail methods
	void LoadUIFile(const std::string& filename);
	
	namespace detail
	{
		// TODO: Implement the s_tocFiles check in the AssetRegistry system somehow?
		// This could be useful to prevent loading files twice which seems like a common
		// thing to do for file loading.

		/// A list of files that have been loaded.
		static std::set<std::string> s_tocFiles;

		/// Subroutine for loading a *.lua file for the frame ui.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadFrameScript(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// TODO: Load (and execute) lua script
		}

		static void StartElement(void *userData, const XML_Char *name, const XML_Char **atts)
		{
			int32* depth = reinterpret_cast<int32*>(userData);

			std::ostringstream strm;
			for (int32 i = 0; i < *depth; ++i)
				strm << "\t";

			strm << "Start: " << name;

			if (atts && *atts)
			{
				strm << "; Attributes: ";
				while (*atts)
				{
					strm << *atts << " = ";
					atts++;
					
					if (!*atts)
						break;

					strm << *atts << ";";
					atts++;
				}
			}

			DLOG("\t" << strm.str());
			(*depth)++;
		}

		static void EndElement(void *userData, const XML_Char *name)
		{
			int32* depth = reinterpret_cast<int32*>(userData);
			(*depth)--;
		}

		static void CharacterHandler(void *userData, const XML_Char *s, int len)
		{
			if (len > 0)
			{
				std::string strContent{ s, (const size_t)len };

				// TODO
			}
		}

		/// Subroutine for loading a *.xml file for the frame ui. An xml file describes frames to
		/// be created, but can also reference script files directly.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadFrameXML(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// Load file content
			file->seekg(0, std::ios::end);
			const auto fileSize = file->tellg();
			file->seekg(0, std::ios::beg);

			// Allocate buffer
			std::vector<char> buffer;
			buffer.resize(fileSize);

			// Read file content
			file->read(&buffer[0], buffer.size());

			// Create the xml parser
			XML_Parser parser = XML_ParserCreate(nullptr);

			// A depth counter to account the xml layer depth
			int32 depth = 0;
			XML_SetUserData(parser, &depth);

			// Set the element handler
			XML_SetElementHandler(parser, StartElement, EndElement);
			XML_SetCharacterDataHandler(parser, CharacterHandler);

			// Parse the file contents
			if (XML_Parse(parser, &buffer[0], buffer.size(), XML_TRUE) == XML_STATUS_ERROR)
			{
				ELOG("Xml Error: " << XML_ErrorString(XML_GetErrorCode(parser)));
				return;
			}

			// TODO...
		}

		/// Subroutine for loading a *.toc file for the frame ui. A toc file is a text file which
		/// contains file names (one per line), which are then loaded.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadTOCFile(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// Read each line of the file
			std::string line;
			while (std::getline(*file, line))
			{
				// Check for empty lines or comments
				if (line.empty())
					continue;

				// Remove all occurences of \r character as getline only accounts for \n and on
				// windows, lines might end with \r\n
				size_t index = 0;
				while ((index = line.find('\r', index)) != line.npos)
				{
					line.erase(index, 1);
				}

				// Load the ui file
				LoadUIFile(line);
			}
		}

		/// This method checks if the given file name is already flagged as being loaded.
		/// If it isn't loaded, it also flags this filename.
		/// @return true if the file wasn't flagged, so loading can proceed.
		static bool LoadCycleCheck(const std::string& filename)
		{
			// Prevent a load cycle and double load
			if (s_tocFiles.find(filename) != s_tocFiles.end())
				return false;

			// Add to the set
			s_tocFiles.insert(filename);
			return true;
		}
	}
	
	/// Loads a UI file, which can be one of: *.toc, *.xml or *.lua. The respective file is
	/// then handled properly. The file is loaded using the AssetRegistry system.
	/// @param filename Name of the file to load.
	void LoadUIFile(const std::string& filename)
	{
		if (!detail::LoadCycleCheck(filename))
			return;

		// Extract the file extension
		std::string extension = GetFileExtension(filename);
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		// Try to open the file
		auto file = AssetRegistry::OpenFile(filename);
		ASSERT(file);

		// Call the respective subroutine depending on the file extension
		if (extension == ".toc")
		{
			detail::LoadTOCFile(std::move(file), filename);
		}
		else if (extension == ".lua")
		{
			detail::LoadFrameScript(std::move(file), filename);
		}
		else if (extension == ".xml")
		{
			detail::LoadFrameXML(std::move(file), filename);
		}
	}


	void LoginState::OnEnter()
	{
		FrameManager::

		// Make the logo frame element
		m_topFrame = std::make_shared<Frame>("TopFrame");

		// Clear all loaded toc files
		detail::s_tocFiles.clear();

		// Load the root toc file
		LoadUIFile("Interface/GlueUI/GlueUI.toc");

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void LoginState::OnLeave()
	{
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// Reset the logo frame ui
		m_topFrame.reset();

		// Reset texture
		m_texture.reset();
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}

	void LoginState::OnPaint()
	{
		// Render the logo frame ui
		m_topFrame->Render();
	}
}
