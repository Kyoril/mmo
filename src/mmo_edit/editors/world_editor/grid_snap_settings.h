// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include <array>

namespace mmo
{
	/// @brief Encapsulates grid snapping settings for the world editor.
	/// Manages snap enable/disable state and configurable snap sizes for translation,
	/// rotation, and scale operations.
	class GridSnapSettings final
	{
	public:
		/// @brief Constructs grid snap settings with default values.
		GridSnapSettings()
			: m_enabled(true)
			, m_currentTranslateIndex(3)
			, m_currentRotateIndex(3)
		{
		}

	public:
		/// @brief Checks if grid snapping is currently enabled.
		/// @return True if snapping is enabled, false otherwise.
		[[nodiscard]] bool IsEnabled() const
		{
			return m_enabled;
		}

		/// @brief Enables or disables grid snapping.
		/// @param enabled True to enable snapping, false to disable.
		void SetEnabled(const bool enabled)
		{
			m_enabled = enabled;
		}

		/// @brief Gets the current translation snap size in world units.
		/// @return The current translation snap size.
		[[nodiscard]] float GetCurrentTranslateSnap() const
		{
			return m_translateSizes[m_currentTranslateIndex];
		}

		/// @brief Gets the current rotation snap size in degrees.
		/// @return The current rotation snap size.
		[[nodiscard]] float GetCurrentRotateSnap() const
		{
			return m_rotateSizes[m_currentRotateIndex];
		}

		/// @brief Gets the current index for translation snap sizes.
		/// @return The current translation snap index.
		[[nodiscard]] int GetCurrentTranslateIndex() const
		{
			return m_currentTranslateIndex;
		}

		/// @brief Gets the current index for rotation snap sizes.
		/// @return The current rotation snap index.
		[[nodiscard]] int GetCurrentRotateIndex() const
		{
			return m_currentRotateIndex;
		}

		/// @brief Sets the current translation snap size by index.
		/// @param index Index into the translate sizes array.
		void SetCurrentTranslateIndex(const int index)
		{
			if (index >= 0 && index < static_cast<int>(m_translateSizes.size()))
			{
				m_currentTranslateIndex = index;
			}
		}

		/// @brief Sets the current rotation snap size by index.
		/// @param index Index into the rotate sizes array.
		void SetCurrentRotateIndex(const int index)
		{
			if (index >= 0 && index < static_cast<int>(m_rotateSizes.size()))
			{
				m_currentRotateIndex = index;
			}
		}

		/// @brief Gets all available translation snap sizes.
		/// @return Array of translation snap sizes.
		[[nodiscard]] const std::array<float, 7>& GetTranslateSizes() const
		{
			return m_translateSizes;
		}

		/// @brief Gets all available rotation snap sizes.
		/// @return Array of rotation snap sizes.
		[[nodiscard]] const std::array<float, 6>& GetRotateSizes() const
		{
			return m_rotateSizes;
		}

		/// @brief Gets string labels for translation snap sizes (for UI).
		/// @return Array of string labels.
		[[nodiscard]] static const std::array<const char*, 7>& GetTranslateSizeLabels()
		{
			static const std::array<const char*, 7> labels = { "0.1", "0.25", "0.5", "1.0", "1.5", "2.0", "4.0" };
			return labels;
		}

		/// @brief Gets string labels for rotation snap sizes (for UI).
		/// @return Array of string labels.
		[[nodiscard]] static const std::array<const char*, 6>& GetRotateSizeLabels()
		{
			static const std::array<const char*, 6> labels = { "1", "5", "10", "15", "45", "90" };
			return labels;
		}

	private:
		bool m_enabled;
		std::array<float, 7> m_translateSizes = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
		std::array<float, 6> m_rotateSizes = { 1.0f, 5.0f, 10.0f, 15.0f, 45.0f, 90.0f };
		int m_currentTranslateIndex;
		int m_currentRotateIndex;
	};
}
