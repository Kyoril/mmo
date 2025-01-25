// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "console.h"
#include "console_commands.h"
#include "console_var.h"
#include "event_loop.h"
#include "screen.h"

#include "base/assign_on_exit.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

#include "assets/asset_registry.h"

#include <algorithm>
#include <mutex>

#include "loading_screen.h"
#include "base/profiler.h"


namespace mmo
{
	ConsoleVar* s_lastRealmVar = nullptr;

	struct ConsoleLogEntry final
	{
		argb_t color;
		std::string message;
	};

	// Static private variables

	/// Map of case insensitive registered console commands.
	std::map<std::string, Console::ConsoleCommand, StrCaseIComp> Console::s_consoleCommands;
	/// Am event that listens for KeyDown events from the event loop to handle console input.
	static scoped_connection_container s_consoleKeyEvents;
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

	static std::vector<std::string> s_commandHistory;

	static int32_t s_commandHistoryIndex = 0;

	static std::string s_consoleInput;

	static scoped_connection s_perfChanged;

	namespace
	{
		ConsoleVar* s_dataPathCVar = nullptr;
	}

	// Graphics CVar stuff
	namespace
	{
		ConsoleVar* s_gxResolutionCVar = nullptr;
		ConsoleVar* s_gxWindowedCVar = nullptr;
		ConsoleVar* s_gxVSyncCVar = nullptr;
		ConsoleVar* s_gxApiCVar = nullptr;
		ConsoleVar* s_gxPerfCVar = nullptr;

		/// Helper struct for automatic gx cvar table.
		struct GxCVarHelper
		{
			std::string name;
			std::string description;
			std::string defaultValue;
			ConsoleVar** outputVar = nullptr;
		};

		/// A list of automatically registered and unregistered console variables that are also
		/// serialized when the games config file is serialized.
		const std::vector<GxCVarHelper> s_gxCVars = 
		{
			{"gxApi",			"Which graphics api should be used.",					"",			&s_gxApiCVar },
			{"gxResolution",	"The resolution of the primary output window.",			"1280x720",	&s_gxResolutionCVar },
			{"gxWindow",		"Whether the application will run in windowed mode.",	"1",		&s_gxWindowedCVar },
			{"gxVSync",			"Whether the application will run with vsync enabled.",	"1",		&s_gxVSyncCVar },

			{ "perf", "Toggles whether performance counters are visible", "0", &s_gxPerfCVar },

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

				if (x.outputVar != nullptr)
				{
					*x.outputVar = output;
				}
			});

			s_perfChanged = s_gxPerfCVar->Changed.connect([](ConsoleVar& var, const std::string&) { Profiler::GetInstance().SetEnabled(var.GetBoolValue()); });
		}

		/// Unregisters the automatically managed gx cvars from the table above.
		void UnregisterGraphicsCVars()
		{
			s_perfChanged.disconnect();

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
		inline void ApplyConsoleScrolling(int32 Amount)
		{
			AssignOnExit<bool> invalidateGraphics{ s_consoleTextDirty, true };
			
			s_consoleScrollOffset += Amount;
			s_consoleScrollOffset = std::max(0, s_consoleScrollOffset);
			
			const int32 maxVisibleEntries = s_consoleWindowHeight / s_consoleFont->GetHeight();
			
			if (s_consoleLog.size() < maxVisibleEntries)
			{
				s_consoleScrollOffset = 0;
				return;
			}
			
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

	
	void ConsoleCommand_Clear(const std::string& cmd, const std::string& args)
	{
		s_consoleLog.clear();
	}
	
	// Console implementation

	void Console::Initialize(const String& configFile)
	{
		RegisterCommand("ver", console_commands::ConsoleCommand_Ver, ConsoleCommandCategory::Default, "Displays the client version.");
		RegisterCommand("run", console_commands::ConsoleCommand_Run, ConsoleCommandCategory::Default, "Runs a console script.");
		RegisterCommand("quit", console_commands::ConsoleCommand_Quit, ConsoleCommandCategory::Default, "Shutdown the game client immediately.");
		RegisterCommand("list", console_commands::ConsoleCommand_List, ConsoleCommandCategory::Default, "Shows all available console commands.");
		RegisterCommand("clear", ConsoleCommand_Clear, ConsoleCommandCategory::Default, "Clears the console text.");
		
		ConsoleVarMgr::Initialize();
		
		s_dataPathCVar = ConsoleVarMgr::RegisterConsoleVar("dataPath", "The path of the client data directory.", (std::filesystem::current_path() / "Data").string());
		s_lastRealmVar = ConsoleVarMgr::RegisterConsoleVar("lastRealm", "Id of the last realm connected to.", "-1");
		
		auto* const localeCVar = ConsoleVarMgr::RegisterConsoleVar("locale", "The locale of the game client. Changing this requires a restart!", "enUS");

		console_commands::ConsoleCommand_Run("run", configFile);

		RegisterGraphicsCVars();

		s_consoleVisible = false;
		s_consoleWindowHeight = 210;

		const auto localeArchive = "Locales/Locale_" + localeCVar->GetStringValue();
		ILOG("Locale: " << localeCVar->GetStringValue());

		AssetRegistry::Initialize(s_dataPathCVar->GetStringValue(),
			{
				"Misc.hpak",
				"ClientDB.hpak",
				"Interface.hpak",
				"Fonts.hpak",
				"Models.hpak",
				"Textures.hpak",
				"Worlds.hpak",
				"Sound.hpak",
				localeArchive,
				localeArchive + ".hpak",
			});	
		
		const GraphicsApi defaultApi =
#if PLATFORM_WINDOWS
			GraphicsApi::D3D11;
#elif PLATFORM_APPLE
            GraphicsApi::Metal;
#else
			GraphicsApi::Null;
#endif
		
		auto api = defaultApi;
#if PLATFORM_WINDOWS
		if (_stricmp(s_gxApiCVar->GetStringValue().c_str(), "d3d11") == 0)
		{
			api = GraphicsApi::D3D11;
		}
#endif
        
#if PLATFORM_APPLE
        if (_stricmp(s_gxApiCVar->GetStringValue().c_str(), "metal") == 0)
        {
            api = GraphicsApi::Metal;
        }
#endif
        
		if (_stricmp(s_gxApiCVar->GetStringValue().c_str(), "gl") == 0)
		{
			api = GraphicsApi::OpenGL;
		}
		
		GraphicsDeviceDesc desc;
		ExtractResolution(s_gxResolutionCVar->GetStringValue(), desc.width, desc.height);
		desc.windowed = s_gxWindowedCVar->GetBoolValue();
		desc.vsync = s_gxVSyncCVar->GetBoolValue();
		
		switch (api)
		{
#if PLATFORM_WINDOWS
		case GraphicsApi::D3D11:
			GraphicsDevice::CreateD3D11(desc);
			break;
#endif
#if PLATFORM_APPLE
        case GraphicsApi::Metal:
            GraphicsDevice::CreateMetal(desc);
            break;
#endif
        case GraphicsApi::Null:
            GraphicsDevice::CreateNull(desc);
            break;
		case GraphicsApi::OpenGL:
			throw std::runtime_error("OpenGL device creation is not currently supported!");
		default:
			throw std::runtime_error("Unsupported graphics API value used!");
		}
		
		auto& device = GraphicsDevice::Get();
		device.GetAutoCreatedWindow()->SetTitle("MMORPG");
		
		device.GetAutoCreatedWindow()->Closed.connect([]() 
		{
			EventLoop::Terminate(0);
		});

		device.GetAutoCreatedWindow()->Resized.connect([](uint16 width, uint16 height) 
		{
			FrameManager::Get().NotifyScreenSizeChanged(width, height);
			if (auto topFrame = FrameManager::Get().GetTopFrame(); topFrame)
			{
				topFrame->Invalidate();
				topFrame->InvalidateChildren();
			}
		});
		
		device.GetViewport(nullptr, nullptr, &s_lastViewportWidth, &s_lastViewportHeight, nullptr, nullptr);
		
		const POS_COL_VERTEX vertices[] = {
			{ { 0.0f, 0.0f, 0.0f }, 0xc0000000 },
			{ { static_cast<float>(s_lastViewportWidth), 0.0f, 0.0f }, 0xc0000000 },
			{ { static_cast<float>(s_lastViewportWidth), static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 },
			{ { 0.0f, static_cast<float>(s_consoleWindowHeight), 0.0f }, 0xc0000000 }
		};
		
		s_consoleVertBuf = device.CreateVertexBuffer(4, sizeof(POS_COL_VERTEX), BufferUsage::DynamicWriteOnlyDiscardable, vertices);
		
		const uint16 indices[] = { 0, 1, 2, 2, 3, 0 };
		s_consoleIndBuf = device.CreateIndexBuffer(6, IndexBufferSize::Index_16, BufferUsage::DynamicWriteOnlyDiscardable, indices);
		
		s_consoleFont = FontManager::Get().CreateOrRetrieve("Fonts/consola.ttf", 16.0f, 0.0f);
		
		s_consoleTextGeom = std::make_unique<GeometryBuffer>();
		s_consoleTextDirty = true;
		s_consoleLog.clear();
		
		Screen::Initialize();
		LoadingScreen::Init();
		
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
		
		s_consoleLayer = Screen::AddLayer(&Console::Paint, 100.0f, IdentityTransform);
		
		s_consoleKeyEvents += 
		{
			EventLoop::KeyDown.connect(&Console::KeyDown, true),
			EventLoop::KeyChar.connect(&Console::KeyChar, true),
			EventLoop::KeyUp.connect(&Console::KeyUp, true),
		};
	}
	
	void Console::Destroy()
	{
		s_consoleKeyEvents.disconnect();
		
		Screen::RemoveLayer(s_consoleLayer);
		LoadingScreen::Destroy();
		
		Screen::Destroy();
		
		s_consoleLogConn.disconnect();
		
		s_consoleTextGeom.reset();
		
		s_consoleFont.reset();
		
		s_consoleIndBuf.reset();
		s_consoleVertBuf.reset();
		s_consoleLog.clear();
		
		UnregisterGraphicsCVars();
		
		GraphicsDevice::Destroy();
		
		ConsoleVarMgr::Destroy();
		
		UnregisterCommand("clear");
		UnregisterCommand("run");
		UnregisterCommand("ver");
		UnregisterCommand("quit");
	}

	void Console::ListCommands()
	{
		ILOG("Console commands available:");

		bool first = true;
		std::ostringstream streamCommands;
		for (const auto& kvp : s_consoleCommands)
		{
			if (!first)
			{
				streamCommands << ", ";
			}
			streamCommands << kvp.first;
			first = false;
		}

		ILOG(streamCommands.str());
	}

	inline void Console::RegisterCommand(const std::string & command, ConsoleCommandHandler handler, ConsoleCommandCategory category, const std::string & help)
	{
		if (const auto it = s_consoleCommands.find(command); it != s_consoleCommands.end())
		{
			return;
		}
		
		ConsoleCommand cmd;
		cmd.category = category;
		cmd.help = help;
		cmd.handler = std::move(handler);
		s_consoleCommands.emplace(command, cmd);
	}

	void Console::UnregisterCommand(const std::string & command)
	{
		if (const auto it = s_consoleCommands.find(command); it != s_consoleCommands.end())
		{
			s_consoleCommands.erase(it);
		}
	}

	void Console::ExecuteCommand(const std::string& commandLine)
	{
		std::string command;
		std::string arguments;
		
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
		
		if (command.empty())
		{
			return;
		}
		
		const auto it = s_consoleCommands.find(command);
		if (it == s_consoleCommands.end())
		{
			ELOG("Unknown console command \"" << command << "\"");
			return;
		}
		
		if (it->second.handler)
		{
			it->second.handler(command, arguments);
		}
	}

	bool Console::KeyUp(const int32 key)
	{
		if (s_consoleVisible)
		{
			abort_emission();
		}

		return !s_consoleVisible;
	}

	bool Console::KeyDown(const int32 key, bool repeat)
	{
		// Console key will toggle the console visibility
		if (key == 0xC0 || key == 0xDC)
		{
			s_consoleVisible = !s_consoleVisible;
			if (s_consoleVisible && s_consoleWindowHeight <= 0)
			{
				s_consoleWindowHeight = 200;
			}

			return false;
		}

		if (!s_consoleVisible)
		{
			return true;
		}

		// Escape key handler
		if (key == 0x1B)
		{
			if (s_consoleInput.empty())
			{
				s_consoleVisible = false;
			}
			else
			{
				s_consoleInput.resize(0);
				s_consoleTextDirty = true;
			}
			
			abort_emission();
			return false;
		}
		
		if (key == 0x0D && !s_consoleInput.empty())
		{
			ExecuteCommand(s_consoleInput);
			
			s_commandHistory.emplace_back(std::move(s_consoleInput));
			if (s_commandHistory.size() > 50)
			{
				s_commandHistory.erase(s_commandHistory.begin());
			}

			s_commandHistoryIndex = s_commandHistory.size();
			s_consoleTextDirty = true;
			
			abort_emission();
		}

		if (key == 0x26)	// UP
		{
			s_commandHistoryIndex = std::max(0, s_commandHistoryIndex - 1);
			if (s_commandHistoryIndex >= s_commandHistory.size())
			{
				return false;
			}

			s_consoleInput = s_commandHistory[s_commandHistoryIndex];
			s_consoleTextDirty = true;

			abort_emission();
		}
		else if (key == 0x28) // DOWN
		{
			s_commandHistoryIndex = std::min<int>(s_commandHistoryIndex + 1, s_commandHistory.size());
			if (s_commandHistoryIndex >= s_commandHistory.size())
			{
				return false;
			}

			s_consoleInput = s_commandHistory[s_commandHistoryIndex];
			s_consoleTextDirty = true;

			abort_emission();
		}
		
		if (key == 0x08)
		{
			if (s_consoleInput.empty())
				return false;

			s_consoleInput.pop_back();
			s_consoleTextDirty = true;

			abort_emission();
		}

		// Enable console scrolling by pressing the PAGE_UP / PAGE_DOWN keys
		if (key == 0x21 || key == 0x22)
		{
			if (key == 0x21)
			{
				ApplyConsoleScrolling(1);
			}
			else
			{

				ApplyConsoleScrolling(-1);
			}

			abort_emission();
		}

		abort_emission();
		return false;
	}

	bool Console::KeyChar(uint16 codepoint)
	{
		if (s_consoleVisible)
		{
			abort_emission();

			if (codepoint == 0xf6 || codepoint == 0xC0 || codepoint == 0xDC || codepoint == 0x0D || codepoint == 0x8 || codepoint == 0x26 || codepoint == 0x28 || codepoint == 0x1B)
				return false;

			s_consoleInput.push_back(static_cast<char>(codepoint & 0xff));
			s_consoleTextDirty = true;

			return false;
		}

		return true;
	}
	
	void Console::Paint()
	{
		auto& gx = GraphicsDevice::Get();

		const bool showPerf = s_gxPerfCVar->GetBoolValue();

		// Nothing to show here
		if (!showPerf && !s_consoleVisible)
		{
			return;
		}

		if (showPerf || s_consoleTextDirty)
		{
			s_consoleTextGeom->Reset();
		}

		if (showPerf)
		{
			std::stringstream strm;
			strm << "Batch count: " << gx.GetBatchCount() << "\n";

			auto& metrics = Profiler::GetInstance().GetMetrics();
			for (auto& m : metrics)
			{
				strm << m.name << " " << std::setprecision(2) << m.totalTimeMs << " ms (" << m.callCount << " calls)\n";
			}

			s_consoleFont->DrawText(strm.str(), Point(0.0f, 0.0f), *s_consoleTextGeom, 1.0f);
		}

		if (s_consoleTextDirty)
		{
			// Calculate start point
			Point startPoint{ 0.0f, static_cast<float>(s_consoleWindowHeight) - s_consoleFont->GetHeight() };
			
			// Draw line of text
			s_consoleFont->DrawText("> ", startPoint, *s_consoleTextGeom, 1.0f);
			s_consoleFont->DrawText(s_consoleInput, startPoint + Point(16.0f, 0.0f), *s_consoleTextGeom, 1.0f);

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

		auto vpWidth = 0, vpHeight = 0;
		gx.GetViewport(nullptr, nullptr, &vpWidth, &vpHeight, nullptr, nullptr);

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
			CScopedGxBufferLock<POS_COL_VERTEX> lock { *s_consoleVertBuf, LockOptions::Discard };
			*lock[0] = vertices[0];
			*lock[1] = vertices[1];
			*lock[2] = vertices[2];
			*lock[3] = vertices[3];
		}
		
		gx.SetClipRect(0, 0, s_lastViewportWidth, s_consoleWindowHeight);
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, vpWidth, vpHeight, 0.0f, 100.0f));
		
		gx.SetVertexFormat(VertexFormat::PosColor);
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetBlendMode(BlendMode::Alpha);
		
		s_consoleVertBuf->Set(0);
		s_consoleIndBuf->Set(0);
		gx.DrawIndexed();
		
		s_consoleTextGeom->Draw();
		
		gx.ResetClipRect();
	}
}
