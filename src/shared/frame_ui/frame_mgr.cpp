// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_mgr.h"
#include "frame_layer.h"
#include "style_xml_loader.h"
#include "layout_xml_loader.h"

#include "base/macros.h"
#include "log/default_log_levels.h"
#include "assets/asset_registry.h"
#include "xml_handler/xml_attributes.h"

#include <set>
#include <string>

#include "expat/lib/expat.h"


namespace mmo
{
	static FrameManager &s_frameMgr = FrameManager::Get();

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


		/// The current xml loader.
		static XmlHandler* s_currentXmlLoader = nullptr;
		/// The current xml indent.
		static int32 s_currentXmlIndent;
		/// A xml handler for loading frame layouts using xml.
		static LayoutXmlLoader s_layoutXmlLoader;
		/// An xml loader for style xml files.
		static StyleXmlLoader s_styleXmlLoader;



		/// Executed when an element is started.
		static void StartElement(void *userData, const XML_Char *name, const XML_Char **atts)
		{
			// If we don't have a current xml loader
			if (s_currentXmlIndent == 0)
			{
				// No current loader, check for tag names
				if (std::string(name) == "UiLayout")
				{
					// Use the layout xml loader from here on
					s_currentXmlLoader = &s_layoutXmlLoader;
				}
				else if(std::string(name) == "UiStyle")
				{
					s_currentXmlLoader = &s_styleXmlLoader;
				}
			}
			else if(s_currentXmlLoader != nullptr)
			{
				// Parse attribute map
				XmlAttributes attributeMap;
				while (atts && *atts)
				{
					const std::string key{ *(atts++) };
					if (!*atts) break;

					const std::string value{ *(atts++) };
					attributeMap.Add(key, value);
				}

				// Call StartElement on the current xml loader
				s_currentXmlLoader->ElementStart(name, attributeMap);
			}

			// Increase the indent counter
			s_currentXmlIndent++;
		}

		/// Executed when an event ended.
		static void EndElement(void *userData, const XML_Char *name)
		{
			// Reduce the xml indent counter
			s_currentXmlIndent--;

			// Reset current loader if we reached the root element
			if (s_currentXmlIndent == 0)
			{
				s_currentXmlLoader = nullptr;
			}
			else if(s_currentXmlLoader != nullptr)
			{
				// Route to current loader
				s_currentXmlLoader->ElementEnd(name);
			}
		}

		/// Executed whenever there is text. Concurrent text data should be concatenated
		/// if there is no other element in between.
		static void CharacterHandler(void *userData, const XML_Char *s, int len)
		{
			if (len > 0)
			{
				const std::string strContent{ s, (const size_t)len };
				s_layoutXmlLoader.Text(strContent);
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
				// Remove all occurences of \r character as getline only accounts for \n and on
				// windows, lines might end with \r\n
				size_t index = 0;
				while ((index = line.find('\r', index)) != line.npos)
				{
					line.erase(index, 1);
				}

				// Check for empty lines or comments
				if (line.empty() || line[0] == '#')
					continue;


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

		// Extract the file extension in lower case letters
		std::string extension = GetFileExtension(filename);
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		// Try to open the file using the asset registry system
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


	FrameManager& FrameManager::Get()
	{
		static FrameManager s_frameMgr;
		return s_frameMgr;
	}

	void FrameManager::Initialize()
	{
		// Register frame factories
		FrameManager::Get().RegisterFrameFactory("frame", [](const std::string& name) -> FramePtr { return std::make_shared<Frame>(name); });
	}

	void FrameManager::Destroy()
	{
		// Unregister all frame factories
		FrameManager::Get().ClearFrameFactories();
	}

	void FrameManager::LoadUIFile(const std::string& filename)
	{
		// Clear all loaded toc files
		detail::s_tocFiles.clear();

		// Load UI file
		mmo::LoadUIFile(filename);
	}

	FramePtr FrameManager::Create(const std::string& type, const std::string & name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return nullptr;
		}

		// Retrieve the frame factory by type
		auto factoryIt = m_frameFactories.find(type);
		if (factoryIt == m_frameFactories.end())
		{
			ELOG("Can not create a frame of type " << type);
			return nullptr;
		}

		// Create the new frame using the frame factory
		auto newFrame = factoryIt->second(name);
		m_framesByName[name] = newFrame;

		return newFrame;
	}

	FramePtr FrameManager::CreateOrRetrieve(const std::string& type, const std::string& name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return it->second;
		}

		// Retrieve the frame factory by type
		auto factoryIt = m_frameFactories.find(type);
		if (factoryIt == m_frameFactories.end())
		{
			ELOG("Can not create a frame of type " << type);
			return nullptr;
		}

		auto newFrame = factoryIt->second(name);
		m_framesByName[name] = newFrame;

		return newFrame;
	}

	FramePtr FrameManager::Find(const std::string & name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return it->second;
		}

		return nullptr;
	}

	void FrameManager::SetTopFrame(const FramePtr& topFrame)
	{
		m_topFrame = topFrame;
	}

	void FrameManager::ResetTopFrame()
	{
		m_topFrame.reset();
	}

	void FrameManager::Draw() const
	{
		// Render top frame if there is any
		if (m_topFrame != nullptr)
		{
			m_topFrame->Render();
		}
	}

	void FrameManager::RegisterFrameFactory(const std::string & elementName, FrameFactory factory)
	{
		auto it = m_frameFactories.find(elementName);
		FATAL(it == m_frameFactories.end(), "Frame factory already registered!");

		// Register factory
		m_frameFactories[elementName] = factory;
	}

	void FrameManager::UnregisterFrameFactory(const std::string & elementName)
	{
		auto it = m_frameFactories.find(elementName);
		if (it != m_frameFactories.end())
		{
			m_frameFactories.erase(it);
		}
	}

	void FrameManager::ClearFrameFactories()
	{
		m_frameFactories.clear();
	}


}