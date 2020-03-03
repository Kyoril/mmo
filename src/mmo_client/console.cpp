// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "console.h"
#include "console_commands.h"
#include "console_var.h"
#include "event_loop.h"
#include "screen.h"

#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/font.h"
#include "frame_ui/geometry_buffer.h"
#include "base/assign_on_exit.h"

#include <mutex>
#include <algorithm>


namespace mmo
{
	// Static private variables

	std::map<std::string, Console::ConsoleCommand, StrCaseIComp> Console::s_consoleCommands;

	/// A event that listens for KeyDown events from the event loop to handle console input.
	static scoped_connection s_consoleKeyDownEvent;

	// Used to determine whether the console window needs to be rendered right now
	/// Whether the console window should be rendered right now.
	static bool s_consoleVisible = false;
	/// The current height of the console window in pixels, counted from the top edge.
	static int32 s_consoleWindowHeight = 210;
	static int32 s_lastViewportWidth = 0;
	static int32 s_lastViewportHeight = 0;
	/// Scroll offset of the console text in lines.
	static int32 s_consoleScrollOffset = 0;

	// Used for rendering the console window background
	static ScreenLayerIt s_consoleLayer;
	static VertexBufferPtr s_consoleVertBuf;
	static IndexBufferPtr s_consoleIndBuf;

	static FontPtr s_consoleFont;
	static std::unique_ptr<GeometryBuffer> s_consoleTextGeom;
	static bool s_consoleTextDirty = true;
	static std::list<std::string> s_consoleLog;

	static std::mutex s_consoleLogMutex;
	static scoped_connection s_consoleLogConn;



	// Graphics CVar stuff
	namespace
	{
		static ConsoleVar* s_gxResolutionCVar = nullptr;
		static ConsoleVar* s_gxWindowedCVar = nullptr;
		static ConsoleVar* s_gxVSyncCVar = nullptr;


		/// Helper struct for automatic gx cvar table.
		typedef struct
		{
			std::string name;
			std::string description;
			std::string defaultValue;
			ConsoleVar** outputVar = nullptr;
			std::function<ConsoleVar::ChangedSignal::signature_type> changedHandler = nullptr;
		} GxCVarHelper;

		/// A list of automatically registered and unregistered console variables that are also
		/// serialized when the games config file is serialized.
		static const std::vector<GxCVarHelper> s_gxCVars = 
		{
			{"gxResolution",	"The resolution of the primary output window.",			"1280x720",	&s_gxResolutionCVar,	nullptr },
			{"gxWindow",		"Whether the application will run in windowed mode.",	"1",		&s_gxWindowedCVar,		nullptr },
			{"gxVSync",			"Whether the application will run with vsync enabled.",	"0",		&s_gxVSyncCVar,			nullptr },

			// TODO: Add more graphics cvars here that should be registered and unregistered automatically
			// as well as being serialized when saving the graphics settings of the game.
		};


		/// Triggered when a gxcvar is changed to invalidate the current graphics settings.
		static void GxCVarChanged(ConsoleVar& var, const std::string& oldValue)
		{

		}


		/// Registers the automatically managed gx cvars from the table above.
		static void RegisterGraphicsCVars()
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
		static void UnregisterGraphicsCVars()
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
	}



	// Console implementation

	void Console::Initialize(const std::filesystem::path& configFile)
	{
		// Ensure the folder is created
		std::filesystem::create_directories(configFile.parent_path());

		// Register some default console commands
		RegisterCommand("ver", console_commands::ConsoleCommand_Ver, ConsoleCommandCategory::Default, "Displays the client version.");
		RegisterCommand("run", console_commands::ConsoleCommand_Run, ConsoleCommandCategory::Default, "Runs a console script.");

		// Initialize the cvar manager
		ConsoleVarMgr::Initialize();

		// Register graphics variables
		RegisterGraphicsCVars();

		// Load the config file
		console_commands::ConsoleCommand_Run("run", configFile.string());

		// Console is hidden by default
		s_consoleVisible = false;
		s_consoleWindowHeight = 210;

		// Initialize the graphics api
		auto& device = GraphicsDevice::CreateD3D11();
		device.SetWindowTitle("MMORPG");

		// Query the viewport size
		device.GetViewport(nullptr, nullptr, &s_lastViewportWidth, &s_lastViewportHeight, nullptr, nullptr);

		// Create the vertex buffer for the console background
		const POS_COL_VERTEX vertices[] = {
			{ { 0.0f, 0.0f, 0.0f }, 0xc0000000 },
			{ { (float)s_lastViewportWidth, 0.0f, 0.0f }, 0xc0000000 },
			{ { (float)s_lastViewportWidth, (float)s_consoleWindowHeight, 0.0f }, 0xc0000000 },
			{ { 0.0f, (float)s_consoleWindowHeight, 0.0f }, 0xc0000000 }
		};

		// Setup vertices
		s_consoleVertBuf = device.CreateVertexBuffer(4, sizeof(POS_COL_VERTEX), true, vertices);

		// Setup indices
		const uint16 indices[] = { 0, 1, 2, 2, 3, 0 };
		s_consoleIndBuf = device.CreateIndexBuffer(6, IndexBufferSize::Index_16, indices);

		// Load the console font
		s_consoleFont = std::make_shared<Font>();
		VERIFY(s_consoleFont->Initialize("Fonts/consola.ttf", 12.0f, 0.0f));

		// Create a geometry buffer for the console output text
		s_consoleTextGeom = std::make_unique<GeometryBuffer>();
		s_consoleTextDirty = true;
		s_consoleLog.clear();

		// Initialize the screen system
		Screen::Initialize();

		// Initialize the frame manager
		FrameManager::Initialize();

		// Assign console log signal
		s_consoleLogConn = mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry & entry) {
			std::scoped_lock lock{ s_consoleLogMutex };

			s_consoleLog.push_front(entry.message);
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

		// Destroy the frame manager
		FrameManager::Destroy();

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
		cmd.help = std::move(help);
		cmd.handler = std::move(handler);
		s_consoleCommands.emplace(command, cmd);
	}

	inline void Console::UnregisterCommand(const std::string & command)
	{
		// Remove the respective iterator
		auto it = s_consoleCommands.find(command);
		if (it != s_consoleCommands.end())
		{
			s_consoleCommands.erase(it);
		}
	}

	void Console::ExecuteCommand(std::string commandLine)
	{
		// Will hold the command name
		std::string command;
		std::string arguments;

		// Find the first space and use it to get the command
		auto space = commandLine.find(' ');
		if (space == commandLine.npos)
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
		auto it = s_consoleCommands.find(command);
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

	bool Console::KeyDown(int32 key)
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
			mmo::Point startPoint{ 0.0f, static_cast<float>(s_consoleWindowHeight) };

			int32 index = 0;

			// Determine max visible entries in console window
			const int32 maxVisibleEntries = s_consoleWindowHeight / s_consoleFont->GetHeight();
			for (auto it = s_consoleLog.begin(); it != s_consoleLog.end(); it++)
			{
				// Skip as many items as required to reach the console scroll offset
				if (index++ < s_consoleScrollOffset)
				{
					continue;
				}

				// Reduce line by one
				startPoint.y -= s_consoleFont->GetHeight();

				// Draw line of text
				s_consoleFont->DrawText(*it, startPoint, *s_consoleTextGeom);

				// Stop it here
				if (startPoint.y < 0.0f)
				{
					break;
				}
			}
			
			s_consoleTextDirty = false;
		}

		// Obtain viewport info
		int32 s_vpWidth = 0, s_vpHeight = 0;
		gx.GetViewport(nullptr, nullptr, &s_vpWidth, &s_vpHeight, nullptr, nullptr);

		// Check for changes in viewport size, in which case we would need to update the contents of our vertex buffer
		if (s_vpWidth != s_lastViewportWidth || s_vpHeight != s_lastViewportHeight)
		{
			s_lastViewportWidth = s_vpWidth;
			s_lastViewportHeight = s_vpHeight;

			// Create the vertex buffer for the console background
			const POS_COL_VERTEX vertices[] = {
				{ { 0.0f, 0.0f, 0.0f }, 0xc0000000 },
				{ { (float)s_lastViewportWidth, 0.0f, 0.0f }, 0xc0000000 },
				{ { (float)s_lastViewportWidth, (float)s_consoleWindowHeight, 0.0f }, 0xc0000000 },
				{ { 0.0f, (float)s_consoleWindowHeight, 0.0f }, 0xc0000000 }
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
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::MakeOrthographic(0.0f, s_vpWidth, s_vpHeight, 0.0f, 0.0f, 100.0f));

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
