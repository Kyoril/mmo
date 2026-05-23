// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// movement_log.h
//
// Lightweight, file-backed movement event logger for the client.
// Enable via the "LogMovement" console variable (set to 1).
// Log is written to "Logs/Movement.log" and flushed on every write so
// the file is readable even while the client is running.

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>

namespace mmo
{
	/// @brief Singleton movement log that writes timestamped entries to disk.
	///
	/// Thread-safe (all public methods take the internal mutex).
	/// Call Open() once when the CVar is turned on, Close() when it is turned off.
	class MovementLog final
	{
	public:
		/// @brief Returns the singleton instance.
		static MovementLog& Get();

	public:
		/// @brief Opens the log file at the given path.
		/// @param path  Relative or absolute path, e.g. "Logs/Movement.log".
		///              The file is truncated on open so each session is fresh.
		/// @return true on success.
		bool Open(const std::string& path);

		/// @brief Closes the log file and flushes pending data.
		void Close();

		/// @brief Returns true if the log file is currently open.
		[[nodiscard]] bool IsOpen() const;

		/// @brief Writes a single line to the log.
		/// @param category  Short tag, e.g. "MOVE_START", "POS_SET", "HEARTBEAT".
		/// @param message   Arbitrary detail string.
		void Write(const char* category, const std::string& message);

	private:
		MovementLog() = default;
		~MovementLog() { Close(); }

		MovementLog(const MovementLog&) = delete;
		MovementLog& operator=(const MovementLog&) = delete;

	private:
		mutable std::mutex m_mutex;
		std::ofstream m_file;
	};

	/// @brief Convenience macro — writes only when the log is open.
	/// Usage:  MOVEMENT_EVENT("SLIDE", "pos=(" << x << "," << y << "," << z << ") normal=...");
#define MOVEMENT_EVENT(category, msg_stream) \
	do { \
		auto& _ml = ::mmo::MovementLog::Get(); \
		if (_ml.IsOpen()) { \
			std::ostringstream _oss; \
			_oss << msg_stream; \
			_ml.Write((category), _oss.str()); \
		} \
	} while (false)

} // namespace mmo
