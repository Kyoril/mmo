// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "debug_text_overlay.h"
#include "screen.h"

#include "frame_ui/font_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/graphics_device.h"

#include <algorithm>
#include <mutex>
#include <sstream>


namespace mmo
{
	// Static member variables
	static ScreenLayerIt s_debugTextLayer;
	static FontPtr s_debugFont;
	static std::unique_ptr<GeometryBuffer> s_debugTextGeometry;
	static std::vector<DebugTextEntry> s_debugTextEntries;
	static std::mutex s_debugTextMutex;
	static bool s_debugTextDirty = true;
	static float s_movementDebugYOffset = 10.0f;
	static float s_lineHeight = 16.0f;
	static std::unique_ptr<DebugTextOverlay> s_instance;

	void DebugTextOverlay::Initialize()
	{
		// Initialize the debug text geometry buffer
		s_debugTextGeometry = std::make_unique<GeometryBuffer>();

		// Get the console font (reuse the same font used by console)
		s_debugFont = FontManager::Get().CreateOrRetrieve("Fonts/consola.ttf", 12, 0, 1, 1);
		if (!s_debugFont)
		{
			// Fallback to a default font if console font is not available
			s_debugFont = FontManager::Get().CreateOrRetrieve("Fonts/arial.ttf", 12, 0, 1, 1);
		}

		// Flags for the screen layer
		uint32 flags = ScreenLayerFlags::IdentityProjection | ScreenLayerFlags::IdentityTransform;
#ifndef _DEBUG
		// In release builds, disable the debug text layer by default
		flags |= ScreenLayerFlags::Disabled;
#endif

		// Register the screen layer for rendering debug text
		s_debugTextLayer = Screen::AddLayer(
			&DebugTextOverlay::Paint,
			900.0f, // High priority to render on top of most things
			flags);
		
		// Get height
		s_lineHeight = s_debugFont->GetHeight();

		SetDebugInterface(GetInstance());
	}

	void DebugTextOverlay::Destroy()
	{
		// Remove the screen layer
		Screen::RemoveLayer(s_debugTextLayer);

		// Clear all debug text entries
		{
			std::lock_guard lock(s_debugTextMutex);
			s_debugTextEntries.clear();
		}

		// Reset geometry buffer and font
		s_debugTextGeometry.reset();
		s_debugFont.reset();

		// Clear instance
		s_instance.reset();
	}

	DebugTextOverlay* DebugTextOverlay::GetInstance()
	{
		std::lock_guard lock(s_debugTextMutex);
		if (!s_instance)
		{
			s_instance = std::make_unique<DebugTextOverlay>();
		}

		return s_instance.get();
	}

	void DebugTextOverlay::OutputString(
		const std::string& text,
		float duration,
		const Color& color,
		uint64 tag)
	{
		if (s_debugTextLayer->flags & ScreenLayerFlags::Disabled)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(s_debugTextMutex);

		// If tag is non-zero, remove any existing entry with the same tag
		if (tag != 0)
		{
			// Try to refresh the entry first if available
			const auto it = std::find_if(s_debugTextEntries.begin(), s_debugTextEntries.end(),
				[tag](const DebugTextEntry& entry) { return entry.tag == tag; });
			if (it != s_debugTextEntries.end())
			{
				it->endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(duration * 1000.0f));
				it->text = text;
				it->color = color;
				s_debugTextDirty = true;
				return;
			}
		}

		// Add the new debug text entry
		s_debugTextEntries.emplace_back(text, color, duration, tag);
		s_debugTextDirty = true;
	}

	void DebugTextOverlay::ClearAll()
	{
		std::lock_guard lock(s_debugTextMutex);
		s_debugTextEntries.clear();
		s_debugTextDirty = true;
		s_movementDebugYOffset = 10.0f; // Reset Y offset
	}

	void DebugTextOverlay::ClearTag(uint64 tag)
	{
		if (tag == 0)
		{
			return; // No tag to clear
		}

		std::lock_guard lock(s_debugTextMutex);

		const auto it = std::remove_if(s_debugTextEntries.begin(), s_debugTextEntries.end(),
			[tag](const DebugTextEntry& entry) { return entry.tag == tag; });

		if(it != s_debugTextEntries.end())
		{
			s_debugTextEntries.erase(it, s_debugTextEntries.end());
			s_debugTextDirty = true;
		}
	}

	void DebugTextOverlay::Paint()
	{
		// Early exit if no font is available
		if (!s_debugFont || !s_debugTextGeometry)
		{
			return;
		}

		// Check if we have anything to render
		{
			std::lock_guard<std::mutex> lock(s_debugTextMutex);
			if (s_debugTextEntries.empty())
			{
				return;
			}

			// Only rebuild geometry if something changed
			if (s_debugTextDirty)
			{
				s_debugTextGeometry->Reset();

				float y = 10.0f;

				// Render all active debug text entries
				for (const auto& entry : s_debugTextEntries)
				{
					const Point position(10.0f, y);
					s_debugFont->DrawText(entry.text, position, *s_debugTextGeometry, 1.0f, entry.color.GetARGB());

					y += s_lineHeight;
				}
				
				s_debugTextDirty = false;
			}
		}

		// Set up rendering state
		auto& gx = GraphicsDevice::Get();
		
		// Get viewport dimensions for orthographic projection
		int32 vpWidth = 0, vpHeight = 0;
		gx.GetViewport(nullptr, nullptr, &vpWidth, &vpHeight);

		// Set up orthographic projection for screen-space rendering
		gx.SetTransformMatrix(TransformType::Projection, 
			gx.MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(vpWidth), static_cast<float>(vpHeight), 0.0f, 100.0f));
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		
		// Set rendering states for text
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetBlendMode(BlendMode::Alpha);

		// Draw the debug text geometry
		s_debugTextGeometry->Draw();

		// Update entries (remove expired ones)
		UpdateEntries();
	}

	void DebugTextOverlay::UpdateEntries()
	{
		std::lock_guard<std::mutex> lock(s_debugTextMutex);
		
		// Remove expired entries
		auto it = std::remove_if(s_debugTextEntries.begin(), s_debugTextEntries.end(),
			[](const DebugTextEntry& entry) { return entry.IsExpired(); });
		
		if (it != s_debugTextEntries.end())
		{
			s_debugTextEntries.erase(it, s_debugTextEntries.end());
			s_debugTextDirty = true;
		}
	}
}
