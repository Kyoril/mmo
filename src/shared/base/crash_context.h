// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>

namespace mmo
{
	/// Records what the current thread is working on, so a crash report can say which player,
	/// packet or world instance was being processed when the process died.
	///
	/// Scopes nest: each one pushes a line onto a per-thread stack on construction and pops it on
	/// destruction. The text is formatted into a fixed buffer up front, so nothing referenced by
	/// it has to outlive the scope and reading it from the crash handler never allocates or
	/// touches game objects that may already be half destroyed.
	///
	/// Construct it on the stack only, and keep it cheap: it runs once per packet and per world
	/// instance tick.
	class CrashContextScope final
	{
	public:
		/// Pushes a context line, formatted printf-style. Lines longer than the internal buffer are
		/// truncated.
		explicit CrashContextScope(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
			__attribute__((format(printf, 2, 3)))
#endif
			;

		/// Pops the line again.
		~CrashContextScope();

		CrashContextScope(const CrashContextScope&) = delete;
		CrashContextScope& operator=(const CrashContextScope&) = delete;

		/// @returns The formatted context line.
		[[nodiscard]] const char* GetText() const { return m_text; }

		/// @returns The enclosing scope of the same thread, or nullptr for the outermost one.
		[[nodiscard]] const CrashContextScope* GetPrevious() const { return m_previous; }

	private:
		CrashContextScope* m_previous;
		char m_text[256];
	};

	/// Appends the current thread's context lines to a crash report's details, outermost first,
	/// as "Context N: <text>". Appends nothing when no scope is active.
	/// @param details Detail lines of the crash report.
	void AppendCrashContext(std::vector<std::string>& details);
}
