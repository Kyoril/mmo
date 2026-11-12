// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path_builder.h"

#include "editor_texture_layers.h"
#include "section_bake_job.h"
#include "editors/world_editor/edit_modes/world_edit_mode.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

namespace mmo
{
	const char* const SectionPathLabel = "SectionPath";
	const char* const SectionAssetFolder = "Editor/Pcg/Sections/";

	namespace
	{
		/// Skipped (tile, layer) pairs listed in a warning before the rest is only counted.
		constexpr size_t MaxListedSkips = 24;

		/// Maps section paint layers to the layers of the tile under a world position by texture key
		/// (EditorTextureLayers), caching the result per tile and remembering every painted section layer a tile lacks.
		class TileLayerRemap final
		{
		public:
			TileLayerRemap(terrain::Terrain& terrain, const pcg::SectionAsset& asset)
				: m_layers(terrain)
				, m_layerTextures(asset.layerTextures)
			{
				// Only layers the section actually paints can be skipped.
				for (const pcg::SectionVariant& variant : asset.variants)
				{
					const pcg::SectionPaint& paint = variant.paint;
					const size_t count = std::min(paint.packed.size(), paint.coverage.size());
					for (size_t i = 0; i < count; ++i)
					{
						if (paint.coverage[i] == 0)
						{
							continue;
						}

						for (size_t layer = 0; layer < 4; ++layer)
						{
							if (((paint.packed[i] >> (layer * 8)) & 0xFFu) != 0)
							{
								m_painted[layer] = true;
							}
						}
					}
				}
			}

			std::array<int8, 4> Get(const float x, const float z)
			{
				int32 tileX = 0, tileZ = 0;
				if (!m_layers.GetTileIndex(x, z, tileX, tileZ))
				{
					return { -1, -1, -1, -1 };
				}

				const auto key = std::make_pair(tileX, tileZ);
				const auto cached = m_cache.find(key);
				if (cached != m_cache.end())
				{
					return cached->second;
				}

				std::array<int8, 4> map{ -1, -1, -1, -1 };
				for (size_t layer = 0; layer < 4; ++layer)
				{
					map[layer] = m_layers.FindLayer(tileX, tileZ, m_layerTextures[layer]);
					if (map[layer] < 0 && m_painted[layer])
					{
						m_skipped.emplace(tileX, tileZ, static_cast<uint32>(layer));
					}
				}

				m_cache.emplace(key, map);
				return map;
			}

			/// One warning listing the skipped (tile, layer) pairs, or empty.
			String DescribeSkipped() const
			{
				if (m_skipped.empty())
				{
					return String();
				}

				std::ostringstream stream;
				stream << "Paint skipped for " << m_skipped.size() << " (tile, layer) pair(s) whose tile material has no layer with the section layer's texture:";
				size_t listed = 0;
				for (const auto& [tileX, tileZ, layer] : m_skipped)
				{
					if (listed == MaxListedSkips)
					{
						stream << " and " << (m_skipped.size() - listed) << " more";
						break;
					}
					stream << (listed == 0 ? " " : ", ") << "tile (" << tileX << ", " << tileZ << ") layer " << layer;
					++listed;
				}
				stream << ".";
				return stream.str();
			}

			/// Every skipped pair, for the log.
			String DescribeAllSkipped() const
			{
				std::ostringstream stream;
				for (const auto& [tileX, tileZ, layer] : m_skipped)
				{
					stream << "\n  tile (" << tileX << ", " << tileZ << ") layer " << layer << " (" << m_layerTextures[layer] << ")";
				}
				return stream.str();
			}

		private:
			EditorTextureLayers m_layers;
			std::array<String, 4> m_layerTextures;
			std::array<bool, 4> m_painted{};
			std::map<std::pair<int32, int32>, std::array<int8, 4>> m_cache;
			std::set<std::tuple<int32, int32, uint32>> m_skipped;
		};
	}

	std::vector<String> ListSectionAssets()
	{
		const String prefix = SectionAssetFolder;
		std::vector<String> result;
		for (const String& file : AssetRegistry::ListFiles(prefix, ".hsec"))
		{
			// Only the baked assets directly in the folder (section worlds live in sub folders).
			if (file.size() > prefix.size() && file.find('/', prefix.size()) == String::npos)
			{
				result.push_back(file);
			}
		}

		// Biome sections: Editor/Pcg/Biomes/<Biome>/Sections/<Name>.hsec.
		const String biomePrefix = "Editor/Pcg/Biomes/";
		const String sectionsPart = "/Sections/";
		for (const String& file : AssetRegistry::ListFiles(biomePrefix, ".hsec"))
		{
			const size_t biomeEnd = file.find('/', biomePrefix.size());
			if (biomeEnd == String::npos || biomeEnd == biomePrefix.size() || file.compare(biomeEnd, sectionsPart.size(), sectionsPart) != 0)
			{
				continue;
			}
			const size_t nameStart = biomeEnd + sectionsPart.size();
			if (file.size() > nameStart && file.find('/', nameStart) == String::npos)
			{
				result.push_back(file);
			}
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	bool LoadSectionAsset(const String& assetPath, pcg::SectionAsset& out, String& error)
	{
		if (assetPath.empty())
		{
			error = "No section asset is set.";
			return false;
		}

		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(assetPath);
		if (!file)
		{
			error = "Section asset '" + assetPath + "' not found. Bake the section first.";
			return false;
		}

		io::StreamSource source(*file);
		io::Reader reader(source);
		if (!pcg::ReadSectionAsset(reader, out))
		{
			error = "Section asset '" + assetPath + "' is not a readable section asset. Bake the section again.";
			return false;
		}

		return true;
	}

	String GetSectionWorldPathForAsset(const String& assetPath)
	{
		const size_t slash = assetPath.find_last_of('/');
		const String dir = slash == String::npos ? String() : assetPath.substr(0, slash + 1);
		String stem = slash == String::npos ? assetPath : assetPath.substr(slash + 1);
		const size_t dot = stem.find_last_of('.');
		if (dot != String::npos)
		{
			stem.erase(dot);
		}

		return dir + stem + "/" + stem + ".hwld";
	}

	bool IsSectionAssetStale(const String& assetPath, const pcg::SectionAsset& asset, String& warning)
	{
		const String worldPath = GetSectionWorldPathForAsset(assetPath);
		if (!AssetRegistry::HasFile(worldPath))
		{
			return false;
		}

		if (ComputeSectionSourceStamp(worldPath) == asset.sourceStamp)
		{
			return false;
		}

		warning = "Section asset '" + assetPath + "' is older than its section world '" + worldPath
			+ "' (changed since the last bake). Built with the baked version; bake the section again to use the changes.";
		return true;
	}

	bool ApplyNewSectionPathOptions(const NewSectionPathOptions& options, pcg::SectionPathParams& params, String& error)
	{
		pcg::SectionPathParams p = params;
		p.seed = options.seed;
		p.heightMode = options.heightMode ? *options.heightMode
			: (options.closed ? pcg::SectionHeightMode::SmoothedGround : pcg::SectionHeightMode::FollowSpline);
		if (options.groundSmoothing)
		{
			p.groundSmoothing = *options.groundSmoothing;
		}
		if (options.mass)
		{
			p.massEnabled = true;
		}
		if (options.mirror)
		{
			p.mirrorCopies = true;
			p.facadeHeightJitter = DefaultFacadeHeightJitter;
		}
		for (const auto& [name, value] : options.settings)
		{
			if (!pcg::SetSectionPathParam(p, name, value, error))
			{
				return false;
			}
		}

		params = p;
		error.clear();
		return true;
	}

	String DescribeSectionLayout(const pcg::SectionPathShape& shape)
	{
		const pcg::SectionLayoutCounts counts = pcg::CountSectionCopies(shape.layout);
		std::ostringstream stream;
		stream << "length " << static_cast<int32>(shape.path ? shape.path->GetLength() + 0.5f : 0.0f) << " m, " << counts.straight
			<< " straight copies, " << counts.inner << " inner / " << counts.outer << " outer corners";
		return stream.str();
	}

	bool PrepareSectionPathProbe(const pcg::SectionPathRecord& record, const pcg::SectionAsset& asset, pcg::SectionPathShape& shape, String& error)
	{
		return pcg::PrepareSectionPath(record.spline, record.pointIds, asset, record.params,
			[](float, float) { return 0.0f; }, shape, error, false);
	}

	OasisBuildReport RebuildSectionPath(IWorldEditor& editor, pcg::SectionPathRecord& record, const pcg::SectionAsset& asset)
	{
		const auto start = std::chrono::steady_clock::now();
		const auto finish = [start](OasisBuildReport& report)
		{
			report.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			return report;
		};

		// Validate first; a broken path must leave the world untouched. Neither validation nor the
		// influence depend on the heights.
		pcg::SectionPathShape probe;
		String probeError;
		if (!PrepareSectionPathProbe(record, asset, probe, probeError))
		{
			OasisBuildReport report;
			report.error = "Invalid section path: " + probeError;
			return finish(report);
		}

		if (!editor.HasTerrain() || editor.GetTerrain() == nullptr)
		{
			OasisBuildReport report;
			report.error = "This world has no terrain.";
			return finish(report);
		}

		// The shape is prepared on the ground as it is once the old contribution is undone (follow-ground
		// mode reads it); the terrain and placement functions read it from there.
		const auto shape = std::make_shared<pcg::SectionPathShape>();
		const auto remap = std::make_shared<TileLayerRemap>(*editor.GetTerrain(), asset);
		const pcg::LayerRemapFunction remapFunction = [remap](const float x, const float z)
		{
			return remap->Get(x, z);
		};

		GeneratedContent content;
		content.influence = pcg::GetInfluenceRect(probe);

		// Every write lies within a copy's rect; open ocean between far apart copies stays open ocean.
		pcg::GetSectionCopyRects(probe, content.writeRects);
		content.prepare = [&record, &asset, shape](const GroundFunction& ground, String& error)
		{
			return pcg::PrepareSectionPath(record.spline, record.pointIds, asset, record.params, ground, *shape, error);
		};
		content.terrain = [shape, remapFunction](const float x, const float z, const float baseHeight, pcg::terrain_channel::Type)
		{
			return pcg::EvaluateSectionTerrain(*shape, x, z, baseHeight, remapFunction);
		};
		content.placements = [shape](const GroundFunction& ground, const GroundFunction& /*slope*/, std::vector<pcg::Placement>& out)
		{
			pcg::BuildSectionPlacements(*shape, ground, out);
		};

		OasisBuildReport report = ApplyGeneratedContent(editor, content, record.content, record.id, SectionPathLabel);
		if (report.ok)
		{
			const String skipped = remap->DescribeSkipped();
			if (!skipped.empty())
			{
				report.warnings.push_back(skipped);
				WLOG("Section path '" << record.name << "': paint skipped on tiles without the section layer's texture:" << remap->DescribeAllSkipped());
			}
		}

		return finish(report);
	}

	OasisBuildReport DeleteSectionPathContent(IWorldEditor& editor, pcg::SectionPathRecord& record)
	{
		OasisBuildReport report = RemoveGeneratedContent(editor, record.content);
		if (report.ok)
		{
			record.builtRects.clear();
		}
		return report;
	}
}
