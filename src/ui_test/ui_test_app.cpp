// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ui_test_app.h"

#include "event_loop.h"
#include "placeholder_frames.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_entry.h"
#include "log/log_std_stream.h"
#include "tex/png_writer.h"

#include "lua.hpp"
#include "luabind/luabind.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// The running app, for the Lua C functions below.
		UiTestApp* s_app = nullptr;

		/// Seconds between two scans of the watched files.
		constexpr float WatchInterval = 0.25f;

		/// Seconds without further changes before a reload starts, so that a burst of saves loads once.
		constexpr float ReloadDebounce = 0.15f;

		/// Seconds between two scans of the commands inbox.
		constexpr float CommandInterval = 0.1f;

		/// Lua installed after the mocks ran: reports reads of undefined globals instead of silently
		/// returning nil, so a missing mock shows up in status.json.
		const char* const UndefinedGlobalTracker = R"lua(
			setmetatable(_G, {
				__index = function(_, key)
					if type(key) == "string" then
						UiTest.ReportUndefined(key)
					end
					return nil
				end
			})
		)lua";

		std::string JsonEscape(const std::string& text)
		{
			std::string result;
			result.reserve(text.size() + 8);
			for (const char c : text)
			{
				switch (c)
				{
				case '"': result += "\\\""; break;
				case '\\': result += "\\\\"; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
						result += buffer;
					}
					else
					{
						result += c;
					}
				}
			}
			return result;
		}

		template <typename Container>
		void WriteJsonArray(std::ostream& out, const char* name, const Container& values, const bool last = false)
		{
			out << "  \"" << name << "\": [";
			bool first = true;
			for (const auto& value : values)
			{
				out << (first ? "\n    \"" : ",\n    \"") << JsonEscape(value) << "\"";
				first = false;
			}
			out << (first ? "]" : "\n  ]") << (last ? "\n" : ",\n");
		}

		/// Formats a Lua value for logs and command results. luabind objects are never passed to tostring,
		/// whose __tostring hangs on them; frames are described by their name instead.
		std::string DescribeLuaValue(lua_State* state, int index)
		{
			index = lua_absindex(state, index);
			switch (lua_type(state, index))
			{
			case LUA_TNIL:
			case LUA_TBOOLEAN:
			case LUA_TNUMBER:
			case LUA_TSTRING:
				{
					size_t length = 0;
					const char* value = luaL_tolstring(state, index, &length);
					std::string result(value, length);
					lua_pop(state, 1);
					return result;
				}
			case LUA_TUSERDATA:
				{
					std::string result = "userdata";
					if (luaL_loadstring(state, "local v = ... local ok, n = pcall(function() return v:GetName() end) if ok and type(n) == 'string' then return n end") != 0)
					{
						lua_pop(state, 1);
						return result;
					}

					lua_pushvalue(state, index);
					if (lua_pcall(state, 1, 1, 0) == 0 && lua_type(state, -1) == LUA_TSTRING)
					{
						result = "frame " + std::string(lua_tostring(state, -1));
					}
					lua_pop(state, 1);
					return result;
				}
			default:
				return lua_typename(state, lua_type(state, index));
			}
		}

		int LuaReportUndefined(lua_State* state)
		{
			if (s_app && lua_isstring(state, 1))
			{
				s_app->ReportUndefinedGlobal(lua_tostring(state, 1));
			}
			return 0;
		}

		int LuaReportUnmocked(lua_State* state)
		{
			if (s_app && lua_isstring(state, 1))
			{
				s_app->ReportUnmockedCall(lua_tostring(state, 1));
			}
			return 0;
		}

		int LuaPrint(lua_State* state)
		{
			std::ostringstream text;
			const int count = lua_gettop(state);
			for (int i = 1; i <= count; ++i)
			{
				text << (i > 1 ? "\t" : "") << DescribeLuaValue(state, i);
			}
			ILOG("[lua] " << text.str());
			return 0;
		}

		int LuaFireEvent(lua_State* state)
		{
			const std::string eventName = luaL_checkstring(state, 1);

			std::vector<luabind::object> args;
			for (int i = 2; i <= lua_gettop(state); ++i)
			{
				args.emplace_back(luabind::from_stack(state, i));
			}

			FrameManager& frameMgr = FrameManager::Get();
			switch (args.size())
			{
			case 0: frameMgr.TriggerLuaEvent(eventName); break;
			case 1: frameMgr.TriggerLuaEvent(eventName, args[0]); break;
			case 2: frameMgr.TriggerLuaEvent(eventName, args[0], args[1]); break;
			case 3: frameMgr.TriggerLuaEvent(eventName, args[0], args[1], args[2]); break;
			case 4: frameMgr.TriggerLuaEvent(eventName, args[0], args[1], args[2], args[3]); break;
			case 5: frameMgr.TriggerLuaEvent(eventName, args[0], args[1], args[2], args[3], args[4]); break;
			default: frameMgr.TriggerLuaEvent(eventName, args[0], args[1], args[2], args[3], args[4], args[5]); break;
			}
			return 0;
		}

		int LuaScreenshot(lua_State* state)
		{
			if (s_app)
			{
				s_app->RequestScreenshot(luaL_optstring(state, 1, "screenshot.png"));
			}
			return 0;
		}

		int LuaGetScreenSize(lua_State* state)
		{
			const auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
			lua_pushinteger(state, window ? window->GetWidth() : 0);
			lua_pushinteger(state, window ? window->GetHeight() : 0);
			return 2;
		}

		std::vector<std::string> Tokenize(const std::string& line)
		{
			std::vector<std::string> tokens;
			std::istringstream stream(line);
			std::string token;
			while (stream >> token)
			{
				tokens.push_back(token);
			}
			return tokens;
		}

		std::string RestOfLine(const std::string& line)
		{
			const size_t start = line.find_first_not_of(" \t");
			const size_t space = line.find_first_of(" \t", start);
			if (space == std::string::npos)
			{
				return {};
			}
			const size_t rest = line.find_first_not_of(" \t", space);
			return rest == std::string::npos ? std::string() : line.substr(rest);
		}

		/// Formats a Lua value as a Lua literal for a generated event call.
		std::string ToLuaLiteral(const std::string& token)
		{
			char* end = nullptr;
			std::strtod(token.c_str(), &end);
			if (end && *end == '\0' && !token.empty())
			{
				return token;
			}
			if (token == "true" || token == "false" || token == "nil")
			{
				return token;
			}

			std::string quoted = "\"";
			for (const char c : token)
			{
				if (c == '"' || c == '\\')
				{
					quoted += '\\';
				}
				quoted += c;
			}
			return quoted + "\"";
		}

		int32 ParseKey(const std::string& token)
		{
			static const std::map<std::string, int32> named = {
				{ "ESCAPE", 0x1B }, { "ENTER", 0x0D }, { "TAB", 0x09 }, { "SPACE", 0x20 },
				{ "BACKSPACE", 0x08 }, { "LEFT", 0x25 }, { "UP", 0x26 }, { "RIGHT", 0x27 }, { "DOWN", 0x28 },
			};

			std::string upper = token;
			std::transform(upper.begin(), upper.end(), upper.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
			if (const auto it = named.find(upper); it != named.end())
			{
				return it->second;
			}
			if (upper.size() == 1)
			{
				return upper[0];
			}
			return std::atoi(token.c_str());
		}
	}

	UiTestApp::UiTestApp(UiTestOptions options)
		: m_options(std::move(options))
	{
		s_app = this;
	}

	UiTestApp::~UiTestApp()
	{
		s_app = nullptr;
	}

	bool UiTestApp::Initialize()
	{
		std::error_code ec;
		std::filesystem::create_directories(m_options.outPath / "commands", ec);

		m_logFile.open(m_options.outPath / "ui_test.log", std::ios::out | std::ios::trunc);
		m_logConnection = g_DefaultLog.signal().connect([this](const LogEntry& entry) { OnLogEntry(entry); });

		ILOG("Data: " << m_options.dataPath.string());
		ILOG("Tools: " << m_options.toolPath.string());
		ILOG("Output: " << m_options.outPath.string());

		GraphicsDeviceDesc desc{};
		desc.width = m_options.width;
		desc.height = m_options.height;
		desc.windowed = true;
		desc.vsync = true;
#ifdef _WIN32
		GraphicsDevice::CreateD3D11(desc);
#elif defined(__APPLE__)
		GraphicsDevice::CreateMetal(desc);
#endif

		const RenderWindowPtr window = GraphicsDevice::Get().GetAutoCreatedWindow();
		if (!window)
		{
			ELOG("Could not create the render window");
			return false;
		}

		m_connections += window->Closed.connect([]() { EventLoop::Terminate(0); });
		m_connections += window->Resized.connect([](const uint16 width, const uint16 height)
			{
				if (FrameManager::IsInitialized())
				{
					FrameManager::Get().NotifyScreenSizeChanged(width, height);
				}
			});

		AssetRegistry::Initialize(m_options.dataPath, { "Locales/Locale_" + m_options.locale });

		// 2x2 opaque white, so the backdrop can be drawn through the UI's own geometry buffer.
		const uint32 white[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
		m_whiteTexture = GraphicsDevice::Get().CreateTexture(2, 2);
		m_whiteTexture->LoadRaw(const_cast<uint32*>(white), sizeof(white));
		if (!m_options.backgroundTexture.empty())
		{
			m_backgroundTexture = TextureManager::Get().CreateOrRetrieve(m_options.backgroundTexture);
			if (!m_backgroundTexture)
			{
				WLOG("Background texture " << m_options.backgroundTexture << " could not be loaded");
			}
		}

		EventLoop::Initialize();
		m_connections += EventLoop::Idle.connect([this](const float deltaSeconds, GameTime) { OnIdle(deltaSeconds); });
		m_connections += EventLoop::Paint.connect([this]() { OnPaint(); });

		m_connections += EventLoop::MouseMove.connect([](const int32 x, const int32 y)
			{
				FrameManager::Get().NotifyMouseMoved(Point(x, y));
				return false;
			});
		m_connections += EventLoop::MouseDown.connect([](const EMouseButton button, const int32 x, const int32 y)
			{
				return FrameManager::Get().NotifyMouseDown(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			});
		m_connections += EventLoop::MouseUp.connect([](const EMouseButton button, const int32 x, const int32 y)
			{
				return FrameManager::Get().NotifyMouseUp(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			});
		m_connections += EventLoop::MouseWheel.connect([](const int32 delta)
			{
				return FrameManager::Get().NotifyMouseWheel(delta);
			});
		m_connections += EventLoop::KeyDown.connect([this](const int32 key, bool)
			{
				// F5 reloads, F12 takes a screenshot; everything else goes to the UI.
				if (key == 0x74)
				{
					Reload("F5");
					return true;
				}
				if (key == 0x7B)
				{
					RequestScreenshot("screenshot.png");
					return true;
				}

				FrameManager::Get().NotifyKeyDown(key);
				return true;
			});
		m_connections += EventLoop::KeyChar.connect([](const uint16 codepoint)
			{
				FrameManager::Get().NotifyKeyChar(codepoint);
				return false;
			});
		m_connections += EventLoop::KeyUp.connect([](const int32 key)
			{
				FrameManager::Get().NotifyKeyUp(key);
				return false;
			});

		SetupWatcher();
		Reload("startup");
		return true;
	}

	int32 UiTestApp::Run()
	{
		EventLoop::Run();
		return m_exitCode;
	}

	void UiTestApp::Shutdown()
	{
		UnloadUi();
		m_connections.disconnect();
		m_captureTarget.reset();
		m_backdrop.reset();
		m_whiteTexture.reset();
		m_backgroundTexture.reset();
		EventLoop::Destroy();
		AssetRegistry::Destroy();
		GraphicsDevice::Destroy();
		m_logConnection.disconnect();
	}

	void UiTestApp::ReportUndefinedGlobal(const std::string& name)
	{
		m_undefinedGlobals.insert(name);
	}

	void UiTestApp::ReportUnmockedCall(const std::string& name)
	{
		m_unmockedCalls.insert(name);
	}

	void UiTestApp::RequestScreenshot(const std::filesystem::path& path)
	{
		m_captures.push_back({ ResolveOutPath(path), false });
	}

	void UiTestApp::OnIdle(const float deltaSeconds)
	{
		if (FrameManager::IsInitialized())
		{
			FrameManager::Get().Update(deltaSeconds);
		}

		if (m_options.watch)
		{
			m_watchTimer += deltaSeconds;
			if (m_watchTimer >= WatchInterval)
			{
				m_watchTimer = 0.0f;
				const auto changed = m_watcher.Poll();
				if (!changed.empty())
				{
					for (const auto& file : changed)
					{
						const auto relative = file.lexically_relative(m_options.dataPath);
						const bool insideData = !relative.empty() && *relative.begin() != "..";
						m_changedFiles.push_back((insideData ? relative : file).generic_string());
					}

					// New or deleted files are unknown to the asset registry's file list.
					if (m_watcher.FileSetChanged())
					{
						AssetRegistry::Destroy();
						AssetRegistry::Initialize(m_options.dataPath, { "Locales/Locale_" + m_options.locale });
					}

					m_pendingReloadReason = "file change";
					m_reloadDelay = ReloadDebounce;
				}
			}

			if (m_reloadDelay >= 0.0f)
			{
				m_reloadDelay -= deltaSeconds;
				if (m_reloadDelay < 0.0f)
				{
					Reload(m_pendingReloadReason);
				}
			}

			m_commandTimer += deltaSeconds;
			if (m_commandTimer >= CommandInterval)
			{
				m_commandTimer = 0.0f;
				PollCommands();
			}
		}

		// Commands run one per frame, and wait while the UI settles or a screenshot is pending.
		if (!m_batches.empty() && m_settleFrames <= 0 && m_captures.empty())
		{
			if (m_waitFrames > 0)
			{
				--m_waitFrames;
			}
			else
			{
				CommandBatch& batch = m_batches.front();
				if (batch.next < batch.lines.size())
				{
					ExecuteCommand(batch.lines[batch.next++]);
				}
				else
				{
					FinishBatch();
				}
			}
		}
	}

	void UiTestApp::OnPaint()
	{
		auto& gx = GraphicsDevice::Get();

		if (m_settleFrames > 0)
		{
			--m_settleFrames;
		}

		if (m_settleFrames <= 0 && m_statusPending)
		{
			m_statusPending = false;

			std::filesystem::path screenshot;
			if (!m_options.oneShotScreenshot.empty())
			{
				screenshot = ResolveOutPath(m_options.oneShotScreenshot);
			}
			else
			{
				screenshot = m_options.outPath / "latest.png";
			}

			if (!Capture(screenshot))
			{
				screenshot.clear();
			}
			WriteStatus(screenshot);

			if (!m_options.oneShotScreenshot.empty())
			{
				m_exitCode = (screenshot.empty() || !m_loadErrors.empty()) ? 1 : 0;
				m_oneShotDone = true;
				EventLoop::Terminate(m_exitCode);
			}
		}

		if (m_settleFrames <= 0)
		{
			while (!m_captures.empty())
			{
				const CaptureRequest request = m_captures.front();
				m_captures.pop_front();

				const bool ok = Capture(request.path);
				if (!m_batches.empty())
				{
					m_batches.front().output.push_back(ok ? "screenshot " + request.path.generic_string() : "screenshot failed: " + request.path.generic_string());
				}
			}
		}

		// The window: the capture above rendered into its own target and left the window inactive.
		const auto window = gx.GetAutoCreatedWindow();
		window->Activate();
		window->Clear(ClearFlags::All);
		DrawBackdrop();
		if (FrameManager::IsInitialized())
		{
			FrameManager::Get().Draw();
		}
	}

	void UiTestApp::OnLogEntry(const LogEntry& entry)
	{
		const auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()).count() / 1000.0;
		const char* level = entry.level ? entry.level->name.c_str() : "?";

		if (m_logFile)
		{
			m_logFile << std::fixed << seconds << " [" << level << "] " << entry.message << "\n";
			m_logFile.flush();
		}

#ifdef _WIN32
		OutputDebugStringA(("[" + std::string(level) + "] " + entry.message + "\n").c_str());
#endif

		if (entry.level != &ErrorLevel && entry.level != &WarningLevel)
		{
			// Lua print() output belongs to the command that caused it.
			if (!m_batches.empty() && entry.message.rfind("[lua] ", 0) == 0)
			{
				m_batches.front().output.push_back(entry.message);
			}
			return;
		}

		// OnUpdate handlers repeat their error every frame: keep each message once, with a count.
		if (m_messageCounts[entry.message]++ > 0)
		{
			return;
		}
		(entry.level == &ErrorLevel ? m_loadErrors : m_loadWarnings).push_back(entry.message);

		if (!m_batches.empty())
		{
			m_batches.front().output.push_back(std::string(entry.level == &ErrorLevel ? "error: " : "warning: ") + entry.message);
		}
	}

	void UiTestApp::Reload(const std::string& reason)
	{
		m_reloadDelay = -1.0f;
		++m_reloadCount;

		ILOG("Reload #" << m_reloadCount << " (" << reason << ")");

		const auto start = std::chrono::steady_clock::now();
		m_loadErrors.clear();
		m_loadWarnings.clear();
		m_messageCounts.clear();
		m_undefinedGlobals.clear();
		m_unmockedCalls.clear();

		UnloadUi();
		LoadUi();

		m_lastLoadMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		ILOG("Loaded in " << m_lastLoadMs << " ms with " << m_loadErrors.size() << " errors and " << m_loadWarnings.size() << " warnings");

		// Scenario and mock files may have been added or removed meanwhile.
		SetupWatcher();

		m_settleFrames = m_options.settleFrames;
		m_statusPending = true;
		UpdateWindowTitle();
	}

	void UiTestApp::UnloadUi()
	{
		if (FrameManager::IsInitialized())
		{
			// Same order as the client: frames (holding Lua references) first, then the Lua state.
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
		}

		if (m_luaState)
		{
			lua_close(m_luaState);
			m_luaState = nullptr;
		}

		m_localization.reset();
	}

	void UiTestApp::LoadUi()
	{
		// The UI files and the frame_ui code that interprets them have to come from the same checkout.
		// Worktrees usually have empty data submodules, so the data may silently come from another one.
		std::error_code ec;
		const auto ownData = m_options.toolPath.parent_path().parent_path() / "data/client";
		if (!std::filesystem::equivalent(ownData, m_options.dataPath, ec))
		{
			WLOG("UI data comes from " << m_options.dataPath.generic_string() << ", not from this checkout (" << ownData.generic_string()
				<< "). If that checkout has frame_ui changes this build lacks, layouts may render wrong.");
		}

		m_luaState = luaL_newstate();
		luaL_openlibs(m_luaState);
		luabind::open(m_luaState);

		m_localization = std::make_unique<Localization>();
		if (!m_localization->LoadFromFile())
		{
			ELOG("Localization.txt of locale " << m_options.locale << " could not be loaded");
		}

		FrameManager::Initialize(m_luaState, *m_localization);
		RegisterPlaceholderFrames(m_luaState);
		RegisterLuaApi();

		const auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
		FrameManager::Get().NotifyScreenSizeChanged(window->GetWidth(), window->GetHeight());

		// Mocks stand in for the game API the client registers in GameScript.
		std::vector<std::filesystem::path> mocks;
		for (const auto& entry : std::filesystem::directory_iterator(m_options.toolPath / "mocks", ec))
		{
			if (entry.path().extension() == ".lua")
			{
				mocks.push_back(entry.path());
			}
		}
		std::sort(mocks.begin(), mocks.end());
		for (const auto& mock : mocks)
		{
			RunLuaFile(mock);
		}

		FrameManager::Get().ExecuteLua(UndefinedGlobalTracker);

		// The client's top frame names; the layouts anchor to them.
		const bool glue = m_options.toc.find("GlueUI") != std::string::npos;
		const auto topFrame = FrameManager::Get().CreateOrRetrieve("Frame", glue ? "TopFrame" : "TopGameFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		FrameManager::Get().SetTopFrame(topFrame);

		if (!AssetRegistry::HasFile(m_options.toc))
		{
			ELOG("TOC file " << m_options.toc << " not found in " << m_options.dataPath.string());
		}
		FrameManager::Get().LoadUIFile(m_options.toc);

		if (!m_options.scenario.empty())
		{
			RunLuaFile(m_options.scenario);
		}
	}

	void UiTestApp::RegisterLuaApi()
	{
		lua_State* state = m_luaState;

		lua_pushcfunction(state, LuaPrint);
		lua_setglobal(state, "print");

		lua_newtable(state);
		lua_pushcfunction(state, LuaFireEvent);
		lua_setfield(state, -2, "FireEvent");
		lua_pushcfunction(state, LuaScreenshot);
		lua_setfield(state, -2, "Screenshot");
		lua_pushcfunction(state, LuaGetScreenSize);
		lua_setfield(state, -2, "GetScreenSize");
		lua_pushcfunction(state, LuaReportUndefined);
		lua_setfield(state, -2, "ReportUndefined");
		lua_pushcfunction(state, LuaReportUnmocked);
		lua_setfield(state, -2, "ReportUnmocked");
		lua_pushstring(state, m_options.toc.c_str());
		lua_setfield(state, -2, "toc");
		lua_pushstring(state, m_options.locale.c_str());
		lua_setfield(state, -2, "locale");
		lua_pushstring(state, ((m_options.toolPath / "scenarios").generic_string() + "/").c_str());
		lua_setfield(state, -2, "scenarioDir");
		lua_setglobal(state, "UiTest");
	}

	void UiTestApp::RunLuaFile(const std::filesystem::path& file)
	{
		if (luaL_dofile(m_luaState, file.string().c_str()) != 0)
		{
			ELOG("Lua error in " << file.filename().string() << ": " << lua_tostring(m_luaState, -1));
			lua_pop(m_luaState, 1);
		}
	}

	void UiTestApp::SetupWatcher()
	{
		m_watcher.Clear();
		m_watcher.SetExtensions({ ".xml", ".lua", ".toc", ".txt" });
		m_watcher.AddFolder(m_options.dataPath / "Interface");
		m_watcher.AddFolder(m_options.toolPath / "mocks");
		m_watcher.AddFile(m_options.dataPath / "Locales" / ("Locale_" + m_options.locale) / "Localization.txt");
		m_watcher.AddFolder(m_options.toolPath / "scenarios");
		if (!m_options.scenario.empty())
		{
			m_watcher.AddFile(m_options.scenario);
		}
		m_watcher.Snapshot();
	}

	void UiTestApp::WriteStatus(const std::filesystem::path& screenshot)
	{
		std::ofstream out(m_options.outPath / "status.json", std::ios::out | std::ios::trunc);
		out << "{\n";
		out << "  \"reload\": " << m_reloadCount << ",\n";
		out << "  \"ok\": " << (m_loadErrors.empty() ? "true" : "false") << ",\n";
		out << "  \"toc\": \"" << JsonEscape(m_options.toc) << "\",\n";
		out << "  \"scenario\": \"" << JsonEscape(m_options.scenario.generic_string()) << "\",\n";
		out << "  \"locale\": \"" << JsonEscape(m_options.locale) << "\",\n";
		out << "  \"dataPath\": \"" << JsonEscape(m_options.dataPath.generic_string()) << "\",\n";
		out << "  \"width\": " << GraphicsDevice::Get().GetAutoCreatedWindow()->GetWidth() << ",\n";
		out << "  \"height\": " << GraphicsDevice::Get().GetAutoCreatedWindow()->GetHeight() << ",\n";
		out << "  \"loadMs\": " << static_cast<int32>(m_lastLoadMs) << ",\n";
		out << "  \"screenshot\": \"" << JsonEscape(screenshot.generic_string()) << "\",\n";
		WriteJsonArray(out, "changedFiles", m_changedFiles);
		const auto withCounts = [this](const std::vector<std::string>& messages)
		{
			std::vector<std::string> result;
			for (const auto& message : messages)
			{
				const int32 count = m_messageCounts[message];
				result.push_back(count > 1 ? message + " (x" + std::to_string(count) + ")" : message);
			}
			return result;
		};
		WriteJsonArray(out, "errors", withCounts(m_loadErrors));
		WriteJsonArray(out, "warnings", withCounts(m_loadWarnings));
		WriteJsonArray(out, "unmockedCalls", m_unmockedCalls);
		WriteJsonArray(out, "undefinedGlobals", m_undefinedGlobals, true);
		out << "}\n";

		m_changedFiles.clear();
	}

	void UiTestApp::UpdateWindowTitle()
	{
		std::ostringstream title;
		title << "UI Test - " << m_options.toc << " - reload #" << m_reloadCount;
		if (!m_loadErrors.empty())
		{
			title << " - " << m_loadErrors.size() << " errors";
		}
		GraphicsDevice::Get().GetAutoCreatedWindow()->SetTitle(title.str());
	}

	void UiTestApp::DrawBackdrop()
	{
		auto& gx = GraphicsDevice::Get();

		int32 width = 0, height = 0;
		gx.GetViewport(nullptr, nullptr, &width, &height);

		if (!m_backdrop || m_backdropWidth != width || m_backdropHeight != height)
		{
			m_backdrop = std::make_unique<GeometryBuffer>();
			m_backdropWidth = static_cast<uint16>(width);
			m_backdropHeight = static_cast<uint16>(height);

			const Rect screen(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
			if (m_backgroundTexture)
			{
				m_backdrop->SetActiveTexture(m_backgroundTexture);
				GeometryHelper::CreateRect(*m_backdrop, 0xFFFFFFFF, screen,
					Rect(0.0f, 0.0f, m_backgroundTexture->GetWidth(), m_backgroundTexture->GetHeight()),
					m_backgroundTexture->GetWidth(), m_backgroundTexture->GetHeight());
			}
			else
			{
				m_backdrop->SetActiveTexture(m_whiteTexture);
				GeometryHelper::CreateRect(*m_backdrop, 0xFF000000 | m_options.backdropColor, screen);
			}
		}

		gx.SetDepthEnabled(false);
		gx.SetDepthWriteEnabled(false);
		gx.SetFaceCullMode(FaceCullMode::None);
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 100.0f));
		m_backdrop->Draw();
	}

	void UiTestApp::EnsureCaptureTarget()
	{
		const auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
		if (m_captureTarget && m_captureTarget->RenderTarget::GetWidth() == window->GetWidth() && m_captureTarget->RenderTarget::GetHeight() == window->GetHeight())
		{
			return;
		}

		m_captureTarget = GraphicsDevice::Get().CreateRenderTexture("UiTestCapture", window->GetWidth(), window->GetHeight(),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
	}

	bool UiTestApp::Capture(const std::filesystem::path& path)
	{
		EnsureCaptureTarget();
		if (!m_captureTarget)
		{
			ELOG("Could not create the capture render target");
			return false;
		}

		m_captureTarget->Activate();
		m_captureTarget->Clear(ClearFlags::All);
		DrawBackdrop();
		if (FrameManager::IsInitialized())
		{
			FrameManager::Get().Draw();
		}

		const uint32 width = m_captureTarget->RenderTarget::GetWidth();
		const uint32 height = m_captureTarget->RenderTarget::GetHeight();
		std::vector<uint8> pixels(m_captureTarget->GetPixelDataSize());
		m_captureTarget->CopyPixelDataTo(pixels.data());
		if (pixels.size() < static_cast<size_t>(width) * height * 4)
		{
			ELOG("Unexpected capture pixel format");
			return false;
		}

		if (!tex::WritePng(path, width, height, pixels.data()))
		{
			ELOG("Could not write " << path.string());
			return false;
		}

		ILOG("Screenshot " << path.string());
		return true;
	}

	void UiTestApp::PollCommands()
	{
		std::error_code ec;
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(m_options.outPath / "commands", ec))
		{
			if (entry.path().extension() == ".cmd")
			{
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());

		for (const auto& file : files)
		{
			CommandBatch batch;
			batch.file = file;

			std::ifstream in(file);
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				// Windows PowerShell writes UTF-8 files with a byte order mark.
				if (batch.lines.empty() && line.rfind("\xEF\xBB\xBF", 0) == 0)
				{
					line.erase(0, 3);
				}
				if (line.find_first_not_of(" \t") != std::string::npos && line[line.find_first_not_of(" \t")] != '#')
				{
					batch.lines.push_back(line);
				}
			}
			in.close();

			// Renamed, so a writer that is slow to close the file cannot get the batch read twice.
			std::filesystem::rename(file, std::filesystem::path(file).replace_extension(".running"), ec);
			batch.file.replace_extension(".running");
			m_batches.push_back(std::move(batch));
		}
	}

	void UiTestApp::ExecuteCommand(const std::string& line)
	{
		const auto tokens = Tokenize(line);
		if (tokens.empty())
		{
			return;
		}

		CommandBatch& batch = m_batches.front();
		const std::string& command = tokens[0];
		FrameManager& frameMgr = FrameManager::Get();

		if (command == "reload")
		{
			Reload("command");
		}
		else if (command == "screenshot")
		{
			m_captures.push_back({ ResolveOutPath(tokens.size() > 1 ? tokens[1] : "screenshot.png"), true });
		}
		else if (command == "lua")
		{
			// Try as an expression first, so "lua CharacterWindow:IsVisible()" reports its value.
			const std::string code = RestOfLine(line);
			const int top = lua_gettop(m_luaState);
			if (luaL_loadstring(m_luaState, ("return " + code).c_str()) != 0)
			{
				lua_pop(m_luaState, 1);
				if (luaL_loadstring(m_luaState, code.c_str()) != 0)
				{
					batch.output.push_back(std::string("error: ") + lua_tostring(m_luaState, -1));
					lua_settop(m_luaState, top);
					return;
				}
			}

			if (lua_pcall(m_luaState, 0, LUA_MULTRET, 0) != 0)
			{
				batch.output.push_back(std::string("error: ") + lua_tostring(m_luaState, -1));
			}
			else
			{
				for (int i = top + 1; i <= lua_gettop(m_luaState); ++i)
				{
					batch.output.push_back("= " + DescribeLuaValue(m_luaState, i));
				}
			}
			lua_settop(m_luaState, top);
		}
		else if (command == "event" && tokens.size() > 1)
		{
			std::string code = "UiTest.FireEvent(" + ToLuaLiteral(tokens[1]);
			for (size_t i = 2; i < tokens.size(); ++i)
			{
				code += ", " + ToLuaLiteral(tokens[i]);
			}
			frameMgr.ExecuteLua(code + ")");
		}
		else if (command == "hover" && tokens.size() > 2)
		{
			frameMgr.NotifyMouseMoved(Point(std::stof(tokens[1]), std::stof(tokens[2])));
		}
		else if (command == "click" && tokens.size() > 2)
		{
			const Point position(std::stof(tokens[1]), std::stof(tokens[2]));
			const MouseButton button = (tokens.size() > 3 && tokens[3] == "right") ? MouseButton::Right : MouseButton::Left;
			frameMgr.NotifyMouseMoved(position);
			frameMgr.NotifyMouseDown(button, position);
			frameMgr.NotifyMouseUp(button, position);
		}
		else if (command == "key" && tokens.size() > 1)
		{
			const int32 key = ParseKey(tokens[1]);
			frameMgr.NotifyKeyDown(key);
			frameMgr.NotifyKeyUp(key);
		}
		else if (command == "type")
		{
			for (const char c : RestOfLine(line))
			{
				frameMgr.NotifyKeyChar(static_cast<uint8>(c));
			}
		}
		else if (command == "wait")
		{
			m_waitFrames = tokens.size() > 1 ? std::max(0, std::atoi(tokens[1].c_str())) : 1;
		}
		else if (command == "resize" && tokens.size() > 2)
		{
			GraphicsDevice::Get().GetAutoCreatedWindow()->Resize(static_cast<uint16>(std::atoi(tokens[1].c_str())), static_cast<uint16>(std::atoi(tokens[2].c_str())));
			m_settleFrames = m_options.settleFrames;
		}
		else if (command == "toc" && tokens.size() > 1)
		{
			m_options.toc = tokens[1];
			Reload("command");
		}
		else if (command == "scenario")
		{
			const std::string name = tokens.size() > 1 ? tokens[1] : "none";
			if (name == "none")
			{
				m_options.scenario.clear();
			}
			else
			{
				std::filesystem::path path = name;
				if (!path.has_extension())
				{
					path = m_options.toolPath / "scenarios" / (name + ".lua");
				}
				m_options.scenario = std::filesystem::absolute(path);
			}
			Reload("command");
		}
		else if (command == "quit")
		{
			FinishBatch();
			EventLoop::Terminate(0);
		}
		else
		{
			batch.output.push_back("error: unknown command '" + command + "'");
		}
	}

	void UiTestApp::FinishBatch()
	{
		if (m_batches.empty())
		{
			return;
		}

		CommandBatch& batch = m_batches.front();
		std::error_code ec;
		{
			std::ofstream out(std::filesystem::path(batch.file).replace_extension(".tmp"), std::ios::out | std::ios::trunc);
			for (const auto& line : batch.output)
			{
				out << line << "\n";
			}
			out << "done\n";
		}

		// The result appears atomically, so a waiting script never reads a half-written file.
		std::filesystem::rename(std::filesystem::path(batch.file).replace_extension(".tmp"), std::filesystem::path(batch.file).replace_extension(".result"), ec);
		std::filesystem::remove(batch.file, ec);
		m_batches.pop_front();
	}

	std::filesystem::path UiTestApp::ResolveOutPath(const std::filesystem::path& path) const
	{
		return path.is_absolute() ? path : m_options.outPath / path;
	}
}
