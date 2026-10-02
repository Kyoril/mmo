// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <functional>
#include <memory>
#include <vector>

namespace mmo
{
	class Camera;
	class DeferredRenderer;
	class Scene;
	class SceneNode;

	namespace terrain
	{
		class Page;
		class Terrain;
	}

	/// @brief Bakes the distant-terrain data of a world's terrain pages: the .tlod geometry and the
	///	       top-down, unlit colour texture the client's FarTerrain draws beyond the streamed pages.
	/// @details The colour is the G-buffer albedo (+ emissive, which is where unlit materials such as
	///	         the minimap water put their colour) of an orthographic top-down render of the page with
	///	         everything but the terrain hidden, so the client can light it like any other surface.
	class TerrainLodBaker final : public NonCopyable
	{
	public:
		/// @brief Edge length of the baked colour texture in texels (about 2 m per texel).
		static constexpr uint32 TextureSize = 256;

		struct Stats
		{
			uint32 baked = 0;
			uint32 upToDate = 0;
			uint32 failed = 0;
		};

		/// @param pumpStreaming Runs one queued main-thread step of the owner's page streaming and returns
		///        whether there was one. Pages the streaming is preparing when the bake reaches them
		///        complete on that queue.
		TerrainLodBaker(Scene& scene, terrain::Terrain& terrain, std::function<bool()> pumpStreaming);

		~TerrainLodBaker() override;

	public:
		/// @brief Bakes every page that has a .tile file.
		/// @param onlyStale Skip pages without unsaved changes whose baked files are newer than their .tile.
		/// @remark Pages that are not loaded are loaded for the bake and released afterwards. Renders
		///         with the graphics device directly; call it outside of any other render pass.
		Stats BakeAll(bool onlyStale);

	private:
		bool IsUpToDate(uint32 x, uint32 z) const;

		bool BakePage(terrain::Page& page, uint32 x, uint32 z);

		bool RenderColor(terrain::Page& page, std::vector<uint8>& srgb);

	private:
		Scene& m_scene;
		terrain::Terrain& m_terrain;
		std::function<bool()> m_pumpStreaming;
		std::unique_ptr<DeferredRenderer> m_renderer;
		Camera* m_camera = nullptr;
		SceneNode* m_cameraNode = nullptr;
	};
}
