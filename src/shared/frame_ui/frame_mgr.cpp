#include "frame_mgr.h"
#include "frame_font_string.h"
#include "frame_texture.h"
#include "frame_layer.h"

#include "base/macros.h"
#include "log/default_log_levels.h"
#include "assets/asset_registry.h"

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

		/// Keeps track if there is a UI element that has been started.
		static bool s_uiStarted = false;

		static bool s_layersStarted = false;

		static std::shared_ptr<Frame> s_currentParseFrame;

		static FrameLayer* s_currentLayer = nullptr;

		/// Executed when an element is started.
		static void StartElement(void *userData, const XML_Char *name, const XML_Char **atts)
		{
			// Check for a starting Ui element
			if (!s_uiStarted)
			{
				s_uiStarted = (strcmp(name, "Ui") == 0);
				if (!s_uiStarted)
				{
					ELOG("The first tag always has to be a Ui tag, but " << name << " was found!");
				}

				return;
			}

			// Parse attribute map
			std::map<std::string, std::string> attributeMap;
			while (atts && *atts)
			{
				std::string key{ *(atts++) };
				if (!*atts) break;

				std::string value{ *(atts++) };
				attributeMap[key] = value;
			}

			// Check for Frame
			if (strcmp(name, "Frame") == 0)
			{
				// Parse supported elements
				std::string frameName;

				// Look for the frame name
				auto nameIt = attributeMap.find("name");
				if (nameIt != attributeMap.end())
				{
					frameName = nameIt->second;
				}

				// The parent frame that this frame will belong to
				FramePtr parentFrame;

				// Look for the parent frame
				auto parentIt = attributeMap.find("parent");
				if (parentIt != attributeMap.end())
				{
					// Try to find the parent frame by name
					parentFrame = s_frameMgr.Find(parentIt->second);
					if (parentFrame == nullptr)
					{
						WLOG("Parent frame " << parentIt->second << " could not be found! Using default top frame as the parent frame.");
					}
				}

				// If there is no parent frame, use the top frame as parent
				if (parentFrame == nullptr)
				{
					parentFrame = s_frameMgr.GetTopFrame();
				}

				// Create the new frame element
				s_currentParseFrame = s_frameMgr.Create(frameName);
				if (!s_currentParseFrame)
				{
					ELOG("Failed to create new frame - name was probably already taken!");
				}

				if (parentFrame != nullptr)
				{
					parentFrame->AddChild(s_currentParseFrame);
				}
			}
			else if (strcmp(name, "Layers") == 0)
			{
				s_layersStarted = true;
			}
			else if (strcmp(name, "Layer") == 0)
			{
				if (!s_layersStarted)
					return;

				if (s_currentParseFrame == nullptr || s_currentLayer != nullptr)
					return;

				s_currentLayer = &s_currentParseFrame->AddLayer();
			}
			else if (strcmp(name, "FontString") == 0)
			{
				if (s_currentLayer == nullptr)
					return;

				std::string fontName;
				size_t fontSize = 8, outline = 0;
				std::string text;

				auto fontIt = attributeMap.find("font");
				if (fontIt != attributeMap.end())
				{
					fontName = fontIt->second;
				}

				auto textIt = attributeMap.find("text");
				if (textIt != attributeMap.end())
				{
					text = textIt->second;
				}

				auto sizeIt = attributeMap.find("size");
				if (sizeIt != attributeMap.end())
				{
					fontSize = std::atoi(sizeIt->second.c_str());
				}

				auto outlineIt = attributeMap.find("outline");
				if (outlineIt != attributeMap.end())
				{
					outline = std::atoi(outlineIt->second.c_str());
				}

				auto fontString = std::make_unique<FrameFontString>(fontName, fontSize, outline);
				fontString->SetText(text);

				s_currentLayer->AddObject(std::move(fontString));
			}
			else if (strcmp(name, "Texture") == 0)
			{
				if (s_currentLayer == nullptr)
					return;

				std::string textureFile;

				auto fileIt = attributeMap.find("file");
				if (fileIt != attributeMap.end())
				{
					textureFile = fileIt->second;
				}

				auto texture = std::make_unique<FrameTexture>(textureFile);
				s_currentLayer->AddObject(std::move(texture));
			}
		}

		/// Executed when an event ended.
		static void EndElement(void *userData, const XML_Char *name)
		{
			// Check for ending Ui element
			if (strcmp(name, "Ui") == 0)
			{
				s_uiStarted = false;
			}
			else if (strcmp(name, "Frame") == 0)
			{
				s_currentParseFrame.reset();
			}
			else if (strcmp(name, "Layers") == 0)
			{
				s_layersStarted = false;
			}
			else if (strcmp(name, "Layer") == 0)
			{
				s_currentLayer = nullptr;
			}
		}

		/// Executed whenever there is text. Concurrent text data should be concatenated
		/// if there is no other element in between.
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


	FrameManager& FrameManager::Get()
	{
		static FrameManager s_frameMgr;
		return s_frameMgr;
	}

	void FrameManager::LoadUIFile(const std::string& filename)
	{
		// Clear all loaded toc files
		detail::s_tocFiles.clear();

		// Load UI file
		mmo::LoadUIFile(filename);
	}

	FramePtr FrameManager::Create(const std::string & name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return nullptr;
		}

		auto newFrame = std::make_shared<Frame>(name);
		m_framesByName[name] = newFrame;

		return newFrame;
	}

	FramePtr FrameManager::CreateOrRetrieve(const std::string& name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return it->second;
		}

		auto newFrame = std::make_shared<Frame>(name);

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
		m_topFrame->Render();
	}

	void FrameManager::RegisterFrameFactory(const std::string & elementName, FrameFactory factory)
	{
		auto it = m_frameFactories.find(elementName);
		FATAL(it != m_frameFactories.end(), "Frame factory already registered!");

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