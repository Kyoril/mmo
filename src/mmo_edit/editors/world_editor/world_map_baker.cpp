// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_map_baker.h"

#include "page_bake_utils.h"

#include "assets/asset_registry.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "game/world_map.h"
#include "log/default_log_levels.h"
#include "terrain_io/page_data.h"
#include "terrain_io/page_io.h"
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>

namespace mmo
{
	namespace
	{
		struct Rgb
		{
			float r;
			float g;
			float b;
		};

		/// The sea behind and around the map (the client's WorldMap clears to the same colour).
		constexpr Rgb SeaColor{ 43.0f / 255.0f, 63.0f / 255.0f, 74.0f / 255.0f };

		/// Ink of the zone border lines.
		constexpr Rgb InkColor{ 0.22f, 0.15f, 0.09f };

		/// Paper tint multiplied into the art.
		constexpr Rgb ParchmentTint{ 1.0f, 0.94f, 0.80f };

		float Hash(const int32 x, const int32 z)
		{
			uint32 h = static_cast<uint32>(x) * 374761393u + static_cast<uint32>(z) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return static_cast<float>(h & 0xFFFF) / 65535.0f;
		}

		Rgb Mix(const Rgb& a, const Rgb& b, const float t)
		{
			return Rgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
		}

		/// The printed-map grade: softer colour, a lift, the parchment tint and paper grain.
		Rgb Grade(Rgb c, const float grain)
		{
			const float luminance = c.r * 0.3f + c.g * 0.59f + c.b * 0.11f;
			c = Mix(Rgb{ luminance, luminance, luminance }, c, 0.72f);
			c = Rgb{ c.r * 1.05f + 0.025f, c.g * 1.05f + 0.025f, c.b * 1.05f + 0.025f };

			constexpr float tintAmount = 0.6f;
			c = Rgb{
				c.r * (1.0f - tintAmount * (1.0f - ParchmentTint.r)),
				c.g * (1.0f - tintAmount * (1.0f - ParchmentTint.g)),
				c.b * (1.0f - tintAmount * (1.0f - ParchmentTint.b)) };

			const float g = 1.0f + (grain - 0.5f) * 0.05f;
			return Rgb{ c.r * g, c.g * g, c.b * g };
		}

		uint8 ToByte(const float value)
		{
			return static_cast<uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		Rgb Unpack565(const uint16 c)
		{
			return Rgb{
				static_cast<float>((c >> 11) & 0x1F) * 255.0f / 31.0f,
				static_cast<float>((c >> 5) & 0x3F) * 255.0f / 63.0f,
				static_cast<float>(c & 0x1F) * 255.0f / 31.0f };
		}
	}

	WorldMapBaker::WorldMapBaker(String worldName, PageFilenameResolver pageFilename, ParentZoneResolver parentZone)
		: m_worldName(std::move(worldName))
		, m_pageFilename(std::move(pageFilename))
		, m_parentZone(std::move(parentZone))
	{
	}

	std::vector<uint8> WorldMapBaker::DecodeDxt1(const uint8* blocks, const uint32 size)
	{
		std::vector<uint8> rgba(static_cast<size_t>(size) * size * 4, 0);
		const uint32 blocksPerSide = std::max(1u, size / 4);
		for (uint32 by = 0; by < blocksPerSide; ++by)
		{
			for (uint32 bx = 0; bx < blocksPerSide; ++bx)
			{
				const uint8* block = blocks + (static_cast<size_t>(by) * blocksPerSide + bx) * 8;
				const uint16 c0 = static_cast<uint16>(block[0] | (block[1] << 8));
				const uint16 c1 = static_cast<uint16>(block[2] | (block[3] << 8));
				const uint32 indices = static_cast<uint32>(block[4]) | (static_cast<uint32>(block[5]) << 8) | (static_cast<uint32>(block[6]) << 16) | (static_cast<uint32>(block[7]) << 24);

				const Rgb a = Unpack565(c0);
				const Rgb b = Unpack565(c1);
				std::array<std::array<uint8, 4>, 4> palette{};
				palette[0] = { ToByte(a.r / 255.0f), ToByte(a.g / 255.0f), ToByte(a.b / 255.0f), 255 };
				palette[1] = { ToByte(b.r / 255.0f), ToByte(b.g / 255.0f), ToByte(b.b / 255.0f), 255 };
				if (c0 > c1)
				{
					const Rgb c2 = Mix(a, b, 1.0f / 3.0f);
					const Rgb c3 = Mix(a, b, 2.0f / 3.0f);
					palette[2] = { ToByte(c2.r / 255.0f), ToByte(c2.g / 255.0f), ToByte(c2.b / 255.0f), 255 };
					palette[3] = { ToByte(c3.r / 255.0f), ToByte(c3.g / 255.0f), ToByte(c3.b / 255.0f), 255 };
				}
				else
				{
					const Rgb c2 = Mix(a, b, 0.5f);
					palette[2] = { ToByte(c2.r / 255.0f), ToByte(c2.g / 255.0f), ToByte(c2.b / 255.0f), 255 };
					palette[3] = { 0, 0, 0, 0 };
				}

				for (uint32 i = 0; i < 16; ++i)
				{
					const uint32 px = bx * 4 + (i % 4);
					const uint32 py = by * 4 + (i / 4);
					if (px >= size || py >= size)
					{
						continue;
					}

					const auto& color = palette[(indices >> (i * 2)) & 0x3];
					std::copy(color.begin(), color.end(), rgba.begin() + (static_cast<size_t>(py) * size + px) * 4);
				}
			}
		}

		return rgba;
	}

	bool WorldMapBaker::LoadMinimapMips(const uint32 x, const uint32 z, PageImage& image) const
	{
		image.mips.clear();
		const String filename = world_map::GetMinimapTileFilename(m_worldName, x, z);
		const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(filename);
		if (!stream)
		{
			return false;
		}

		const std::vector<char> data{ std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>() };

		io::MemorySource source(data);
		io::Reader reader(source);
		tex::PreHeader preHeader;
		tex::v1_0::Header header(tex::Version_1_0);
		if (!tex::loadPreHeader(preHeader, reader) || preHeader.version != tex::Version_1_0 || !tex::v1_0::loadHeader(header, reader))
		{
			WLOG("World map bake: unreadable minimap tile " << filename);
			return false;
		}

		if (header.format != tex::v1_0::DXT1 || header.width != world_map::MinimapTileSize || header.height != world_map::MinimapTileSize)
		{
			WLOG("World map bake: minimap tile " << filename << " is not a " << world_map::MinimapTileSize << " px DXT1 texture");
			return false;
		}

		// Mip m has 512 >> m texels per side; level L art needs mip L + 1
		uint32 size = header.width;
		for (uint32 mip = 0; mip < header.mipmapOffsets.size() && size >= 4; ++mip, size /= 2)
		{
			const size_t offset = header.mipmapOffsets[mip];
			const size_t length = static_cast<size_t>(size / 4) * (size / 4) * 8;
			if (mip == 0 || offset == 0 || offset + length > data.size())
			{
				image.mips.emplace_back();
				continue;
			}

			image.mips.push_back(DecodeDxt1(reinterpret_cast<const uint8*>(data.data() + offset), size));
		}

		return true;
	}

	uint32 WorldMapBaker::GetRootZone(uint32 zoneId) const
	{
		for (int32 depth = 0; depth < 8 && zoneId != 0 && m_parentZone; ++depth)
		{
			const uint32 parent = m_parentZone(zoneId);
			if (parent == 0 || parent == zoneId)
			{
				break;
			}
			zoneId = parent;
		}

		return zoneId;
	}

	bool WorldMapBaker::Bake()
	{
		using namespace world_map;

		WorldMapInfo info;

		// Which pages have terrain
		uint32 minX = MaxPages, minZ = MaxPages, maxX = 0, maxZ = 0;
		for (uint32 z = 0; z < MaxPages; ++z)
		{
			for (uint32 x = 0; x < MaxPages; ++x)
			{
				if (AssetRegistry::HasFile(m_pageFilename(x, z)))
				{
					info.pages[z * MaxPages + x] = true;
					minX = std::min(minX, x);
					minZ = std::min(minZ, z);
					maxX = std::max(maxX, x);
					maxZ = std::max(maxZ, z);
				}
			}
		}

		if (minX > maxX)
		{
			ELOG("World map bake: " << m_worldName << " has no terrain pages");
			return false;
		}

		info.minPageX = minX;
		info.minPageZ = minZ;
		info.maxPageX = maxX;
		info.maxPageZ = maxZ;
		info.maxArtLevel = MaxArtLevel;
		info.ResetCells();

		// Zones of every terrain tile
		uint32 pageCount = 0;
		for (uint32 z = minZ; z <= maxZ; ++z)
		{
			for (uint32 x = minX; x <= maxX; ++x)
			{
				if (!info.HasPage(x, z))
				{
					continue;
				}

				const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(m_pageFilename(x, z));
				if (!stream)
				{
					continue;
				}

				io::StreamSource source(*stream);
				io::Reader reader(source);
				terrain_io::PageData page;
				if (!terrain_io::LoadPage(reader, page) || page.zones.size() < CellsPerPage * CellsPerPage)
				{
					WLOG("World map bake: could not read the zones of page " << x << "," << z);
					continue;
				}

				for (uint32 tz = 0; tz < CellsPerPage; ++tz)
				{
					for (uint32 tx = 0; tx < CellsPerPage; ++tx)
					{
						info.SetCell(x * CellsPerPage + tx, z * CellsPerPage + tz, page.zones[tx + tz * CellsPerPage]);
					}
				}
				++pageCount;
			}
		}

		info.RebuildZones();
		ILOG("World map bake: " << m_worldName << " has " << pageCount << " pages and " << info.zones.size() << " zones");

		// The cell grid with every sub zone merged into its root, for the strong border lines
		WorldMapInfo roots = info;
		roots.RemapCells([this](const uint32 zoneId) { return GetRootZone(zoneId); });

		// Minimap mips of every page, decoded once
		std::map<uint32, PageImage> images;
		for (uint32 z = minZ; z <= maxZ; ++z)
		{
			for (uint32 x = minX; x <= maxX; ++x)
			{
				PageImage image;
				if (info.HasPage(x, z) && LoadMinimapMips(x, z, image))
				{
					images.emplace(z * MaxPages + x, std::move(image));
				}
			}
		}

		if (images.empty())
		{
			WLOG("World map bake: " << m_worldName << " has no minimap tiles; the art will be open sea. Bake the minimaps first.");
		}

		uint32 written = 0;
		bool success = true;
		for (uint32 level = MinArtLevel; level <= MaxArtLevel; ++level)
		{
			const uint32 span = 1u << level;
			const uint32 pagePixels = ArtTileSize / span;
			const uint32 mip = level + 1;
			const double unitsPerPixel = ArtUnitsPerPixel(level);

			for (uint32 tz = minZ / span; tz <= maxZ / span; ++tz)
			{
				for (uint32 tx = minX / span; tx <= maxX / span; ++tx)
				{
					if (!info.HasArtTile(level, tx, tz))
					{
						continue;
					}

					std::vector<uint8> rgba(static_cast<size_t>(ArtTileSize) * ArtTileSize * 4, 255);
					for (uint32 py = 0; py < ArtTileSize; ++py)
					{
						for (uint32 px = 0; px < ArtTileSize; ++px)
						{
							const uint32 pageX = tx * span + px / pagePixels;
							const uint32 pageZ = tz * span + py / pagePixels;
							Rgb color = SeaColor;

							// Pages without a minimap tile (ocean placeholders) stay open sea
							const auto image = info.HasPage(pageX, pageZ) ? images.find(pageZ * MaxPages + pageX) : images.end();
							if (image != images.end() && mip < image->second.mips.size() && !image->second.mips[mip].empty())
							{
								const uint32 sx = px % pagePixels;
								const uint32 sy = py % pagePixels;
								const uint8* texel = &image->second.mips[mip][(static_cast<size_t>(sy) * pagePixels + sx) * 4];
								color = Rgb{ texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f };

								const double worldX = GridOrigin() + tx * span * PageSize + (px + 0.5) * unitsPerPixel;
								const double worldZ = GridOrigin() + tz * span * PageSize + (py + 0.5) * unitsPerPixel;
								color = Grade(color, Hash(static_cast<int32>(std::floor(worldX / unitsPerPixel)), static_cast<int32>(std::floor(worldZ / unitsPerPixel))));

								// Zone borders: a pixel on a border has neighbours (a pixel away) in another zone.
								// Sub pixel samples keep the line soft where it crosses cells diagonally.
								const uint32 rootHere = roots.GetZoneAt(worldX, worldZ);
								const uint32 zoneHere = info.GetZoneAt(worldX, worldZ);
								int32 rootEdges = 0;
								int32 subEdges = 0;
								constexpr std::array<std::pair<float, float>, 8> offsets{ {
									{ -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f },
									{ -0.7f, -0.7f }, { 0.7f, -0.7f }, { -0.7f, 0.7f }, { 0.7f, 0.7f } } };
								for (const auto& [ox, oz] : offsets)
								{
									const double sampleX = worldX + ox * unitsPerPixel;
									const double sampleZ = worldZ + oz * unitsPerPixel;
									const uint32 rootThere = roots.GetZoneAt(sampleX, sampleZ);
									if (rootHere != 0 && rootThere != 0 && rootThere != rootHere)
									{
										++rootEdges;
									}
									else if (zoneHere != 0)
									{
										const uint32 zoneThere = info.GetZoneAt(sampleX, sampleZ);
										if (zoneThere != 0 && zoneThere != zoneHere)
										{
											++subEdges;
										}
									}
								}

								if (rootEdges > 0)
								{
									color = Mix(color, InkColor, std::min(1.0f, rootEdges / 3.0f) * 0.62f);
								}
								else if (subEdges > 0)
								{
									color = Mix(color, InkColor, std::min(1.0f, subEdges / 3.0f) * 0.22f);
								}
							}

							uint8* out = &rgba[(static_cast<size_t>(py) * ArtTileSize + px) * 4];
							out[0] = ToByte(color.r);
							out[1] = ToByte(color.g);
							out[2] = ToByte(color.b);
							out[3] = 255;
						}
					}

					if (!page_bake::WriteDxt1Texture(GetArtTileFilename(m_worldName, level, tx, tz), std::move(rgba), ArtTileSize))
					{
						success = false;
					}
					++written;
				}
			}
		}

		// The metadata last, so a client never finds it before the tiles it names
		const String infoFilename = GetInfoFilename(m_worldName);
		const auto file = AssetRegistry::CreateNewFile(infoFilename);
		if (!file)
		{
			ELOG("World map bake: failed to create " << infoFilename);
			return false;
		}

		io::StreamSink sink(*file);
		io::Writer writer(sink);
		writer << info;
		file->flush();

		ILOG("World map bake: wrote " << written << " art tiles and " << infoFilename);
		return success && static_cast<bool>(*file);
	}
}
