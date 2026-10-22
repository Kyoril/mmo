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

		/// @brief How RenderPageColor renders and encodes the colour.
		struct ColorOptions
		{
			/// @brief Encode the colour with pow(1/2.2). False (the default, used by the .tlod bake) stores
			///        saturate(albedo + emissive) as is, which is what a material sampling the texture (no sRGB
			///        decode) writes back.
			bool srgb = false;

			/// @brief Draw the page water as the opaque minimap water. False hides the water so the ground
			///        under it is rendered.
			bool water = true;
		};

		/// @param pumpStreaming Runs one queued main-thread step of the owner's page streaming and returns
		///        whether there was one. Pages the streaming is preparing when the bake reaches them
		///        complete on that queue.
		/// @param textureSize Edge length of the rendered colour in texels (TextureSize for the .tlod bake).
		TerrainLodBaker(Scene& scene, terrain::Terrain& terrain, std::function<bool()> pumpStreaming, uint32 textureSize = TextureSize);

		~TerrainLodBaker() override;

	public:
		/// @brief Bakes every page that has a .tile file.
		/// @param onlyStale Skip pages without unsaved changes whose baked files are newer than their .tile.
		/// @remark Pages that are not loaded are loaded for the bake and released afterwards. Renders
		///         with the graphics device directly; call it outside of any other render pass.
		Stats BakeAll(bool onlyStale);

		/// @brief Renders one page's top-down colour the way the bake does, without writing anything.
		/// @param x Page X index.
		/// @param z Page Z index.
		/// @param srgb Receives textureSize² RGBA8 texels, encoded as options say; texel (u, v) maps to page-local (x, z).
		/// @param options Encoding and water.
		/// @return False when the page has no .tile file, cannot be loaded or the read-back failed.
		/// @remark Loads the page for the render and releases it again, like BakeAll.
		bool RenderPageColor(uint32 x, uint32 z, std::vector<uint8>& srgb, const ColorOptions& options = ColorOptions{});

	private:
		bool IsUpToDate(uint32 x, uint32 z) const;

		bool BakePage(terrain::Page& page, uint32 x, uint32 z);

		bool RenderColor(terrain::Page& page, std::vector<uint8>& srgb, const ColorOptions& options);

	private:
		Scene& m_scene;
		terrain::Terrain& m_terrain;
		std::function<bool()> m_pumpStreaming;
		uint32 m_textureSize = TextureSize;
		std::unique_ptr<DeferredRenderer> m_renderer;
		Camera* m_camera = nullptr;
		SceneNode* m_cameraNode = nullptr;
	};
}
