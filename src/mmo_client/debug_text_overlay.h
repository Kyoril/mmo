// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "frame_ui/color.h"
#include "frame_ui/font.h"

#include "game_client/debug_interface.h"

#include <string>
#include <vector>
#include <chrono>

namespace mmo
{
	class GeometryBuffer;

	/// Debug text entry that tracks text, color, duration and position
	struct DebugTextEntry
	{
		std::string text;
		Color color;
		std::chrono::steady_clock::time_point endTime;
		uint64 tag;
		
		explicit DebugTextEntry(std::string text, const Color& color, float duration, uint64 tag = 0)
			: text(std::move(text))
			, color(color)
			, endTime(std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(duration * 1000.0f)))
			, tag(tag)
		{
		}
		
		bool IsExpired() const
		{
			return std::chrono::steady_clock::now() >= endTime;
		}
	};

	/// This class provides debug text overlay functionality similar to Unreal Engine's OutputString.
	/// It renders debug text in the upper-left corner of the screen with configurable duration,
	/// color, and positioning.
	class DebugTextOverlay final
		: public DebugInterface
	{
		friend std::unique_ptr<DebugTextOverlay> std::make_unique<DebugTextOverlay>();
	private:
		/// Private constructor to enforce singleton pattern
		DebugTextOverlay() = default;

	public:
		/// Initializes the debug text overlay system
		static void Initialize();
		
		/// Destroys the debug text overlay system  
		static void Destroy();

		/// @brief Gets the singleton instance for interface implementation
		/// @return Pointer to the singleton instance
		static DebugTextOverlay* GetInstance();

	private:
		/// Paint function called by the screen layer system
		static void Paint();
		
		/// Updates the debug text list, removing expired entries
		static void UpdateEntries();

	public:
		void OutputString(const std::string& text, float duration, const Color& color, uint64 tag) override;
		void ClearAll() override;
		void ClearTag(uint64 tag) override;
	};
}
