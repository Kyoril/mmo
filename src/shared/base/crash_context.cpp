// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "crash_context.h"

#include <cstdarg>
#include <cstdio>

namespace mmo
{
	namespace
	{
		/// Innermost active scope of this thread. Per thread because the login server runs two io
		/// threads, and the crash handler runs on the thread that crashed.
		thread_local CrashContextScope* t_innermost = nullptr;
	}

	CrashContextScope::CrashContextScope(const char* format, ...)
		: m_previous(t_innermost)
	{
		va_list args;
		va_start(args, format);
		const int written = std::vsnprintf(m_text, sizeof(m_text), format, args);
		va_end(args);

		if (written < 0)
		{
			m_text[0] = 0;
		}

		t_innermost = this;
	}

	CrashContextScope::~CrashContextScope()
	{
		t_innermost = m_previous;
	}

	void AppendCrashContext(std::vector<std::string>& details)
	{
		// The stack links innermost to outermost; the report reads better the other way round.
		std::vector<const CrashContextScope*> scopes;
		for (const CrashContextScope* scope = t_innermost; scope != nullptr; scope = scope->GetPrevious())
		{
			scopes.push_back(scope);
		}

		for (size_t i = 0; i < scopes.size(); ++i)
		{
			details.push_back("Context " + std::to_string(i) + ": " + scopes[scopes.size() - 1 - i]->GetText());
		}
	}
}
