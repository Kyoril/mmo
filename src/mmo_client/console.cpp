// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "console.h"
#include "console_commands.h"
#include "console_var.h"
#include "event_loop.h"
#include "screen.h"

#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "base/assign_on_exit.h"
#include "assets/asset_registry.h"

#include <mutex>
#include <algorithm>


namespace mmo
{
	struct ConsoleLogEntry final
	{
		argb_t color;
		std::string message;
	};

	// Static private variables

	/// Map of case insensitive registered console commands.
	std::map<std::string, Console::ConsoleCommand, StrCaseIComp> Console::s_consoleCommands;
	/// Am event that listens for KeyDown events from the event loop to handle console input.
	static scoped_connection s_consoleKeyDownEvent;
	/// Whether the console window is currently visible on screen or not.
	static bool s_consoleVisible = false;
	/// The current height of the console window in pixels, counted from the top edge.
	static int32 s_consoleWindowHeight = 210;
	/// The last viewport width in pixels. Used to detect size changes.
	static int32 s_lastViewportWidth = 0;
	/// The last viewport height in pixels. Used to detect size changes.
	static int32 s_lastViewportHeight = 0;
	/// Scroll offset of the console text in lines.
	static int32 s_consoleScrollOffset = 0;
	/// Reference to the layer that has been registered for rendering the client console.
	static ScreenLayerIt s_consoleLayer;
	/// A vertex buffer that is used to render the console geometry (except the text).
	static VertexBufferPtr s_consoleVertBuf;
	/// Index buffer that is used to render the console vertex buffer geometry more efficiently.
	static IndexBufferPtr s_consoleIndBuf;
	/// The font that is used to render the console text.
	static FontPtr s_consoleFont;
	/// A geometry buffer which is populated by the console font's draw commands and then used to
	/// actually draw the console text on screen.
	static std::unique_ptr<GeometryBuffer> s_consoleTextGeom;
	/// A flag that indicates that the console text needs to be redrawn. Set on scrolling and text
	/// change events currently.
	static bool s_consoleTextDirty = true;
	/// A list of all console text lines. The list contents are clamped.
	static std::list<ConsoleLogEntry> s_consoleLog;
	/// A mutex to support adding console text lines in a multi threaded environment.
	static std::mutex s_consoleLogMutex;
	/// A connection that binds a function which displays the content of the LOG macros in the console.
	static scoped_connection s_consoleLogConn;


	// Graphics CVar stuff
	namespace
	{
		ConsoleVar* s_gxResolutionCVar = nullptr;
		ConsoleVar* s_gxWindowedCVar = nullptr;
		ConsoleVar* s_gxVSyncCVar = nullptr;
		ConsoleVar* s_gxApiCVar = nullptr;


		/// Helper struct for automatic gx cvar table.
		struct GxCVarHelper
		{
			std::string name;
			std::string description;
			std::string defaultValue;
			ConsoleVar** outputVar = nullptr;
			std::function<ConsoleVar::ChangedSignal::signature_type> changedHandler = nullptr;
		};

		/// A list of automatically registered and unregistered console variables that are also
		/// serialized when the games config file is serialized.
		const std::vector<GxCVarHelper> s_gxCVars = 
		{
			{"gxApi",			"Which graphics api should be used.",					"",			&s_gxApiCVar,			nullptr },
			{"gxResolution",	"The resolution of the primary output window.",			"1280x720",	&s_gxResolutionCVar,	nullptr },
			{"gxWindow",		"Whether the application will run in windowed mode.",	"1",		&s_gxWindowedCVar,		nullptr },
			{"gxVSync",			"Whether the application will run with vsync enabled.",	"1",		&s_gxVSyncCVar,			nullptr },

			// TODO: Add more graphics cvars here that should be registered and unregistered automatically
			// as well as being serialized when saving the graphics settings of the game.
		};


		/// Triggered when a gxcvar is changed to invalidate the current graphics settings.
		static void GxCVarChanged(ConsoleVar& var, const std::string& oldValue)
		{
			// TODO: Set pending graphics changes so that they can be submitted all at once.
		}


		/// Registers the automatically managed gx cvars from the table above.
		void RegisterGraphicsCVars()
		{
			// Register console variables from the table
			std::for_each(s_gxCVars.cbegin(), s_gxCVars.cend(), [](const GxCVarHelper& x) {
				ConsoleVar* output = ConsoleVarMgr::RegisterConsoleVar(x.name, x.description, x.defaultValue);

				// Eventually assign the output variable if asked to do so.
				if (x.outputVar != nullptr)
				{
					*x.outputVar = output;
				}
			});
		}

		/// Unregisters the automatically managed gx cvars from the table above.
		void UnregisterGraphicsCVars()
		{
			std::for_each(s_gxCVars.cbegin(), s_gxCVars.cend(), [](const GxCVarHelper& x) {
				ConsoleVarMgr::UnregisterConsoleVar(x.name);
			});
		}
	}


	// Console helpers
	namespace
	{
		/// Changes the console scrolling value by an amount and ensures that the scroll index
		/// stays in the expected range so that no overflow / underflow occurrs.
		inline void EnsureConsoleScrolling(int32 Amount)
		{
			// Ensure that s_consoleTextDirty is set to true at the end of the function,
			// no matter how we exit it.
			AssignOnExit<bool> invalidateGraphics{ s_consoleTextDirty, true };

			// Increase the scrolling amount
			s_consoleScrollOffset += Amount;

			// Always enforce positive scroll offset value
			s_consoleScrollOffset = std::max(0, s_consoleScrollOffset);

			// Max visible console lines 
			const int32 maxVisibleEntries = s_consoleWindowHeight / s_consoleFont->GetHeight();

			// Ensure there are enough entries so that scrolling is needed
			if (s_consoleLog.size() < maxVisibleEntries)
			{
				// Without scrolling, there is no need to change the scroll offset
				s_consoleScrollOffset = 0;
				return;
			}

			// Enforce max scroll value
			s_consoleScrollOffset = std::min(s_consoleScrollOffset, 
				static_cast<int32>(s_consoleLog.size()) - maxVisibleEntries);
		}

		void ExtractResolution(const std::string& resolutionString, uint16& out_width, uint16& out_height)
		{
			const auto delimiter = resolutionString.find('x');
			if (delimiter == std::string::npos)
			{
				return;
			}

			// Split string and handle both parts separately
			out_width = static_cast<uint16>(std::atoi(resolutionString.substr(0, delimiter).c_str()));
			out_height = static_cast<uint16>(std::atoi(resolutionString.substr(delimiter + 1).c_str()));
		}
	}


	// Console implementation

	void Console::Initialize(const std::filesystem::path& configFile)
	{
		// Ensure the folder is created
		std::filesystem::create_directories(configFile.parent_path());

		// Register some default console commands
		RegisterCommand("ver", console_commands::ConsoleCommand_Ver, ConsoleCommandCategory::Default, "Displays the client version.");
		RegisterCommand("run", console_commands::ConsoleCommand_Run, ConsoleCommandCategory::Default, "Runs a console script.");
		RegisterCommand("quit", console_commands::ConsoleCommand_Quit, ConsoleCommandCategory::Default, "Shutdown the game client immediately.");

		// Initialize the cvar manager
		ConsoleVarMgr::Initialize();

		// Register locale cvar
		auto* const localeCVar = ConsoleVarMgr::RegisterConsoleVar("locale", "The locale of the game client. Changing this requires a restart!", "enUS");

		// Register graphics variables
		RegisterGraphicsCVars();

		// Load the config file
		console_commands::ConsoleCommand_Run("run", configFile.string());

		// Console is hidden by default
		s_consoleVisible = false;
		s_consoleWindowHeight = 210;

		// Load the locale archive
		ILOG("Locale: " << localeCVar->GetStringValue());
		const auto localeArchive = "Locales/Locale_" + localeCVar->GetStringValue();

		// Initialize the asset registry
		AssetRegistry::Initialize(std::filesystem::current_path() / "Data", { "Interface.hpak", "Fonts.hpak", localeArchive, localeArchive + ".hpak" });

		// Set default graphics api
		const GraphicsApi defaultApi =
#if PLATFORM_WINDOWS
			GraphicsApi::D3D11;
#else
			GraphicsApi::OpenGL;
#endif

		// Check for console var values
		auto api = GraphicsApi::Unknown;
		if (_stricmp(s_gxApiCVar->GetStringValue().c_str(), "d3d11") == 0)
		{
			api = GraphicsApi::D3D11;
		}
		else if (_stricmp(s_gxApiCVar->GetStringValue().c_str(), "gl") == 0)
		{
			api = GraphicsApi::OpenGL;
		}

		// Use platform default api if unknown api was provided
		if (api == GraphicsApi::Unknown) api = defaultApi;

		// Extract the resolution
		GraphicsDeviceDesc desc;
		ExtractResolution(s_gxResolutionCVar->GetStringValue(), desc.width, desc.height);

		// Initialize the graphics api
		switch (api)
		{
#if PLATFORM_WINDOWS
		case GraphicsApi::D3D11:
			GraphicsDevice::CreateD3D11(desc);
			break;
#endif
		case GraphicsApi::OpenGL:
			// TODO: Create OpenGL graphics device here
		default:
			throw std::runtime_error("Unsupported graphics API value used!");
			break;
		}

		// Get the current graphics device object
		auto& device = GraphicsDevice::Get();
		device.GetAutoCreatedWindow()->SetTitle("MMORPG");

		// Terminate the event loop if the main window get's closed
		device.GetAutoCreatedWindow()->Closed.connect([]() {
			EventLoop::Terminate(0);
		});
		device.GetAutoCreatedWindow()->Resized.connect([](uint16 width, uint16 height) {
			auto topFrame = FrameManager::Get().GetTopFrame();
			if (topFrame)
			{
				topFrame->Invalidate();
				topFrame->InvalidateChildren();
			}
		});

		// Query the viewport size
		device.GetViewport(nullptr, nullptr, &s_lastViewportWidth, &s_lastViewportHeight, nullptr, nullptr);

		// Create the vertex buffer for the console background
		const POS_COL_VERTEX vertices[] = {
			{ { 0.0f, 0.0f, 0.0f }, 0xc0000000 },
			{ { static_cast<float>(s_lastViewportWidth), 0.0f, 0.0f }, 0xc0000000 },
			{ { static_cast<float>(s_lastViewportWidth), static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 },
			{ { 0.0f, static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 }
		};

		// Setup vertices
		s_consoleVertBuf = device.CreateVertexBuffer(4, sizeof(POS_COL_VERTEX), true, vertices);

		// Setup indices
		const uint16 indices[] = { 0, 1, 2, 2, 3, 0 };
		s_consoleIndBuf = device.CreateIndexBuffer(6, IndexBufferSize::Index_16, indices);

		// Load the console font
		s_consoleFont = FontManager::Get().CreateOrRetrieve("Fonts/consola.ttf", 12.0f, 0.0f);

		// Create a geometry buffer for the console output text
		s_consoleTextGeom = std::make_unique<GeometryBuffer>();
		s_consoleTextDirty = true;
		s_consoleLog.clear();

		// Initialize the screen system
		Screen::Initialize();

		// Assign console log signal
		s_consoleLogConn = mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry & entry) {
			std::scoped_lock lock{ s_consoleLogMutex };

			// Determine color value
			argb_t color = Color(1.0f, 1.0f, 1.0f);
			switch (entry.level->color)
			{
			case log_color::Yellow:		color = Color(1.0f, 1.0f, 0.0f);	break;
			case log_color::Green:		color = Color(0.0f, 1.0f, 0.0f);	break;
			case log_color::Red:		color = Color(1.0f, 0.0f, 0.0f);	break;
			case log_color::Purple:		color = Color(0.65f, 0.0f, 0.65f);	break;
			case log_color::Black:		color = Color(0.0f, 0.0f, 0.0f);	break;
			case log_color::Blue:		color = Color(0.4f, 0.5f, 1.0f);	break;
			case log_color::Grey:		color = Color(0.75, 0.75f, 0.75f);	break;
			default: break;
			}

			// Push log entry
			s_consoleLog.push_front({ color, entry.message });

			// Ensure log size doesn't explode
			if (s_consoleLog.size() > 50)
			{
				s_consoleLog.pop_back();
			}

			s_consoleTextDirty = true;
		});

		// Add the console layer
		s_consoleLayer = Screen::AddLayer(&Console::Paint, 100.0f, ScreenLayerFlags::IdentityTransform);

		// Watch for the console key event
		s_consoleKeyDownEvent = EventLoop::KeyDown.connect(&Console::KeyDown);
	}
	
	void Console::Destroy()
	{
		// Disconnect the key events
		s_consoleKeyDownEvent.disconnect();

		// Remove the console layer
		Screen::RemoveLayer(s_consoleLayer);

		// Destroy the screen system
		Screen::Destroy();
		
		// Close connection
		s_consoleLogConn.disconnect();

		// Delete console text geometry
		s_consoleTextGeom.reset();

		// Delete console font object
		s_consoleFont.reset();

		// Reset vertex and index buffer
		s_consoleIndBuf.reset();
		s_consoleVertBuf.reset();
		s_consoleLog.clear();

		// Unregister graphics console variables and do so before we destroy the graphics device, so that
		// no variables could ever affect the graphics device after it has been destroyed
		UnregisterGraphicsCVars();

		// Destroy the graphics device
		GraphicsDevice::Destroy();

		// Destroy the cvar manager
		ConsoleVarMgr::Destroy();

		// Remove default console commands
		UnregisterCommand("run");
		UnregisterCommand("ver");
	}

	inline void Console::RegisterCommand(const std::string & command, ConsoleCommandHandler handler, ConsoleCommandCategory category, const std::string & help)
	{
		// Don't do anything if this console command is already registered
		auto it = s_consoleCommands.find(command);
		if (it != s_consoleCommands.end())
		{
			return;
		}

		// Build command structure and add it
		ConsoleCommand cmd;
		cmd.category = category;
		cmd.help = help;
		cmd.handler = std::move(handler);
		s_consoleCommands.emplace(command, cmd);
	}

	inline void Console::UnregisterCommand(const std::string & command)
	{
		// Remove the respective iterator
		const auto it = s_consoleCommands.find(command);
		if (it != s_consoleCommands.end())
		{
			s_consoleCommands.erase(it);
		}
	}

	void Console::ExecuteCommand(const std::string& commandLine)
	{
		// Will hold the command name
		std::string command;
		std::string arguments;

		// Find the first space and use it to get the command
		const auto space = commandLine.find(' ');
		if (space == std::string::npos)
		{
			command = commandLine;
		}
		else
		{
			command = commandLine.substr(0, space);
			arguments = commandLine.substr(space + 1);
		}

		// If somehow the command is empty, just stop here without saying anything.
		if (command.empty())
		{
			return;
		}

		// Check if such argument exists
		const auto it = s_consoleCommands.find(command);
		if (it == s_consoleCommands.end())
		{
			ELOG("Unknown console command \"" << command << "\"");
			return;
		}

		// Now execute the console commands handler if there is any
		if (it->second.handler)
		{
			it->second.handler(command, arguments);
		}
	}

	bool Console::KeyDown(const int32 key)
	{
		// Console key will toggle the console visibility
		if (key == 0xC0 || key == 0xDC)
		{
			// Show the console window again
			s_consoleVisible = !s_consoleVisible;
			if (s_consoleVisible && s_consoleWindowHeight <= 0)
			{
				s_consoleWindowHeight = 200;
			}

			return false;
		}

		// Enable console scrolling by pressing the PAGE_UP / PAGE_DOWN keys
		if (key == 0x21 || key == 0x22)
		{
			if (key == 0x21)	// PG_UP
			{
				EnsureConsoleScrolling(1);
			}
			else				// PG_DOWN
			{

				EnsureConsoleScrolling(-1);
			}

		}

		return true;
	}

	bool Console::KeyUp(int32 key)
	{
		return true;
	}

	void Console::Paint()
	{
		// Nothing to render here eventually
		if (!s_consoleVisible)
			return;

		// Get the current graphics device
		auto& gx = GraphicsDevice::Get();

		// Create console text geometry
		if (s_consoleTextDirty)
		{
			s_consoleTextGeom->Reset();
			
			// Calculate start point
			Point startPoint{ 0.0f, static_cast<float>(s_consoleWindowHeight) };

			auto index = 0;

			// Determine max visible entries in console window
			const int32 maxVisibleEntries = s_consoleWindowHeight / s_consoleFont->GetHeight();
			for (auto& it : s_consoleLog)
			{
				// Skip as many items as required to reach the console scroll offset
				if (index++ < s_consoleScrollOffset)
				{
					continue;
				}

				// Reduce line by one
				startPoint.y -= s_consoleFont->GetHeight();

				// Draw line of text
				s_consoleFont->DrawText(it.message, startPoint, *s_consoleTextGeom, 1.0f, it.color);

				// Stop it here
				if (startPoint.y < 0.0f)
				{
					break;
				}
			}
			
			s_consoleTextDirty = false;
		}

		// Obtain viewport info
		auto vpWidth = 0, vpHeight = 0;
		gx.GetViewport(nullptr, nullptr, &vpWidth, &vpHeight, nullptr, nullptr);

		// Check for changes in viewport size, in which case we would need to update the contents of our vertex buffer
		if (vpWidth != s_lastViewportWidth || vpHeight != s_lastViewportHeight)
		{
			s_lastViewportWidth = vpWidth;
			s_lastViewportHeight = vpHeight;

			// Create the vertex buffer for the console background
			const POS_COL_VERTEX vertices[] = {
				{ { 0.0f, 0.0f, 0.0f }, 0xc0000000 },
				{ { static_cast<float>(s_lastViewportWidth), 0.0f, 0.0f }, 0xc0000000 },
				{ { static_cast<float>(s_lastViewportWidth), static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 },
				{ { 0.0f, static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 }
			};

			// Update vertex buffer data
			CScopedGxBufferLock<POS_COL_VERTEX> lock { *s_consoleVertBuf };
			*lock[0] = vertices[0];
			*lock[1] = vertices[1];
			*lock[2] = vertices[2];
			*lock[3] = vertices[3];
		}

		// Set up a clipping rect
		gx.SetClipRect(0, 0, s_lastViewportWidth, s_consoleWindowHeight);

		// Update transform
		gx.SetTransformMatrix(TransformType::Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, vpWidth, vpHeight, 0.0f, 100.0f));

		// Prepare drawing mode
		gx.SetVertexFormat(VertexFormat::PosColor);
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetBlendMode(BlendMode::Alpha);

		// Set buffers
		s_consoleVertBuf->Set();
		s_consoleIndBuf->Set();

		// Draw buffer content
		gx.DrawIndexed();

		// Draw text
		s_consoleTextGeom->Draw();

		// Clear the clip rect again
		gx.ResetClipRect();
	}
}
