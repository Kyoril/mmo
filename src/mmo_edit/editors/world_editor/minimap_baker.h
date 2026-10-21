// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace mmo
{
	class Camera;
	class DeferredRenderer;
	class MovableObject;
	class Scene;
	class SceneNode;

	namespace terrain
	{
		class Page;
		class Terrain;
	}

	/// @brief Bakes the minimap tiles of a world (Textures/Minimaps/<world>/<pageIndex>.htex).
	/// @details Each page is rendered top-down into a G-buffer at RenderSize, once with only the terrain
	///	         and once with the world models (WMOs) added, both without water. The CPU then lights the
	///	         albedo with fixed relief lighting from the G-buffer normals and paints the liquids
	///	         analytically from the page's water data (depth-tinted body, rounded shoreline, foam, wet
	///	         banks, per-type looks; see terrain/minimap_shading.h). World models hide the water under
	///	         them, so bridges and docks stay on top. The result is box-filtered to OutputSize and
	///	         written as a mip-mapped DXT1 texture.
	class MinimapBaker final : public NonCopyable
	{
	public:
		/// @brief Edge length the page is rendered and shaded at (about 0.5 m per pixel).
		static constexpr uint32 RenderSize = 1024;

		/// @brief Edge length of the written texture (about 1 m per texel).
		static constexpr uint32 OutputSize = 512;

		struct Stats
		{
			uint32 baked = 0;
			uint32 upToDate = 0;
			uint32 failed = 0;
		};

		/// @brief An inclusive rectangle of page coordinates; the default covers every page.
		struct Region
		{
			uint32 minX = 0;
			uint32 minZ = 0;
			uint32 maxX = std::numeric_limits<uint32>::max();
			uint32 maxZ = std::numeric_limits<uint32>::max();

			[[nodiscard]] bool Contains(const uint32 x, const uint32 z) const
			{
				return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
			}
		};

		/// @brief Given a page's coordinates, returns the scene nodes of the world models to draw around it.
		using ModelProvider = std::function<std::vector<const SceneNode*>(uint32 x, uint32 z)>;

		/// @param worldName The world's name, used as the minimap folder name.
		/// @param prepareModels Called before each page with its coordinates; makes sure the world models
		///        around it exist and returns their scene nodes (drawn with their whole subtree). May be empty.
		/// @param pumpStreaming Runs one queued main-thread step of the owner's page streaming and returns
		///        whether there was one. Pages the streaming is preparing when the bake reaches them
		///        complete on that queue.
		MinimapBaker(Scene& scene, terrain::Terrain& terrain, String worldName, ModelProvider prepareModels, std::function<bool()> pumpStreaming);

		~MinimapBaker() override;

	public:
		/// @brief Bakes every page that has a .tile file.
		/// @param onlyStale Skip pages without unsaved changes whose minimap is newer than their .tile.
		///        World model edits do not touch the .tile, so a full bake is the safe default.
		/// @param region Only pages inside this inclusive page rectangle are baked.
		Stats BakeAll(bool onlyStale, const Region& region = Region{});

		/// @brief The minimap texture filename of a page.
		static String GetTileFilename(const String& worldName, uint32 x, uint32 z);

	private:
		struct GBufferCopy
		{
			std::vector<uint16> albedo;
			std::vector<uint16> normal;
			std::vector<uint16> emissive;
		};

		bool IsUpToDate(uint32 x, uint32 z) const;

		bool BakePage(terrain::Page& page, uint32 x, uint32 z);

		bool Render(terrain::Page& page, GBufferCopy& out);

		void Compose(const terrain::Page& page, const GBufferCopy& withModels, const GBufferCopy& terrainOnly, std::vector<uint8>& srgb) const;

		void SetModelsVisible(bool visible);

	private:
		Scene& m_scene;
		terrain::Terrain& m_terrain;
		String m_worldName;
		ModelProvider m_prepareModels;
		std::vector<MovableObject*> m_modelObjects;
		std::function<bool()> m_pumpStreaming;
		std::unique_ptr<DeferredRenderer> m_renderer;
		Camera* m_camera = nullptr;
		SceneNode* m_cameraNode = nullptr;
	};
}
