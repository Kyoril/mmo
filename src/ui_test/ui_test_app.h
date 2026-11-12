// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "file_watcher.h"
#include "ui_test_options.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "frame_ui/geometry_buffer.h"
#include "frame_ui/localization.h"
#include "graphics/render_texture.h"

#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct lua_State;

namespace mmo
{
	class LogEntry;

	/// @brief Hosts the frame UI outside the game client.
	///
	/// Every (re)load builds a fresh Lua state and FrameManager, runs the Lua mocks that stand in for
	/// the client's game API, loads the TOC, runs the optional scenario and, once the UI has settled
	/// for a few frames, writes a screenshot and status.json. In live mode, changed UI, mock or
	/// scenario files trigger the same reload, and command files dropped into <out>/commands drive
	/// the running app (screenshots, Lua, events, mouse input).
	class UiTestApp final : public NonCopyable
	{
	public:
		explicit UiTestApp(UiTestOptions options);
		~UiTestApp();

	public:
		/// @brief Creates the device and window and loads the UI.
		bool Initialize();

		/// @brief Runs until the window is closed (live mode) or the one-shot screenshot was written.
		/// @return Process exit code.
		int32 Run();

		/// @brief Releases the UI, Lua state and graphics.
		void Shutdown();

	public:
		/// @brief Called by Lua (UiTest.ReportUndefined) when the UI reads an undefined global.
		void ReportUndefinedGlobal(const std::string& name);

		/// @brief Called by Lua (UiTest.ReportUnmocked) when the UI calls a client API function that only has a stub.
		void ReportUnmockedCall(const std::string& name);

		/// @brief Queues a screenshot of the next settled frame.
		void RequestScreenshot(const std::filesystem::path& path);

	private:
		/// One file from the commands inbox, executed line by line.
		struct CommandBatch
		{
			std::filesystem::path file;
			std::vector<std::string> lines;
			size_t next = 0;
			std::vector<std::string> output;
		};

		/// A screenshot waiting for the next settled frame.
		struct CaptureRequest
		{
			std::filesystem::path path;
			bool reportToBatch = false;
		};

	private:
		void OnIdle(float deltaSeconds);
		void OnPaint();
		void OnLogEntry(const LogEntry& entry);

		/// Tears down the current UI (if any) and loads it again from disk.
		void Reload(const std::string& reason);
		void UnloadUi();
		void LoadUi();
		void RegisterLuaApi();
		void RunLuaFile(const std::filesystem::path& file);
		void SetupWatcher();
		void WriteStatus(const std::filesystem::path& screenshot);
		void UpdateWindowTitle();

		void DrawBackdrop();
		bool Capture(const std::filesystem::path& path);
		void EnsureCaptureTarget();

		void PollCommands();
		void ExecuteCommand(const std::string& line);
		void FinishBatch();

		std::filesystem::path ResolveOutPath(const std::filesystem::path& path) const;

	private:
		UiTestOptions m_options;
		lua_State* m_luaState = nullptr;
		std::unique_ptr<Localization> m_localization;
		scoped_connection_container m_connections;
		scoped_connection m_logConnection;
		std::ofstream m_logFile;

		FileWatcher m_watcher;
		float m_watchTimer = 0.0f;
		float m_reloadDelay = -1.0f;
		std::string m_pendingReloadReason;

		int32 m_reloadCount = 0;
		int32 m_settleFrames = 0;
		bool m_statusPending = false;
		float m_lastLoadMs = 0.0f;
		std::vector<std::string> m_loadErrors;
		std::vector<std::string> m_loadWarnings;
		std::map<std::string, int32> m_messageCounts;
		std::set<std::string> m_undefinedGlobals;
		std::set<std::string> m_unmockedCalls;
		std::vector<std::string> m_changedFiles;

		std::deque<CaptureRequest> m_captures;
		RenderTexturePtr m_captureTarget;
		std::unique_ptr<GeometryBuffer> m_backdrop;
		TexturePtr m_whiteTexture;
		TexturePtr m_backgroundTexture;
		uint16 m_backdropWidth = 0;
		uint16 m_backdropHeight = 0;

		std::deque<CommandBatch> m_batches;
		float m_commandTimer = 0.0f;
		int32 m_waitFrames = 0;

		bool m_oneShotDone = false;
		int32 m_exitCode = 0;
	};
}
