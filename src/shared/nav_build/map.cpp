
#include "map.h"

#include "assets/asset_registry.h"
#include "terrain/terrain.h"
#include "terrain/page.h"
#include "scene_graph/scene.h"

#include <iomanip>
#include <sstream>
#include <algorithm>

#include "base/utilities.h"
#include "game_client/world_entity_loader.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_serializer.h"


namespace mmo
{
    static const ChunkMagic VersionChunkMagic = MakeChunkMagic('MVER');
    static const ChunkMagic MeshNamesChunkMagic = MakeChunkMagic('MESH');
    static const ChunkMagic EntityChunkMagic = MakeChunkMagic('MENT');
    static const ChunkMagic TerrainChunkMagic = MakeChunkMagic('RRET');

    uint16 GetIndex(size_t x, size_t y)
    {
        return static_cast<uint16>(x + y * terrain::constants::VerticesPerTile);
    }

    MapEntity::MapEntity(const std::string& path)
		: Filename(path)
    {
        auto file = AssetRegistry::OpenFile(path);
        if (!file)
        {
            ELOG("Failed to load entity file " << path << ": File does not exist");
            return;
        }

        io::StreamSource source{ *file };
		io::Reader reader{ source };

        MeshPtr mesh = std::make_shared<Mesh>(path);
        MeshDeserializer deserializer(*mesh);
        deserializer.Read(reader);

        RootId = 0; // Hrm

        const AABBTree& collisionTree = mesh->GetCollisionTree();
        if (collisionTree.IsEmpty())
        {
            DLOG("Mesh " << path << " has no collision - ignoring it!");
            return;
        }

        // Copy collision data
        Vertices = collisionTree.GetVertices();
        Indices.reserve(collisionTree.GetIndices().size());
		for (auto& index : collisionTree.GetIndices())
		{
			Indices.push_back(static_cast<int32>(index));
		}

		Bounds = collisionTree.GetBoundingBox();
    }

    MapEntityInstance::MapEntityInstance(const MapEntity* entity, const AABB& bounds, const Matrix4& transformMatrix)
		: TransformMatrix(transformMatrix)
		, Bounds(bounds)
		, Model(entity)
    {
        std::vector<Vector3> vertices;
        std::vector<int32> indices;

        BuildTriangles(vertices, indices);
    }

    Vector3 MapEntityInstance::TransformVertex(const Vector3& vertex) const
	{
		return TransformMatrix * vertex;
    }

    void MapEntityInstance::BuildTriangles(std::vector<Vector3>& vertices, std::vector<int32>& indices) const
    {
        vertices.clear();
        vertices.reserve(Model->Vertices.size());

        indices.clear();
        indices.resize(Model->Indices.size());
        std::copy(Model->Indices.cbegin(), Model->Indices.cend(), indices.begin());

        for (auto& vertex : Model->Vertices)
        {
            vertices.push_back(TransformVertex(vertex));
        }
    }

    TerrainPage::TerrainPage(const Map* map, int x, int y)
        : map(map)
        , X(x)
        , Y(y)
    {
        const float minX = static_cast<float>(static_cast<double>(X - 32) * terrain::constants::PageSize);
        const float minZ = static_cast<float>(static_cast<double>(Y - 32) * terrain::constants::PageSize);
        Bounds = AABB(
            Vector3(minX, std::numeric_limits<float>::max(), minZ),
            Vector3(minX + terrain::constants::PageSize, std::numeric_limits<float>::lowest(), minZ + terrain::constants::PageSize));

        // TODO: This method is lazy as it loads the page, but the page also has references to visual stuff which is really not needed here
        // We should separate page loading and data holding from the visual representation!
        Scene scene;
        terrain::Terrain terrain(scene, nullptr, terrain::constants::MaxPages, terrain::constants::MaxPages);
        terrain.SetBaseFileName("Worlds/" + map->Name + "/" + map->Name + "/Terrain");

        terrain::Page* page = terrain.GetPage(x, y);
        ASSERT(page);

        const bool pagePrepared = page->Prepare();
        ASSERT(pagePrepared);

        for (uint32 y = 0; y < terrain::constants::TilesPerPage; ++y)
        {
            for (uint32 x = 0; x < terrain::constants::TilesPerPage; ++x)
            {
                // Initialize the terrain chunk
                m_chunks[y][x] = std::make_unique<TerrainChunk>();
                m_chunks[y][x]->m_minY = std::numeric_limits<float>::max();
                m_chunks[y][x]->m_maxY = std::numeric_limits<float>::lowest();
                m_chunks[y][x]->m_areaId = 0;
                m_chunks[y][x]->m_zoneId = 0;
                m_chunks[y][x]->m_terrainVertices.reserve(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile);
                m_chunks[y][x]->m_terrainIndices.reserve(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile * 6);

                // Fill the terrain chunk vertex, index and height data
                const size_t startX = x * (terrain::constants::VerticesPerTile - 1);
                const size_t startZ = y * (terrain::constants::VerticesPerTile - 1);
                const size_t endX = startX + terrain::constants::VerticesPerTile;
                const size_t endZ = startZ + terrain::constants::VerticesPerTile;

                constexpr float scale = terrain::constants::TileSize / (terrain::constants::VerticesPerTile - 1);

                for (size_t j = startZ; j < endZ; ++j)
                {
                    for (size_t i = startX; i < endX; ++i)
                    {
                        const float height = page->GetHeightAt(i, j);

                        Vector3 position = Vector3(scale * i, height, scale * j);

                        // TODO: Convert this position to the global position!
                        position.x += Bounds.min.x;
                        position.z += Bounds.min.z;

                        if (height < m_chunks[y][x]->m_minY)
                        {
                            m_chunks[y][x]->m_minY = height;
                        }

                        if (height > m_chunks[y][x]->m_maxY)
                        {
                            m_chunks[y][x]->m_maxY = height;
                        }

                        if (height < Bounds.min.y)
                        {
                            Bounds.min.y = height;
                        }
                        if (height > Bounds.max.y)
                        {
                            Bounds.max.y = height;
                        }

                        // Save height and vertex
                        m_chunks[y][x]->m_terrainVertices.push_back(position);
						m_chunks[y][x]->m_heights[(j - startZ) + (i - startX) * terrain::constants::VerticesPerTile] = height;

						if (j != endZ - 1 && i != endX - 1)
						{
                            // triangles
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ));

                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ + 1));
                            m_chunks[y][x]->m_terrainIndices.push_back(GetIndex(i - startX + 1, j - startZ));
						}
                    }
                }

                for (const auto& index : m_chunks[y][x]->m_terrainIndices)
                {
					ASSERT(index < m_chunks[y][x]->m_terrainVertices.size());
                }

                // TODO: Calculate chunk bounds and use these instead of page bounds, but whatever
                AABB tileBounds = Bounds;
                tileBounds.min.y = FLT_MIN;
                tileBounds.max.y = FLT_MAX;
                map->GetMapEntityInstancesInArea(tileBounds, m_chunks[y][x]->m_mapEntityInstances);

                // Adjust min and max Y values based on map entities
                for (auto& uniqueId : m_chunks[y][x]->m_mapEntityInstances)
                {
                    const auto* instance = map->GetMapEntityInstance(uniqueId);

                    const float minY = std::min(instance->Bounds.min.y, m_chunks[y][x]->m_minY);
                    const float maxY = std::max(instance->Bounds.max.y, m_chunks[y][x]->m_maxY);

                    if (m_chunks[y][x]->m_minY > minY)
                    {
                        //DLOG("Adjusted minY to " << minY << " because " << instance->Model->Filename);
						m_chunks[y][x]->m_minY = minY;
                    }
                    if (m_chunks[y][x]->m_maxY < maxY)
                    {
                        //DLOG("Adjusted maxY to " << minY << " because " << instance->Model->Filename);
                        m_chunks[y][x]->m_maxY = maxY;
                    }
                }
            }
        }
    }

    Map::Map(std::string mapName)
        : Name(std::move(mapName))
        , Id(0)
    {
		auto file = AssetRegistry::OpenFile("Worlds/" + Name + "/" + Name + ".hwld");
		if (!file)
		{
			ELOG("Failed to load map file " << Name << ": File does not exist");
			return;
		}

        m_ignoreUnhandledChunks = true;

        AddChunkHandler(*VersionChunkMagic, true, *this, &Map::ReadVersionChunk);

        // Read the world file
		io::StreamSource source{ *file };
		io::Reader reader{ source };
        if (!Read(reader))
        {
            ELOG("Failed to read world file!");
            return;
        }

        if (m_hasTerrain)
        {
            for (uint32 y = 0; y < terrain::constants::MaxPages; ++y)
            {
                for (uint32 x = 0; x < terrain::constants::MaxPages; ++x)
                {
                    std::stringstream strm;
                    strm << "Worlds/" << Name << "/" << Name << "/Terrain/" << std::setfill('0') << std::setw(2) << x << "_" << std::setfill('0') << std::setw(2) << y << ".tile";
                    m_hasPage[x][y] = AssetRegistry::HasFile(strm.str());
                }
            }
        }

        // Load entities
        DLOG("Loading map entities...");
		const std::vector<std::string> entities = AssetRegistry::ListFiles("Worlds/" + Name + "/" + Name + "/Entities/", "wobj");
        for (const auto& entityFilename : entities)
        {
			std::unique_ptr<std::istream> filePtr = AssetRegistry::OpenFile(entityFilename);
            if (!filePtr)
            {
				ELOG("Failed to load entity file " << entityFilename << ": File can not be opened");
                continue;
            }

			io::StreamSource entitySource{ *filePtr };
			io::Reader entityReader{ entitySource };
            WorldEntityLoader loader;
			if (!loader.Read(entityReader))
			{
				ELOG("Failed to load entity file " << entityFilename << ": Failed to read file");
				continue;
			}

			const auto& placement = loader.GetEntity();
            if (GetMapEntityInstance(placement.uniqueId) != nullptr)
            {
                WLOG("Duplicate entity id found: " << placement.uniqueId);
                continue;
            }

            const MapEntity* entity = GetMapEntity(placement.meshName);
            ASSERT(entity);

            Matrix4 transform;
            transform.MakeTransform(placement.position, placement.scale, placement.rotation);

            AABB bounds = entity->Bounds;
            bounds.Transform(transform);

            InsertMapEntityInstance(placement.uniqueId, std::make_unique<MapEntityInstance>(entity, bounds, transform));
        }

		DLOG("Loaded " << m_loadedMapEntityInstances.size() << " map entities!");
    }

    bool Map::HasPage(int x, int y) const
    {
        ASSERT(x >= 0 && y >= 0 && x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

        return m_hasPage[x][y];
    }

    const TerrainPage* Map::GetPage(int x, int y)
    {
        ASSERT(x >= 0 && y >= 0 && x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

        std::lock_guard guard(m_pageMutex);

        if (!m_hasPage[x][y])
            return nullptr;

        if (!m_pages[x][y])
            m_pages[x][y] = std::make_unique<TerrainPage>(this, x, y);

        return m_pages[x][y].get();
    }

    void Map::UnloadPage(int x, int y)
    {
        std::lock_guard guard(m_pageMutex);
        m_pages[x][y].reset();
    }

    const MapEntity* Map::GetMapEntity(const std::string& name)
    {
        std::lock_guard guard(m_mapEntityMutex);

        for (auto const& entity : m_loadedMapEntities)
        {
        	if (entity->Filename == name)
            {
                return entity.get();
            }
		}

        DLOG("Loading map entity " << name << "...");

        auto ret = std::make_unique<MapEntity>(name);
		m_loadedMapEntities.push_back(std::move(ret));

        return m_loadedMapEntities.back().get();
    }

    void Map::InsertMapEntityInstance(unsigned int uniqueId, std::unique_ptr<MapEntityInstance> instance)
    {
        std::lock_guard guard(m_mapEntityMutex);
        m_loadedMapEntityInstances[uniqueId] = std::move(instance);
    }

    const MapEntityInstance* Map::GetMapEntityInstance(unsigned int uniqueId) const
    {
        std::lock_guard guard(m_mapEntityMutex);

        auto const itr = m_loadedMapEntityInstances.find(uniqueId);
        return itr == m_loadedMapEntityInstances.end() ? nullptr : itr->second.get();
    }

    void Map::GetMapEntityInstancesInArea(const AABB& bounds, std::vector<uint32>& out_instanceIds) const
    {
		std::lock_guard guard(m_mapEntityMutex);

		for (auto const& [uniqueId, instance] : m_loadedMapEntityInstances)
		{
			if (bounds.Intersects(instance->Bounds))
			{
				out_instanceIds.push_back(uniqueId);
			}
		}
    }

    void Map::Serialize(io::Writer& writer) const
    {
        writer << io::write<uint32>('MAP1');

        if (m_hasTerrain)
        {
            writer << io::write<uint8>(1);

            uint8 hasPageMap[sizeof(m_hasPage) / 8] = {};
            for (auto y = 0; y < terrain::constants::MaxPages; ++y)
            {
                for (auto x = 0; x < terrain::constants::MaxPages; ++x)
                {
                    if (!m_hasPage[x][y])
                    {
                        continue;
                    }

                    auto const offset = y * terrain::constants::MaxPages + x;
                    auto const byte_offset = offset / 8;
                    auto const bit_offset = offset % 8;

                    hasPageMap[byte_offset] |= (1 << bit_offset);
                }
            }

            writer.WritePOD(hasPageMap);
        }
    }

    bool Map::ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *VersionChunkMagic);

        // Read version
        m_version = 0;
        reader >> io::read<uint32>(m_version);

        if (m_version <= world_version::Version_0_0_0_3)
        {
            // Check if version is supported
            if (m_version >= world_version::Version_0_0_0_1)
            {
                AddChunkHandler(*MeshNamesChunkMagic, false, *this, &Map::ReadMeshNamesChunk);

                if (m_version == world_version::Version_0_0_0_1)
                {
                    AddChunkHandler(*EntityChunkMagic, false, *this, &Map::ReadEntityChunk);
                    WLOG("World file " << m_version << " is deprecated, please update to the latest version!");
                }
                else if (m_version == world_version::Version_0_0_0_2)
                {
                    AddChunkHandler(*EntityChunkMagic, false, *this, &Map::ReadEntityChunkV2);
                    WLOG("World file " << m_version << " is deprecated, please update to the latest version!");
                }

                AddChunkHandler(*TerrainChunkMagic, false, *this, &Map::ReadTerrainChunk);
                return true;
            }
        }

        ELOG("Unsupported world version: " << m_version);
        return false;
    }

    bool Map::ReadMeshNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *MeshNamesChunkMagic);

        RemoveChunkHandler(*MeshNamesChunkMagic);

        if (m_version == world_version::Version_0_0_0_1)
        {
            AddChunkHandler(*EntityChunkMagic, false, *this, &Map::ReadEntityChunk);
        }
        else if (m_version == world_version::Version_0_0_0_2)
        {
            AddChunkHandler(*EntityChunkMagic, false, *this, &Map::ReadEntityChunkV2);
        }

        if (!m_meshNames.empty())
        {
            ELOG("Duplicate mesh names chunk detected!");
            return false;
        }

        const size_t contentStart = reader.getSource()->position();
        while (reader.getSource()->position() - contentStart < chunkSize)
        {
            String meshName;
            if (!(reader >> io::read_string(meshName)))
            {
                ELOG("Failed to read world file: Unexpected end of file");
                return false;
            }

            m_meshNames.emplace_back(std::move(meshName));
        }

        return reader;
    }

    bool Map::ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *EntityChunkMagic);

        if (m_meshNames.empty())
        {
            ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
            return false;
        }

        if (chunkSize != sizeof(MapEntityChunkContent))
        {
            ELOG("Entity chunk has incorrect chunk size, found " << log_hex_digit(chunkSize) << " bytes, expected " << log_hex_digit(sizeof(MapEntityChunkContent)) << " bytes");
            return false;
        }

        MapEntityChunkContent content;
        reader.readPOD(content);
        if (!reader)
        {
            ELOG("Failed to read map entity chunk content, unexpected end of file!");
            return false;
        }

        if (content.meshNameIndex >= m_meshNames.size())
        {
            ELOG("Map entity chunk references unknown mesh names!");
            return false;
        }

        if (GetMapEntityInstance(content.uniqueId) != nullptr)
        {
            WLOG("Duplicate entity id found: " << content.uniqueId);
            return reader;
        }

        const MapEntity* entity = GetMapEntity(m_meshNames[content.meshNameIndex]);
        ASSERT(entity);

        Matrix4 transform;
        transform.MakeTransform(content.position, content.scale, content.rotation);

        AABB bounds = entity->Bounds;
        bounds.Transform(transform);

        InsertMapEntityInstance(content.uniqueId, std::make_unique<MapEntityInstance>(entity, bounds, transform));

        return reader;
    }

    bool Map::ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *EntityChunkMagic);

        if (m_meshNames.empty())
        {
            ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
            return false;
        }

        uint32 uniqueId;
        uint32 meshNameIndex;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
        if (!(reader
            >> io::read<uint32>(uniqueId)
            >> io::read<uint32>(meshNameIndex)
            >> io::read<float>(position.x)
            >> io::read<float>(position.y)
            >> io::read<float>(position.z)
            >> io::read<float>(rotation.w)
            >> io::read<float>(rotation.x)
            >> io::read<float>(rotation.y)
            >> io::read<float>(rotation.z)
            >> io::read<float>(scale.x)
            >> io::read<float>(scale.y)
            >> io::read<float>(scale.z)
            ))
        {
            ELOG("Failed to read map entity chunk content, unexpected end of file!");
            return false;
        }

        ASSERT(position.IsValid());
        ASSERT(!rotation.IsNaN());
        ASSERT(scale.IsValid());

        if (meshNameIndex >= m_meshNames.size())
        {
            ELOG("Map entity chunk references unknown mesh names!");
            return false;
        }

        struct MaterialOverride
        {
            uint8 materialIndex;
            String materialName;
        };

        uint8 numMaterialOverrides;
        if (!(reader >> io::read<uint8>(numMaterialOverrides)))
        {
            ELOG("Failed to read material override count for map entity chunk, unexpected end of file!");
            return false;
        }

        for (uint8 i = 0; i < numMaterialOverrides; ++i)
        {
            MaterialOverride materialOverride;
            if (!(reader >> io::read<uint8>(materialOverride.materialIndex) >> io::read_container<uint16>(materialOverride.materialName)))
            {
                ELOG("Failed to read material override for map entity chunk, unexpected end of file!");
                return false;
            }
        }

        if (GetMapEntityInstance(uniqueId) != nullptr)
        {
            WLOG("Duplicate entity id found: " << uniqueId);
            return reader;
        }

		const MapEntity* entity = GetMapEntity(m_meshNames[meshNameIndex]);
        ASSERT(entity);

        Matrix4 transform;
        transform.MakeTransform(position, scale, rotation);

        AABB bounds = entity->Bounds;
		bounds.Transform(transform);

        InsertMapEntityInstance(uniqueId, std::make_unique<MapEntityInstance>(entity, bounds, transform));

        return reader;
    }

    bool Map::ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *TerrainChunkMagic);

        uint8 hasTerrain;
        if (!(reader >> io::read<uint8>(hasTerrain)))
        {
            ELOG("Failed to read terrain chunk: Unexpected end of file");
            return false;
        }

        if (hasTerrain)
        {
            // Ensure terrain is created
            m_hasTerrain = true;
        }

        // Read terrain default material
        String defaultMaterialName;
        if (!(reader >> io::read_container<uint16>(defaultMaterialName)))
        {
            ELOG("Failed to read terrain default material name: Unexpected end of file");
            return false;
        }

        return reader;
    }
}
