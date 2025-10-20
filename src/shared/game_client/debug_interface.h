// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "frame_ui/color.h"

#include <string>

namespace mmo
{
	/// @brief Global debug interface for rendering debug strings on screen.
	/// This interface provides functionality similar to Unreal Engine's OutputString,
	/// allowing debug text to be displayed with colors, duration, and optional tags.
	class DebugInterface : public NonCopyable
	{
	public:
		/// @brief Outputs a debug string to the screen with optional parameters.
		/// @param text The text to display on screen
		/// @param duration How long to display the text in seconds (default: 2.0)
		/// @param color The color of the text (default: white)  
		/// @param tag Optional tag to group similar debug messages. If tag is 0, no grouping is applied.
		///             If tag is non-zero, only one message with that tag will be visible at a time.
		virtual void OutputString(
			const std::string& text,
			float duration = 2.0f,
			const Color& color = Color(0.0f, 0.66f, 1.0f),
			uint64 tag = 0) = 0;

		/// @brief Clears all debug text entries immediately.
		virtual void ClearAll() = 0;

		/// @brief Clears all debug text entries with a specific tag.
		/// @param tag The tag to clear. If 0, does nothing.
		virtual void ClearTag(uint64 tag) = 0;

	protected:
		/// @brief Protected constructor to prevent direct instantiation.
		DebugInterface() = default;

	public:
		/// @brief Virtual destructor.
		virtual ~DebugInterface() = default;
	};

	/// @brief Global accessor for the debug interface.
	/// @return Pointer to the global debug interface instance, or nullptr if not initialized.
	DebugInterface* GetDebugInterface();

	/// @brief Sets the global debug interface instance.
	/// @param debugInterface Pointer to the debug interface implementation.
	void SetDebugInterface(DebugInterface* debugInterface);
}

/// @brief Convenience macros for debug output that are only active in debug builds.
#ifdef _DEBUG
#	define DEBUG_OUTPUT_STRING(text) \
		do { auto* debug = mmo::GetDebugInterface(); if (debug) debug->OutputString(text); } while(0)
#	define DEBUG_OUTPUT_STRING_EX(text, duration, color) \
		do { auto* debug = mmo::GetDebugInterface(); if (debug) debug->OutputString(text, duration, color); } while(0)
#	define DEBUG_OUTPUT_STRING_TAGGED(text, duration, color, tag) \
		do { auto* debug = mmo::GetDebugInterface(); if (debug) debug->OutputString(text, duration, color, tag); } while(0)
#	define DEBUG_CLEAR_ALL() \
		do { auto* debug = mmo::GetDebugInterface(); if (debug) debug->ClearAll(); } while(0)
#	define DEBUG_CLEAR_TAG(tag) \
		do { auto* debug = mmo::GetDebugInterface(); if (debug) debug->ClearTag(tag); } while(0)
#else
#	define DEBUG_OUTPUT_STRING(text) (void)0
#	define DEBUG_OUTPUT_STRING_EX(text, duration, color) (void)0
#	define DEBUG_OUTPUT_STRING_TAGGED(text, duration, color, tag) (void)0
#	define DEBUG_CLEAR_ALL() (void)0
#	define DEBUG_CLEAR_TAG(tag) (void)0
#endif