// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>
#include <vector>

namespace mmo
{
	class MovableObject;
	class SceneNode;

	namespace terrain
	{
		class Page;
	}

	/// @brief Helpers shared by the editor's per-page bakers (terrain LOD, minimap).
	namespace page_bake
	{
		/// @brief IEEE 754 half to float (the G-buffer targets are RGBA16F).
		float HalfToFloat(uint16 half);

		/// @brief Box-filters a square RGBA8 image to half its size.
		std::vector<uint8> Downsample(const std::vector<uint8>& source, uint32 size);

		/// @brief Writes a square RGBA8 image as a DXT1 .htex with a mip chain down to 4x4.
		bool WriteDxt1Texture(const String& filename, std::vector<uint8> rgba, uint32 size);

		/// @brief Hides every visible movable object outside the given scene subtrees and restores it on
		///	       destruction. Cameras are left alone.
		class ScopedVisibleSubtrees final
		{
		public:
			ScopedVisibleSubtrees(SceneNode& root, std::vector<const SceneNode*> keep);

			~ScopedVisibleSubtrees();

			ScopedVisibleSubtrees(const ScopedVisibleSubtrees&) = delete;
			ScopedVisibleSubtrees& operator=(const ScopedVisibleSubtrees&) = delete;

		private:
			void Hide(SceneNode& node);

		private:
			std::vector<const SceneNode*> m_keep;
			std::vector<MovableObject*> m_hidden;
		};

		/// @brief Makes a terrain page renderable for a bake and puts it back the way the editor's
		///	       streaming had it on destruction.
		class ScopedPageLoad final
		{
		public:
			/// @param pumpStreaming Runs one queued main-thread step of the owner's page streaming and
			///        returns whether there was one. Pages the streaming is preparing complete on that queue.
			ScopedPageLoad(terrain::Page& page, uint32 x, uint32 z, const std::function<bool()>& pumpStreaming);

			~ScopedPageLoad();

			ScopedPageLoad(const ScopedPageLoad&) = delete;
			ScopedPageLoad& operator=(const ScopedPageLoad&) = delete;

			/// @brief Whether the page is loaded and can be rendered.
			[[nodiscard]] bool IsLoaded() const { return m_loaded; }

		private:
			terrain::Page& m_page;
			bool m_wasPrepared = false;
			bool m_wasLoaded = false;
			bool m_loaded = false;
			bool m_touched = false;
		};
	}
}
