// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_bake_job.h"

#include "editor_terrain_access.h"
#include "editors/world_editor/world_editor_instance.h"

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "pcg/section_asset.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"
#include "terrain_io/page_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <vector>

namespace mmo
{
	const char* const SectionWorldFolder = "Editor/Pcg/Sections";
	const char* const DefaultSectionTerrainMaterial = "Models/FalwynPlains_Terrain_Inst.hmi";

	namespace
	{
		constexpr uint64 FnvOffset = 14695981039346656037ull;
		constexpr uint64 FnvPrime = 1099511628211ull;

		/// Chunk magics of the .hwld format (see WorldEditorInstance::Save).
		const ChunkMagic WorldVersionChunk = MakeChunkMagic('MVER');
		const ChunkMagic WorldMeshChunk = MakeChunkMagic('MESH');
		const ChunkMagic WorldTerrainChunk = MakeChunkMagic('RRET');

		/// Pages of a new section world: the 2x2 pages around world origin on a 64x64 page terrain.
		constexpr uint32 FirstSectionPage = terrain::constants::MaxPages / 2 - 1;
		constexpr uint32 LastSectionPage = terrain::constants::MaxPages / 2;

		String Normalize(String path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		/// The world's data folder (pages, entities, foliage): <dir>/<name>.
		String GetWorldDataFolder(const String& worldPath)
		{
			const std::filesystem::path path(Normalize(worldPath));
			return (path.parent_path() / path.stem()).generic_string();
		}

		void HashBytes(uint64& hash, const char* data, const size_t size)
		{
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= static_cast<uint8>(data[i]);
				hash *= FnvPrime;
			}
		}

		/// Hashes a file's path, size and contents; canonicalDefinition hashes a .hsecdef in its canonical form.
		void HashFile(uint64& hash, const String& assetPath, const bool canonicalDefinition = false)
		{
			HashBytes(hash, assetPath.data(), assetPath.size());
			const char zero = 0;
			HashBytes(hash, &zero, 1);

			std::vector<char> contents;
			if (std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(assetPath))
			{
				contents.assign(std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>());
			}

			// The section definition is text and may be checked out with CRLF line endings: hash its canonical
			// form so the stamp is the same on every checkout. Binary files are hashed as they are.
			if (canonicalDefinition)
			{
				const String canonical = pcg::CanonicalSectionDefinitionText(String(contents.begin(), contents.end()));
				contents.assign(canonical.begin(), canonical.end());
			}

			uint64 size = contents.size();
			char sizeBytes[8];
			for (int i = 0; i < 8; ++i)
			{
				sizeBytes[i] = static_cast<char>((size >> (8 * i)) & 0xFF);
			}
			HashBytes(hash, sizeBytes, sizeof(sizeBytes));
			HashBytes(hash, contents.data(), contents.size());
		}

		bool EndsWith(const String& text, const char* suffix)
		{
			const size_t length = std::char_traits<char>::length(suffix);
			return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
		}

		/// Writes a whole file through the asset registry.
		bool WriteAssetFile(const String& assetPath, const std::vector<char>& bytes)
		{
			std::unique_ptr<std::ostream> file = AssetRegistry::CreateNewFile(assetPath);
			if (!file)
			{
				return false;
			}

			file->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			file->flush();
			return static_cast<bool>(*file);
		}

		/// The .hwld of a new section world, laid out like WorldEditorInstance::Save writes it.
		std::vector<char> BuildWorldFile(const String& terrainMaterial)
		{
			std::vector<char> bytes;
			io::VectorSink<char> sink(bytes);
			io::Writer writer(sink);

			writer
				<< io::write<uint32>(*WorldVersionChunk)
				<< io::write<uint32>(sizeof(uint32))
				<< io::write<uint32>(3);

			{
				ChunkWriter terrainWriter(WorldTerrainChunk, writer);
				writer
					<< io::write<uint8>(1)
					<< io::write_dynamic_range<uint16>(terrainMaterial)
					<< io::write<float>(terrain::constants::DefaultSeaLevel);
				terrainWriter.Finish();
			}

			writer
				<< io::write<uint32>(*WorldMeshChunk)
				<< io::write<uint32>(0);
			return bytes;
		}

		/// A flat page at height 0, painted with SectionDefaultPaint, without water.
		std::vector<char> BuildFlatPage()
		{
			terrain_io::PageData page;
			page.Reset();
			std::fill(page.layers.begin(), page.layers.end(), SectionDefaultPaint);

			std::vector<char> bytes;
			io::VectorSink<char> sink(bytes);
			io::Writer writer(sink);
			if (!terrain_io::SavePage(writer, terrain_io::MakeView(page)))
			{
				bytes.clear();
			}
			return bytes;
		}

		/// World rect a variant captures: the variant rectangle plus the side falloff.
		struct VariantRect
		{
			double minX, minZ, maxX, maxZ;
		};

		VariantRect GetVariantRect(const pcg::SectionDefinition& def, const pcg::SectionVariantDef& variant)
		{
			const double reach = static_cast<double>(def.halfWidth) + static_cast<double>(def.sideFalloff);
			return { variant.originX - reach, 0.0, variant.originX + reach, static_cast<double>(def.length) };
		}

		String FormatMetres(const double value)
		{
			std::ostringstream stream;
			stream.setf(std::ios::fixed);
			stream.precision(2);
			stream << value;
			return stream.str();
		}

		/// Refuses rects that leave the terrain or touch pages without terrain (missing page files load
		/// as open ocean placeholders). Expects the pages to be prepared already.
		bool ValidateVariantTerrain(const terrain::Terrain& terrain, const pcg::SectionDefinition& def, String& error)
		{
			const int32 width = static_cast<int32>(terrain.GetWidth());
			const int32 height = static_cast<int32>(terrain.GetHeight());
			const double pageSize = terrain::constants::PageSize;
			const double minWorldX = -static_cast<double>(width / 2) * pageSize;
			const double minWorldZ = -static_cast<double>(height / 2) * pageSize;
			const double maxWorldX = minWorldX + width * pageSize;
			const double maxWorldZ = minWorldZ + height * pageSize;

			for (size_t i = 0; i < def.variants.size(); ++i)
			{
				const VariantRect rect = GetVariantRect(def, def.variants[i]);
				const String label = "Variant " + std::to_string(i + 1) + " (originX " + FormatMetres(def.variants[i].originX) + ")";
				if (rect.minX < minWorldX || rect.minZ < minWorldZ || rect.maxX > maxWorldX || rect.maxZ > maxWorldZ)
				{
					error = label + " leaves the terrain";
					return false;
				}

				// The upper bound is closed on purpose: a rect ending exactly on a page edge samples the
				// seam vertex and splat pixel there, and Terrain::GetAt / GetLayersAt resolve a seam index to
				// the page AFTER the edge (index / steps per page). Without that page the bake would read
				// height 0 and paint 0 at the seam instead of the shared value.
				const int32 minPageX = static_cast<int32>(std::floor(rect.minX / pageSize)) + width / 2;
				const int32 maxPageX = std::min(width - 1, static_cast<int32>(std::floor(rect.maxX / pageSize)) + width / 2);
				const int32 minPageZ = static_cast<int32>(std::floor(rect.minZ / pageSize)) + height / 2;
				const int32 maxPageZ = std::min(height - 1, static_cast<int32>(std::floor(rect.maxZ / pageSize)) + height / 2);
				for (int32 pz = minPageZ; pz <= maxPageZ; ++pz)
				{
					for (int32 px = minPageX; px <= maxPageX; ++px)
					{
						const terrain::Page* page = terrain.GetPage(static_cast<uint32>(px), static_cast<uint32>(pz));
						if (!page || !page->IsPrepared() || page->IsOceanPlaceholder())
						{
							error = label + " covers terrain page " + std::to_string(px) + "x" + std::to_string(pz) +
								", which has no terrain (x " + FormatMetres(rect.minX) + ".." + FormatMetres(rect.maxX) +
								", z " + FormatMetres(rect.minZ) + ".." + FormatMetres(rect.maxZ) + ")";
							return false;
						}
					}
				}
			}

			return true;
		}
	}

	String GetSectionWorldPath(const String& name)
	{
		return GetSectionWorldPath(String(), name);
	}

	String GetSectionWorldPath(const String& folder, const String& name)
	{
		const String base = folder.empty() ? String(SectionWorldFolder) : folder;
		return base + "/" + name + "/" + name + ".hwld";
	}

	bool IsValidSectionFolder(const String& folder, String& error)
	{
		// Only where ListSectionAssets finds the baked assets: never next to world records (Editor/Pcg/Worlds).
		const String usage = "A section folder is " + String(SectionWorldFolder) + " or Editor/Pcg/Biomes/<Biome>/Sections (got '" + folder + "')";
		if (folder == SectionWorldFolder)
		{
			return true;
		}

		const String prefix = "Editor/Pcg/Biomes/";
		const String suffix = "/Sections";
		if (folder.size() <= prefix.size() + suffix.size() || folder.compare(0, prefix.size(), prefix) != 0 ||
			folder.compare(folder.size() - suffix.size(), suffix.size(), suffix) != 0)
		{
			error = usage;
			return false;
		}

		const String biome = folder.substr(prefix.size(), folder.size() - prefix.size() - suffix.size());
		String nameError;
		if (biome.find('/') != String::npos || !IsValidSectionName(biome, nameError))
		{
			error = usage + ": the biome name needs 1 to 64 of A-Z, a-z, 0-9, '_' and '-'";
			return false;
		}

		return true;
	}

	String GetSectionName(const String& worldPath)
	{
		return std::filesystem::path(Normalize(worldPath)).stem().string();
	}

	String GetSectionDefinitionPath(const String& worldPath)
	{
		std::filesystem::path path(Normalize(worldPath));
		path.replace_extension(".hsecdef");
		return path.generic_string();
	}

	String GetSectionAssetPath(const String& worldPath)
	{
		const std::filesystem::path path(Normalize(worldPath));
		const std::filesystem::path folder = path.parent_path().parent_path();
		const std::filesystem::path file = path.stem().string() + ".hsec";
		return folder.empty() ? file.generic_string() : (folder / file).generic_string();
	}

	bool IsSectionWorld(const String& worldPath)
	{
		return AssetRegistry::HasFile(GetSectionDefinitionPath(worldPath));
	}

	bool IsValidSectionName(const String& name, String& error)
	{
		if (name.empty() || name.size() > 64)
		{
			error = "A section name needs 1 to 64 characters";
			return false;
		}

		for (const char c : name)
		{
			const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			if (!allowed)
			{
				error = "A section name may only use letters, digits, '_' and '-'";
				return false;
			}
		}

		return true;
	}

	bool CreateSectionWorld(const String& name, const String& terrainMaterial, String& error)
	{
		return CreateSectionWorld(String(), name, terrainMaterial, error);
	}

	bool CreateSectionWorld(const String& folder, const String& name, const String& terrainMaterial, String& error)
	{
		error.clear();
		if (!IsValidSectionName(name, error))
		{
			return false;
		}
		if (!folder.empty() && !IsValidSectionFolder(folder, error))
		{
			return false;
		}

		const String material = terrainMaterial.empty() ? String(DefaultSectionTerrainMaterial) : Normalize(terrainMaterial);
		if (!AssetRegistry::HasFile(material))
		{
			error = "Terrain material " + material + " does not exist";
			return false;
		}

		const String worldPath = GetSectionWorldPath(folder, name);
		const String definitionPath = GetSectionDefinitionPath(worldPath);
		if (AssetRegistry::HasFile(worldPath) || AssetRegistry::HasFile(definitionPath))
		{
			error = "Section " + name + " already exists (" + worldPath + ")";
			return false;
		}

		const String terrainFolder = GetWorldDataFolder(worldPath) + "/Terrain";
		for (uint32 z = FirstSectionPage; z <= LastSectionPage; ++z)
		{
			for (uint32 x = FirstSectionPage; x <= LastSectionPage; ++x)
			{
				const String pagePath = terrainFolder + "/" + std::to_string(x) + "_" + std::to_string(z) + ".tile";
				if (AssetRegistry::HasFile(pagePath))
				{
					error = "Section " + name + " already has terrain pages (" + pagePath + ")";
					return false;
				}
			}
		}

		std::vector<String> written;
		const auto fail = [&written, &error](const String& message)
		{
			for (const String& path : written)
			{
				AssetRegistry::RemoveFile(path);
			}
			error = message;
			return false;
		};

		const std::vector<char> page = BuildFlatPage();
		if (page.empty())
		{
			return fail("Could not build a flat terrain page");
		}

		for (uint32 z = FirstSectionPage; z <= LastSectionPage; ++z)
		{
			for (uint32 x = FirstSectionPage; x <= LastSectionPage; ++x)
			{
				const String pagePath = terrainFolder + "/" + std::to_string(x) + "_" + std::to_string(z) + ".tile";
				if (!WriteAssetFile(pagePath, page))
				{
					return fail("Could not write " + pagePath);
				}
				written.push_back(pagePath);
			}
		}

		if (!WriteAssetFile(worldPath, BuildWorldFile(material)))
		{
			return fail("Could not write " + worldPath);
		}
		written.push_back(worldPath);

		pcg::SectionDefinition def;
		def.name = name;
		def.variants.push_back(pcg::SectionVariantDef{ 0.0f });
		String saveError;
		if (!SaveSectionDefinition(worldPath, def, saveError))
		{
			return fail(saveError);
		}

		ILOG("Created section world " << worldPath << " (terrain material " << material << ")");
		return true;
	}

	bool LoadSectionDefinition(const String& worldPath, pcg::SectionDefinition& out, String& error)
	{
		const String path = GetSectionDefinitionPath(worldPath);
		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(path);
		if (!file)
		{
			error = "Section definition " + path + " not found (not a section world)";
			return false;
		}

		const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
		if (!pcg::ParseSectionDefinition(text, out))
		{
			error = "Section definition " + path + " is not valid JSON";
			return false;
		}

		return true;
	}

	bool SaveSectionDefinition(const String& worldPath, const pcg::SectionDefinition& def, String& error)
	{
		const String path = GetSectionDefinitionPath(worldPath);
		const String text = pcg::WriteSectionDefinition(def);
		if (!WriteAssetFile(path, std::vector<char>(text.begin(), text.end())))
		{
			error = "Could not write " + path;
			return false;
		}

		return true;
	}

	uint64 ComputeSectionSourceStamp(const String& worldPath)
	{
		const String world = Normalize(worldPath);
		uint64 hash = FnvOffset;
		HashFile(hash, world);
		HashFile(hash, GetSectionDefinitionPath(world), true);

		std::vector<String> files;
		for (String file : AssetRegistry::ListFiles(GetWorldDataFolder(world) + "/", ""))
		{
			file = Normalize(file);
			if (EndsWith(file, ".tile") || EndsWith(file, ".wobj") || EndsWith(file, ".hfol"))
			{
				files.push_back(std::move(file));
			}
		}

		std::sort(files.begin(), files.end());
		files.erase(std::unique(files.begin(), files.end()), files.end());
		for (const String& file : files)
		{
			HashFile(hash, file);
		}

		return hash;
	}

	bool ReadSectionAssetStamp(const String& assetPath, uint64& stamp)
	{
		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(assetPath);
		if (!file)
		{
			return false;
		}

		io::StreamSource source(*file);
		io::Reader reader(source);
		pcg::SectionAsset asset;
		if (!pcg::ReadSectionAsset(reader, asset))
		{
			return false;
		}

		stamp = asset.sourceStamp;
		return true;
	}

	String GetTerrainLayerTextureKey(const MaterialPtr& material, const uint8 layer, String* parameter, bool* fromBinding)
	{
		if (parameter)
		{
			parameter->clear();
		}
		if (fromBinding)
		{
			*fromBinding = false;
		}

		if (!material || layer >= MaterialLayerBindingCount)
		{
			return String();
		}

		const String& bound = material->GetLayerBinding(layer).albedoTextureParam;
		const bool useBinding = !bound.empty();
		const String name = useBinding ? bound : "Albedo0" + std::to_string(layer + 1);

		String texture;
		if (!material->GetTextureParameter(name, texture) || texture.empty())
		{
			return String();
		}

		if (parameter)
		{
			*parameter = name;
		}
		if (fromBinding)
		{
			*fromBinding = useBinding;
		}
		return texture;
	}

	bool GetSectionLayerTextures(const MaterialPtr& material, std::array<String, 4>& out)
	{
		out = {};
		if (!material)
		{
			return false;
		}

		for (uint8 layer = 0; layer < 4; ++layer)
		{
			out[layer] = GetTerrainLayerTextureKey(material, layer);
		}

		return true;
	}

	bool ComputeRunPieceExtent(const MapEntity& entity, pcg::SectionPieceExtent& out)
	{
		out = pcg::SectionPieceExtent();
		out.key = entity.GetUniqueId();

		const MovableObject* movable = entity.GetMovableObject();
		if (!movable || (entity.IsMeshEntity() && (!entity.GetEntity() || !entity.GetEntity()->GetMesh())))
		{
			return false;
		}

		const AABB& bounds = movable->GetBoundingBox();
		if (bounds.IsNull())
		{
			return false;
		}

		const SceneNode& node = entity.GetSceneNode();
		const Quaternion rotation = node.GetDerivedOrientation();
		const Vector3 scale = node.GetDerivedScale();

		Vector3 corners[8];
		bounds.GetCorners(corners);
		float minZ = 0.0f;
		float maxZ = 0.0f;
		for (int32 i = 0; i < 8; ++i)
		{
			const float z = (rotation * (corners[i] * scale)).z;
			minZ = i == 0 ? z : std::min(minZ, z);
			maxZ = i == 0 ? z : std::max(maxZ, z);
		}

		out.length = maxZ - minZ;
		out.startOffset = minZ;
		return true;
	}

	void CollectRunPieceExtents(const WorldEditorInstance& world, const pcg::SectionDefinition& def, std::vector<pcg::SectionPieceExtent>& out)
	{
		out.clear();
		for (const pcg::SectionRunDef& run : def.runs)
		{
			for (const uint64 member : run.members)
			{
				const MapEntity* entity = world.FindMapEntity(member);
				pcg::SectionPieceExtent extent;
				if (entity && ComputeRunPieceExtent(*entity, extent))
				{
					out.push_back(extent);
				}
			}
		}

		std::sort(out.begin(), out.end(), [](const pcg::SectionPieceExtent& a, const pcg::SectionPieceExtent& b) { return a.key < b.key; });
		out.erase(std::unique(out.begin(), out.end(), [](const pcg::SectionPieceExtent& a, const pcg::SectionPieceExtent& b) { return a.key == b.key; }), out.end());
	}

	/// Bakes without releasing force-loaded pages (see BakeSectionWorld).
	static bool BakeSectionWorldImpl(WorldEditorInstance& world, String& error)
	{
		const String worldPath = Normalize(world.GetAssetPath().generic_string());

		pcg::SectionDefinition def;
		if (!LoadSectionDefinition(worldPath, def, error))
		{
			return false;
		}

		// The section is named after its world (the asset is <name>.hsec next to the folder).
		def.name = GetSectionName(worldPath);

		terrain::Terrain* terrain = world.GetTerrain();
		if (!terrain || !world.HasTerrain())
		{
			error = "The section world has no terrain";
			return false;
		}

		if (def.variants.empty())
		{
			error = "The section has no variants";
			return false;
		}

		if (def.length <= 0.0f || def.halfWidth <= 0.0f || def.sideFalloff < 0.0f)
		{
			error = "The section needs a positive length and half width and a side falloff of at least 0";
			return false;
		}

		// Load every page the rects cover first, so the terrain check below sees prepared pages.
		double minX = 0.0, maxX = 0.0;
		for (size_t i = 0; i < def.variants.size(); ++i)
		{
			const VariantRect rect = GetVariantRect(def, def.variants[i]);
			minX = i == 0 ? rect.minX : std::min(minX, rect.minX);
			maxX = i == 0 ? rect.maxX : std::max(maxX, rect.maxX);
		}

		String loadError;
		const bool pagesLoaded = world.EnsurePagesLoaded(static_cast<float>(minX), 0.0f, static_cast<float>(maxX), def.length, loadError);
		if (!ValidateVariantTerrain(*terrain, def, error))
		{
			return false;
		}
		if (!pagesLoaded)
		{
			error = loadError;
			return false;
		}

		std::vector<pcg::Placement> placements;
		world.CollectWorldPlacements(placements);

		const MaterialPtr material = terrain->GetDefaultMaterial();
		if (!material)
		{
			error = "The section terrain has no default material";
			return false;
		}

		std::array<String, 4> layerTextures;
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			String parameter;
			bool fromBinding = false;
			layerTextures[layer] = GetTerrainLayerTextureKey(material, layer, &parameter, &fromBinding);
			if (layerTextures[layer].empty())
			{
				ILOG("Section paint layer " << static_cast<uint32>(layer) << ": no texture key in " << material->GetName());
			}
			else
			{
				ILOG("Section paint layer " << static_cast<uint32>(layer) << ": " << layerTextures[layer] << " (" << (fromBinding ? "layer binding" : "fallback")
					<< " parameter " << parameter << " of " << material->GetName() << ")");
			}
		}

		const uint64 stamp = ComputeSectionSourceStamp(worldPath);

		std::vector<pcg::SectionPieceExtent> extents;
		CollectRunPieceExtents(world, def, extents);

		EditorTerrainAccess access(*terrain);
		pcg::SectionAsset asset;
		size_t ignored = 0;
		size_t missingMembers = 0;
		if (!pcg::BakeSection(def, access, placements, extents, SectionDefaultPaint, layerTextures, stamp, asset, error, &ignored, &missingMembers))
		{
			return false;
		}

		if (missingMembers > 0)
		{
			WLOG("Section " << def.name << ": " << missingMembers << " run member(s) are not in the section world any more and were skipped");
		}
		for (size_t v = 0; v < asset.variants.size(); ++v)
		{
			for (const pcg::SectionRun& run : asset.variants[v].runs)
			{
				ILOG("Section run '" << run.name << "' (variant " << (v + 1) << "): " << run.pieces.size() << " piece(s), x " << FormatMetres(run.x)
					<< ", z " << FormatMetres(run.startZ) << ".." << FormatMetres(run.endZ) << ", "
					<< (pcg::IsContinuousRun(run, asset.length) ? "continuous" : "not continuous") << ", keep " << run.keepChance);
				for (const pcg::SectionRunPiece& piece : run.pieces)
				{
					ILOG("  piece " << piece.placement.key << " " << piece.placement.asset << " at z " << FormatMetres(piece.placement.position.z)
						<< ": length " << FormatMetres(piece.length) << ", start offset " << FormatMetres(piece.startOffset));
				}
			}
		}

		// Paint of a layer without a texture key could never be matched when stamping: refuse instead of
		// silently dropping it. Untouched default paint has coverage 0 and does not count.
		for (uint8 layer = 0; layer < 4; ++layer)
		{
			if (!layerTextures[layer].empty())
			{
				continue;
			}

			for (size_t v = 0; v < asset.variants.size(); ++v)
			{
				const pcg::SectionPaint& paint = asset.variants[v].paint;
				for (size_t i = 0; i < paint.packed.size() && i < paint.coverage.size(); ++i)
				{
					if (paint.coverage[i] != 0 && ((paint.packed[i] >> (8 * layer)) & 0xFFu) != 0)
					{
						error = "Paint layer " + std::to_string(layer + 1) + " is painted in variant " + std::to_string(v + 1) +
							", but the terrain material " + String(material->GetName()) + " has no texture for it (no layer binding and no Albedo0" +
							std::to_string(layer + 1) + " parameter)";
						return false;
					}
				}
			}
		}

		std::vector<char> bytes;
		io::VectorSink<char> sink(bytes);
		io::Writer writer(sink);
		if (!pcg::WriteSectionAsset(writer, asset))
		{
			error = "The baked section could not be serialized";
			return false;
		}

		const String assetPath = GetSectionAssetPath(worldPath);
		if (!WriteAssetFile(assetPath, bytes))
		{
			error = "Could not write " + assetPath;
			return false;
		}

		size_t captured = 0;
		size_t runs = 0;
		for (const pcg::SectionVariant& variant : asset.variants)
		{
			captured += variant.placements.size();
			runs += variant.runs.size();
		}

		char stampText[32];
		std::snprintf(stampText, sizeof(stampText), "%016llx", static_cast<unsigned long long>(stamp));
		ILOG("Baked section " << def.name << " to " << assetPath << ": " << asset.variants.size() << " variant(s), "
			<< captured << " placement(s) captured, " << ignored << " outside every variant, " << asset.rules.size()
			<< " rule(s), " << runs << " run(s), " << bytes.size() << " bytes, source stamp " << stampText);
		return true;
	}

	bool BakeSectionWorld(WorldEditorInstance& world, String& error)
	{
		error.clear();

		// Entities and foliage the bake has to load are released again afterwards, unless pages were
		// already force-loaded before (an unsaved generator build may still own those).
		const bool hadForcedPages = world.HasForcedPages();
		const bool baked = BakeSectionWorldImpl(world, error);
		if (!hadForcedPages)
		{
			world.ReleaseForcedPages();
		}

		return baked;
	}
}
