// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_serializer.h"
#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
    // Chunk magic values
    static const ChunkMagic VersionChunk = MakeChunkMagic('MVER');
    static const ChunkMagic HeaderChunk = MakeChunkMagic('MOHD');
    static const ChunkMagic MaterialNamesChunk = MakeChunkMagic('MOTX');
    static const ChunkMagic GroupInfoChunk = MakeChunkMagic('MOGI');
    static const ChunkMagic PortalVerticesChunk = MakeChunkMagic('MOPV');
    static const ChunkMagic PortalInfoChunk = MakeChunkMagic('MOPT');
    static const ChunkMagic PortalRefsChunk = MakeChunkMagic('MOPR');
    static const ChunkMagic LightsChunk = MakeChunkMagic('MOLT');
    static const ChunkMagic DoodadSetsChunk = MakeChunkMagic('MODS');
    static const ChunkMagic DoodadNamesChunk = MakeChunkMagic('MODN');
    static const ChunkMagic DoodadDefsChunk = MakeChunkMagic('MODD');
    static const ChunkMagic FogChunk = MakeChunkMagic('MFOG');
    static const ChunkMagic GroupChunk = MakeChunkMagic('MOGP');
    
    // New chunks for mesh references and child WMOs
    static const ChunkMagic MeshRefsChunk = MakeChunkMagic('MMRF');
    static const ChunkMagic ChildWMOsChunk = MakeChunkMagic('MCWR');
    static const ChunkMagic GroupNameChunk = MakeChunkMagic('MGNM');
    static const ChunkMagic ContainmentVolumesChunk = MakeChunkMagic('MCVP');  // Containment Volume Planes

    // ==================== WorldModelSerializer ====================

    void WorldModelSerializer::Serialize(const WorldModel& worldModel, io::Writer& writer, WorldModelVersion version)
    {
        if (version == world_model_version::Latest)
        {
            version = world_model_version::Version_2_0;
        }

        // Write version chunk
        writer
            << io::write<uint32>(*VersionChunk)
            << io::write<uint32>(sizeof(uint32))
            << io::write<uint32>(version);

        // Calculate header size
        const uint32 headerSize = 64; // Fixed header size

        // Write header chunk
        writer
            << io::write<uint32>(*HeaderChunk)
            << io::write<uint32>(headerSize);

        // Header data
        writer
            << io::write<uint32>(static_cast<uint32>(worldModel.GetMaterialNames().size()))    // nMaterials
            << io::write<uint32>(static_cast<uint32>(worldModel.GetGroupCount()))              // nGroups
            << io::write<uint32>(static_cast<uint32>(worldModel.GetPortals().size()))          // nPortals
            << io::write<uint32>(static_cast<uint32>(worldModel.GetLights().size()))           // nLights
            << io::write<uint32>(static_cast<uint32>(worldModel.GetDoodadNames().size()))      // nDoodadNames
            << io::write<uint32>(static_cast<uint32>(worldModel.GetDoodads().size()))          // nDoodadDefs
            << io::write<uint32>(static_cast<uint32>(worldModel.GetDoodadSets().size()))       // nDoodadSets
            << io::write<uint32>(worldModel.GetAmbientColor());                                 // ambientColor

        // Bounding box
        const auto& bbox = worldModel.GetBoundingBox();
        writer
            << io::write<float>(bbox.min.x)
            << io::write<float>(bbox.min.y)
            << io::write<float>(bbox.min.z)
            << io::write<float>(bbox.max.x)
            << io::write<float>(bbox.max.y)
            << io::write<float>(bbox.max.z);

        // Flags and padding
        writer
            << io::write<uint32>(0)  // flags
            << io::write<uint32>(0); // padding

        // Write material names (MOTX)
        {
            size_t textureChunkSize = 0;
            for (const auto& name : worldModel.GetMaterialNames())
            {
                textureChunkSize += name.size() + 1;
            }

            writer
                << io::write<uint32>(*MaterialNamesChunk)
                << io::write<uint32>(static_cast<uint32>(textureChunkSize));

            for (const auto& name : worldModel.GetMaterialNames())
            {
                writer.Sink().Write(name.c_str(), name.size() + 1);
            }
        }

        // Write group info (MOGI)
        {
            const uint32 groupCount = static_cast<uint32>(worldModel.GetGroupCount());
            const uint32 groupInfoSize = groupCount * 32 + 4;
            
            writer
                << io::write<uint32>(*GroupInfoChunk)
                << io::write<uint32>(groupInfoSize)
                << io::write<uint32>(groupCount);

            for (size_t i = 0; i < worldModel.GetGroupCount(); ++i)
            {
                const auto* group = worldModel.GetGroup(i);
                if (group)
                {
                    const auto& bbox = group->GetBoundingBox();
                    writer
                        << io::write<uint32>(group->GetFlags())
                        << io::write<float>(bbox.min.x)
                        << io::write<float>(bbox.min.y)
                        << io::write<float>(bbox.min.z)
                        << io::write<float>(bbox.max.x)
                        << io::write<float>(bbox.max.y)
                        << io::write<float>(bbox.max.z)
                        << io::write<int32>(-1); // nameOffset
                }
            }
        }

        // Write portal vertices (MOPV)
        {
            size_t totalVertices = 0;
            for (const auto& portal : worldModel.GetPortals())
            {
                totalVertices += portal->GetWorldVertices().size();
            }

            const uint32 portalVerticesSize = static_cast<uint32>(totalVertices * 12 + 4);
            writer
                << io::write<uint32>(*PortalVerticesChunk)
                << io::write<uint32>(portalVerticesSize)
                << io::write<uint32>(static_cast<uint32>(totalVertices));

            for (const auto& portal : worldModel.GetPortals())
            {
                for (const auto& vertex : portal->GetWorldVertices())
                {
                    writer
                        << io::write<float>(vertex.x)
                        << io::write<float>(vertex.y)
                        << io::write<float>(vertex.z);
                }
            }
        }

        // Write portal info (MOPT)
        {
            const uint32 portalCount = static_cast<uint32>(worldModel.GetPortals().size());
            const uint32 portalInfoSize = portalCount * 44 + 4;
            
            writer
                << io::write<uint32>(*PortalInfoChunk)
                << io::write<uint32>(portalInfoSize)
                << io::write<uint32>(portalCount);

            uint16 vertexOffset = 0;
            for (const auto& portal : worldModel.GetPortals())
            {
                uint16 count = static_cast<uint16>(portal->GetWorldVertices().size());
                
                // Calculate plane from portal vertices
                // For simplicity, we use the portal's stored normal
                Vector3 normal(0, 0, 1);
                float planeDist = 0;

                // Get the rotation quaternion
                const Quaternion& rotation = portal->GetRotation();
                
                writer
                    << io::write<uint16>(vertexOffset)
                    << io::write<uint16>(count)
                    << io::write<float>(normal.x)
                    << io::write<float>(normal.y)
                    << io::write<float>(normal.z)
                    << io::write<float>(planeDist)
                    << io::write<float>(portal->GetWidth())
                    << io::write<float>(portal->GetHeight())
                    << io::write<float>(rotation.x)
                    << io::write<float>(rotation.y)
                    << io::write<float>(rotation.z)
                    << io::write<float>(rotation.w);

                vertexOffset += count;
            }
        }

        // Write lights (MOLT)
        {
            const uint32 lightCount = static_cast<uint32>(worldModel.GetLights().size());
            const uint32 lightsSize = lightCount * 48 + 4;
            
            writer
                << io::write<uint32>(*LightsChunk)
                << io::write<uint32>(lightsSize)
                << io::write<uint32>(lightCount);

            for (const auto& light : worldModel.GetLights())
            {
                writer
                    << io::write<uint8>(static_cast<uint8>(light.type))
                    << io::write<uint8>(light.useAttenuation ? 1 : 0)
                    << io::write<uint8>(0) // pad
                    << io::write<uint8>(0) // pad
                    << io::write<uint32>(light.color)
                    << io::write<float>(light.position.x)
                    << io::write<float>(light.position.y)
                    << io::write<float>(light.position.z)
                    << io::write<float>(light.intensity)
                    << io::write<float>(light.rotation.x)
                    << io::write<float>(light.rotation.y)
                    << io::write<float>(light.rotation.z)
                    << io::write<float>(light.rotation.w)
                    << io::write<float>(light.attenuationStart)
                    << io::write<float>(light.attenuationEnd);
            }
        }

        // Write doodad sets (MODS)
        {
            const uint32 setCount = static_cast<uint32>(worldModel.GetDoodadSets().size());
            const uint32 doodadSetsSize = setCount * 32 + 4;
            
            writer
                << io::write<uint32>(*DoodadSetsChunk)
                << io::write<uint32>(doodadSetsSize)
                << io::write<uint32>(setCount);

            for (const auto& set : worldModel.GetDoodadSets())
            {
                char nameBuffer[20] = {0};
                strncpy(nameBuffer, set.name.c_str(), 19);
                writer.Sink().Write(nameBuffer, 20);
                
                writer
                    << io::write<uint32>(set.startIndex)
                    << io::write<uint32>(set.count)
                    << io::write<uint32>(0); // pad
            }
        }

        // Write doodad names (MODN)
        {
            size_t namesChunkSize = 0;
            for (const auto& name : worldModel.GetDoodadNames())
            {
                namesChunkSize += name.size() + 1;
            }

            writer
                << io::write<uint32>(*DoodadNamesChunk)
                << io::write<uint32>(static_cast<uint32>(namesChunkSize));

            for (const auto& name : worldModel.GetDoodadNames())
            {
                writer.Sink().Write(name.c_str(), name.size() + 1);
            }
        }

        // Write doodad definitions (MODD)
        {
            const uint32 doodadCount = static_cast<uint32>(worldModel.GetDoodads().size());
            const uint32 doodadDefsSize = doodadCount * 40 + 4;
            
            writer
                << io::write<uint32>(*DoodadDefsChunk)
                << io::write<uint32>(doodadDefsSize)
                << io::write<uint32>(doodadCount);

            for (const auto& doodad : worldModel.GetDoodads())
            {
                writer
                    << io::write<uint32>(doodad.nameIndex)
                    << io::write<float>(doodad.position.x)
                    << io::write<float>(doodad.position.y)
                    << io::write<float>(doodad.position.z)
                    << io::write<float>(doodad.rotation.x)
                    << io::write<float>(doodad.rotation.y)
                    << io::write<float>(doodad.rotation.z)
                    << io::write<float>(doodad.rotation.w)
                    << io::write<float>(doodad.scale)
                    << io::write<uint32>(doodad.color);
            }
        }

        // Write fog (MFOG)
        {
            const uint32 fogCount = static_cast<uint32>(worldModel.GetFogs().size());
            const uint32 fogSize = fogCount * 48 + 4;
            
            writer
                << io::write<uint32>(*FogChunk)
                << io::write<uint32>(fogSize)
                << io::write<uint32>(fogCount);

            for (const auto& fog : worldModel.GetFogs())
            {
                writer
                    << io::write<uint32>(0) // flags
                    << io::write<float>(fog.position.x)
                    << io::write<float>(fog.position.y)
                    << io::write<float>(fog.position.z)
                    << io::write<float>(fog.innerRadius)
                    << io::write<float>(fog.outerRadius)
                    << io::write<float>(fog.fogEnd)
                    << io::write<float>(fog.fogStartScalar)
                    << io::write<uint32>(fog.fogColor)
                    << io::write<float>(fog.underwaterFogEnd)
                    << io::write<float>(fog.underwaterFogStartScalar)
                    << io::write<uint32>(fog.underwaterFogColor);
            }
        }

        // Build global portal refs list and track portalStart for each group
        std::vector<uint16> groupPortalStarts(worldModel.GetGroupCount());
        std::vector<uint16> groupPortalCounts(worldModel.GetGroupCount());
        std::vector<WorldModelPortalRef> allPortalRefs;
        
        for (size_t groupIdx = 0; groupIdx < worldModel.GetGroupCount(); ++groupIdx)
        {
            const auto* group = worldModel.GetGroup(groupIdx);
            if (group)
            {
                groupPortalStarts[groupIdx] = static_cast<uint16>(allPortalRefs.size());
                groupPortalCounts[groupIdx] = static_cast<uint16>(group->GetPortalRefs().size());
                
                for (const auto& ref : group->GetPortalRefs())
                {
                    allPortalRefs.push_back(ref);
                }
            }
        }

        // Write portal refs (MOPR)
        if (!allPortalRefs.empty())
        {
            const uint32 refCount = static_cast<uint32>(allPortalRefs.size());
            const uint32 portalRefsSize = refCount * 8 + 4;
            
            writer
                << io::write<uint32>(*PortalRefsChunk)
                << io::write<uint32>(portalRefsSize)
                << io::write<uint32>(refCount);

            for (const auto& ref : allPortalRefs)
            {
                writer
                    << io::write<uint16>(ref.portalIndex)
                    << io::write<uint16>(ref.groupIndex)
                    << io::write<int16>(ref.side)
                    << io::write<uint16>(0); // filler
            }
        }

        // Write child WMO references (MCWR)
        if (!worldModel.GetChildRefs().empty())
        {
            // Calculate chunk size
            size_t childWMOSize = 4; // count
            for (const auto& childRef : worldModel.GetChildRefs())
            {
                childWMOSize += 4; // string length
                childWMOSize += childRef.wmoPath.size() + 1; // path + null
                childWMOSize += 4; // name string length
                childWMOSize += childRef.name.size() + 1; // name + null
                childWMOSize += 12; // position (3 floats)
                childWMOSize += 16; // rotation (4 floats - quaternion)
                childWMOSize += 12; // scale (3 floats)
                childWMOSize += 1;  // visible
            }

            writer
                << io::write<uint32>(*ChildWMOsChunk)
                << io::write<uint32>(static_cast<uint32>(childWMOSize));

            writer << io::write<uint32>(static_cast<uint32>(worldModel.GetChildRefs().size()));
            
            for (const auto& childRef : worldModel.GetChildRefs())
            {
                // Write path
                writer << io::write<uint32>(static_cast<uint32>(childRef.wmoPath.size()));
                writer.Sink().Write(childRef.wmoPath.c_str(), childRef.wmoPath.size() + 1);
                
                // Write name
                writer << io::write<uint32>(static_cast<uint32>(childRef.name.size()));
                writer.Sink().Write(childRef.name.c_str(), childRef.name.size() + 1);
                
                // Write position
                writer
                    << io::write<float>(childRef.position.x)
                    << io::write<float>(childRef.position.y)
                    << io::write<float>(childRef.position.z);
                
                // Write rotation
                writer
                    << io::write<float>(childRef.rotation.w)
                    << io::write<float>(childRef.rotation.x)
                    << io::write<float>(childRef.rotation.y)
                    << io::write<float>(childRef.rotation.z);
                
                // Write scale
                writer
                    << io::write<float>(childRef.scale.x)
                    << io::write<float>(childRef.scale.y)
                    << io::write<float>(childRef.scale.z);
                
                // Write visible
                writer << io::write<uint8>(childRef.visible ? 1 : 0);
            }
        }

        // Write groups (MOGP)
        for (size_t groupIdx = 0; groupIdx < worldModel.GetGroupCount(); ++groupIdx)
        {
            const auto* group = worldModel.GetGroup(groupIdx);
            if (!group)
            {
                continue;
            }

            // Calculate group chunk size
            size_t groupDataSize = 68; // Header size

            // Add group name chunk size
            if (!group->GetName().empty())
            {
                groupDataSize += 8; // chunk header
                groupDataSize += group->GetName().size() + 1; // name + null
            }

            // Add mesh refs chunk size
            if (!group->GetMeshRefs().empty())
            {
                groupDataSize += 8; // chunk header
                groupDataSize += 4; // count
                for (const auto& meshRef : group->GetMeshRefs())
                {
                    groupDataSize += 4; // path string length
                    groupDataSize += meshRef.meshPath.size() + 1;
                    groupDataSize += 4; // name string length
                    groupDataSize += meshRef.name.size() + 1;
                    groupDataSize += 4; // material override length
                    groupDataSize += meshRef.materialOverride.size() + 1;
                    groupDataSize += 12; // position
                    groupDataSize += 16; // rotation
                    groupDataSize += 12; // scale
                    groupDataSize += 1;  // visible
                }
            }

            // Add containment volumes chunk size
            if (!group->GetContainmentVolumes().empty())
            {
                groupDataSize += 8; // chunk header
                groupDataSize += 4; // count
                for (const auto& volume : group->GetContainmentVolumes())
                {
                    groupDataSize += 4; // name string length
                    groupDataSize += volume.name.size() + 1;
                    groupDataSize += 24; // bounding box (6 floats)
                    groupDataSize += 4;  // plane count
                    groupDataSize += volume.planes.size() * 16; // 4 floats per plane
                }
            }

            writer
                << io::write<uint32>(*GroupChunk)
                << io::write<uint32>(static_cast<uint32>(groupDataSize));

            // Group header
            writer
                << io::write<int32>(-1) // groupName offset
                << io::write<int32>(-1); // descriptiveGroupName offset

            writer
                << io::write<uint32>(group->GetFlags());

            const auto& groupBBox = group->GetBoundingBox();
            writer
                << io::write<float>(groupBBox.min.x)
                << io::write<float>(groupBBox.min.y)
                << io::write<float>(groupBBox.min.z)
                << io::write<float>(groupBBox.max.x)
                << io::write<float>(groupBBox.max.y)
                << io::write<float>(groupBBox.max.z);

            writer
                << io::write<uint16>(groupPortalStarts[groupIdx])
                << io::write<uint16>(groupPortalCounts[groupIdx])
                << io::write<uint16>(0) // transBatchCount
                << io::write<uint16>(0) // intBatchCount
                << io::write<uint16>(0) // extBatchCount
                << io::write<uint16>(0) // padding
                << io::write<uint32>(0) // fogIds
                << io::write<uint32>(0) // groupLiquid
                << io::write<uint32>(0) // uniqueID
                << io::write<uint32>(0) // flags2
                << io::write<uint32>(0); // padding

            // Write group name (MGNM)
            if (!group->GetName().empty())
            {
                const uint32 nameSize = static_cast<uint32>(group->GetName().size() + 1);
                writer
                    << io::write<uint32>(*GroupNameChunk)
                    << io::write<uint32>(nameSize);
                
                writer.Sink().Write(group->GetName().c_str(), group->GetName().size() + 1);
            }

            // Write mesh references (MMRF)
            if (!group->GetMeshRefs().empty())
            {
                // Calculate chunk size
                size_t meshRefSize = 4; // count
                for (const auto& meshRef : group->GetMeshRefs())
                {
                    meshRefSize += 4; // path string length
                    meshRefSize += meshRef.meshPath.size() + 1;
                    meshRefSize += 4; // name string length
                    meshRefSize += meshRef.name.size() + 1;
                    meshRefSize += 4; // material override length
                    meshRefSize += meshRef.materialOverride.size() + 1;
                    meshRefSize += 12; // position
                    meshRefSize += 16; // rotation
                    meshRefSize += 12; // scale
                    meshRefSize += 1;  // visible
                }

                writer
                    << io::write<uint32>(*MeshRefsChunk)
                    << io::write<uint32>(static_cast<uint32>(meshRefSize));

                DLOG("Writing " << group->GetMeshRefs().size() << " mesh refs (size=" << meshRefSize << ")");

                writer << io::write<uint32>(static_cast<uint32>(group->GetMeshRefs().size()));

                for (const auto& meshRef : group->GetMeshRefs())
                {
                    // Write mesh path
                    writer << io::write<uint32>(static_cast<uint32>(meshRef.meshPath.size()));
                    writer.Sink().Write(meshRef.meshPath.c_str(), meshRef.meshPath.size() + 1);
                    
                    // Write name
                    writer << io::write<uint32>(static_cast<uint32>(meshRef.name.size()));
                    writer.Sink().Write(meshRef.name.c_str(), meshRef.name.size() + 1);
                    
                    // Write material override
                    writer << io::write<uint32>(static_cast<uint32>(meshRef.materialOverride.size()));
                    writer.Sink().Write(meshRef.materialOverride.c_str(), meshRef.materialOverride.size() + 1);
                    
                    // Write position
                    writer
                        << io::write<float>(meshRef.position.x)
                        << io::write<float>(meshRef.position.y)
                        << io::write<float>(meshRef.position.z);
                    
                    // Write rotation
                    writer
                        << io::write<float>(meshRef.rotation.w)
                        << io::write<float>(meshRef.rotation.x)
                        << io::write<float>(meshRef.rotation.y)
                        << io::write<float>(meshRef.rotation.z);
                    
                    // Write scale
                    writer
                        << io::write<float>(meshRef.scale.x)
                        << io::write<float>(meshRef.scale.y)
                        << io::write<float>(meshRef.scale.z);
                    
                    // Write visible
                    writer << io::write<uint8>(meshRef.visible ? 1 : 0);
                }
            }

            // Write containment volumes (MCVP)
            if (!group->GetContainmentVolumes().empty())
            {
                // Calculate chunk size
                size_t cvSize = 4; // count
                for (const auto& volume : group->GetContainmentVolumes())
                {
                    cvSize += 4; // name string length
                    cvSize += volume.name.size() + 1;
                    cvSize += 24; // bounding box (6 floats)
                    cvSize += 4;  // plane count
                    cvSize += volume.planes.size() * 16; // 4 floats per plane (normal + d)
                }

                writer
                    << io::write<uint32>(*ContainmentVolumesChunk)
                    << io::write<uint32>(static_cast<uint32>(cvSize));

                writer << io::write<uint32>(static_cast<uint32>(group->GetContainmentVolumes().size()));

                for (const auto& volume : group->GetContainmentVolumes())
                {
                    // Write name
                    writer << io::write<uint32>(static_cast<uint32>(volume.name.size()));
                    writer.Sink().Write(volume.name.c_str(), volume.name.size() + 1);
                    
                    // Write bounding box
                    writer
                        << io::write<float>(volume.boundingBox.min.x)
                        << io::write<float>(volume.boundingBox.min.y)
                        << io::write<float>(volume.boundingBox.min.z)
                        << io::write<float>(volume.boundingBox.max.x)
                        << io::write<float>(volume.boundingBox.max.y)
                        << io::write<float>(volume.boundingBox.max.z);
                    
                    // Write planes
                    writer << io::write<uint32>(static_cast<uint32>(volume.planes.size()));
                    for (const auto& plane : volume.planes)
                    {
                        writer
                            << io::write<float>(plane.normal.x)
                            << io::write<float>(plane.normal.y)
                            << io::write<float>(plane.normal.z)
                            << io::write<float>(plane.d);
                    }
                }
            }
        }
    }

    // ==================== WorldModelDeserializer ====================

    WorldModelDeserializer::WorldModelDeserializer(WorldModel& worldModel)
        : m_worldModel(worldModel)
        , m_version(world_model_version::Version_2_0)
    {
        AddChunkHandler(*VersionChunk, true, *this, &WorldModelDeserializer::ReadVersionChunk);
    }

    bool WorldModelDeserializer::ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *VersionChunk);
        RemoveChunkHandler(*VersionChunk);

        uint32 version = 0;
        if (!(reader >> io::read<uint32>(version)))
        {
            ELOG("Failed to read world model version!");
            return false;
        }

        m_version = static_cast<WorldModelVersion>(version);

        // Only V2 and newer are supported
        if (m_version < world_model_version::Version_2_0)
        {
            ELOG("Unsupported world model version: " << std::hex << m_version << ". Please re-export the file.");
            return false;
        }

        // Add handlers for the rest of the chunks
        AddChunkHandler(*HeaderChunk, false, *this, &WorldModelDeserializer::ReadHeaderChunk);
        AddChunkHandler(*MaterialNamesChunk, false, *this, &WorldModelDeserializer::ReadMaterialNamesChunk);
        AddChunkHandler(*GroupInfoChunk, false, *this, &WorldModelDeserializer::ReadGroupInfoChunk);
        AddChunkHandler(*PortalVerticesChunk, false, *this, &WorldModelDeserializer::ReadPortalVerticesChunk);
        AddChunkHandler(*PortalInfoChunk, false, *this, &WorldModelDeserializer::ReadPortalInfoChunk);
        AddChunkHandler(*PortalRefsChunk, false, *this, &WorldModelDeserializer::ReadPortalRefsChunk);
        AddChunkHandler(*LightsChunk, false, *this, &WorldModelDeserializer::ReadLightsChunk);
        AddChunkHandler(*DoodadSetsChunk, false, *this, &WorldModelDeserializer::ReadDoodadSetsChunk);
        AddChunkHandler(*DoodadNamesChunk, false, *this, &WorldModelDeserializer::ReadDoodadNamesChunk);
        AddChunkHandler(*DoodadDefsChunk, false, *this, &WorldModelDeserializer::ReadDoodadDefsChunk);
        AddChunkHandler(*FogChunk, false, *this, &WorldModelDeserializer::ReadFogChunk);
        AddChunkHandler(*ChildWMOsChunk, false, *this, &WorldModelDeserializer::ReadChildWMOsChunk);
        AddChunkHandler(*GroupChunk, false, *this, &WorldModelDeserializer::ReadGroupChunk);

        return reader;
    }

    bool WorldModelDeserializer::ReadHeaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *HeaderChunk);
        RemoveChunkHandler(*HeaderChunk);

        uint32 nMaterials, nGroups, nPortals, nLights;
        uint32 nDoodadNames, nDoodadDefs, nDoodadSets;
        uint32 ambientColor;

        reader
            >> io::read<uint32>(nMaterials)
            >> io::read<uint32>(nGroups)
            >> io::read<uint32>(nPortals)
            >> io::read<uint32>(nLights)
            >> io::read<uint32>(nDoodadNames)
            >> io::read<uint32>(nDoodadDefs)
            >> io::read<uint32>(nDoodadSets)
            >> io::read<uint32>(ambientColor);

        if (!reader)
        {
            return false;
        }

        m_worldModel.SetAmbientColor(ambientColor);

        // Read bounding box
        float minX, minY, minZ, maxX, maxY, maxZ;
        reader
            >> io::read<float>(minX)
            >> io::read<float>(minY)
            >> io::read<float>(minZ)
            >> io::read<float>(maxX)
            >> io::read<float>(maxY)
            >> io::read<float>(maxZ);

        if (!reader)
        {
            return false;
        }

        // Skip flags and padding
        uint32 flags, padding;
        reader
            >> io::read<uint32>(flags)
            >> io::read<uint32>(padding);

        return reader;
    }

    bool WorldModelDeserializer::ReadMaterialNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *MaterialNamesChunk);
        
        auto& materialNames = m_worldModel.GetMaterialNames();
        materialNames.clear();

        if (chunkSize > 0)
        {
            const size_t startPos = reader.getSource()->position();
            while (reader.getSource()->position() - startPos < chunkSize)
            {
                String name;
                if (!(reader >> io::read_string(name)))
                {
                    ELOG("Failed to read material name");
                    return false;
                }
                materialNames.push_back(std::move(name));
            }
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadGroupInfoChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *GroupInfoChunk);
        
        uint32 numGroups;
        reader >> io::read<uint32>(numGroups);
        
        auto& groupInfos = m_worldModel.GetGroupInfos();
        groupInfos.clear();
        groupInfos.reserve(numGroups);

        for (size_t i = 0; i < numGroups; ++i)
        {
            WorldModelGroupInfo info;
            float minX, minY, minZ, maxX, maxY, maxZ;

            reader
                >> io::read<uint32>(info.flags)
                >> io::read<float>(minX)
                >> io::read<float>(minY)
                >> io::read<float>(minZ)
                >> io::read<float>(maxX)
                >> io::read<float>(maxY)
                >> io::read<float>(maxZ)
                >> io::read<int32>(info.nameOffset);

            if (!reader)
            {
                return false;
            }

            info.boundingBox.min = Vector3(minX, minY, minZ);
            info.boundingBox.max = Vector3(maxX, maxY, maxZ);
            groupInfos.push_back(info);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadPortalVerticesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *PortalVerticesChunk);
        
        uint32 numVertices;
        reader >> io::read<uint32>(numVertices);
        
        m_portalVertices.clear();
        m_portalVertices.reserve(numVertices);

        for (size_t i = 0; i < numVertices; ++i)
        {
            float x, y, z;
            reader
                >> io::read<float>(x)
                >> io::read<float>(y)
                >> io::read<float>(z);

            if (!reader)
            {
                return false;
            }

            m_portalVertices.emplace_back(x, y, z);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadPortalInfoChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *PortalInfoChunk);
        
        uint32 numPortals;
        reader >> io::read<uint32>(numPortals);
        
        m_portalInfos.clear();
        m_portalInfos.reserve(numPortals);

        for (size_t i = 0; i < numPortals; ++i)
        {
            PortalInfo info;
            reader
                >> io::read<uint16>(info.startVertex)
                >> io::read<uint16>(info.vertexCount)
                >> io::read<float>(info.planeNormal[0])
                >> io::read<float>(info.planeNormal[1])
                >> io::read<float>(info.planeNormal[2])
                >> io::read<float>(info.planeDist)
                >> io::read<float>(info.width)
                >> io::read<float>(info.height)
                >> io::read<float>(info.rotation[0])
                >> io::read<float>(info.rotation[1])
                >> io::read<float>(info.rotation[2])
                >> io::read<float>(info.rotation[3]);

            if (!reader)
            {
                return false;
            }

            m_portalInfos.push_back(info);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadPortalRefsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *PortalRefsChunk);
        
        uint32 numRefs;
        reader >> io::read<uint32>(numRefs);
        
        m_portalRefs.clear();
        m_portalRefs.reserve(numRefs);

        for (size_t i = 0; i < numRefs; ++i)
        {
            PortalRef ref;
            uint16 filler;
            reader
                >> io::read<uint16>(ref.portalIndex)
                >> io::read<uint16>(ref.groupIndex)
                >> io::read<int16>(ref.side)
                >> io::read<uint16>(filler);

            if (!reader)
            {
                return false;
            }

            m_portalRefs.push_back(ref);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadLightsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *LightsChunk);
        
        uint32 numLights;
        reader >> io::read<uint32>(numLights);
        
        auto& lights = m_worldModel.GetLights();
        lights.clear();
        lights.reserve(numLights);

        for (size_t i = 0; i < numLights; ++i)
        {
            WorldModelLight light;
            uint8 type, useAtten, pad1, pad2;
            float posX, posY, posZ;
            float rotX, rotY, rotZ, rotW;

            reader
                >> io::read<uint8>(type)
                >> io::read<uint8>(useAtten)
                >> io::read<uint8>(pad1)
                >> io::read<uint8>(pad2)
                >> io::read<uint32>(light.color)
                >> io::read<float>(posX)
                >> io::read<float>(posY)
                >> io::read<float>(posZ)
                >> io::read<float>(light.intensity)
                >> io::read<float>(rotX)
                >> io::read<float>(rotY)
                >> io::read<float>(rotZ)
                >> io::read<float>(rotW)
                >> io::read<float>(light.attenuationStart)
                >> io::read<float>(light.attenuationEnd);

            if (!reader)
            {
                return false;
            }

            light.type = static_cast<WorldModelLight::LightType>(type);
            light.useAttenuation = useAtten != 0;
            light.position = Vector3(posX, posY, posZ);
            light.rotation = Quaternion(rotW, rotX, rotY, rotZ);
            lights.push_back(light);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadDoodadSetsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *DoodadSetsChunk);
        
        uint32 numSets;
        reader >> io::read<uint32>(numSets);
        
        auto& doodadSets = m_worldModel.GetDoodadSets();
        doodadSets.clear();
        doodadSets.reserve(numSets);

        for (size_t i = 0; i < numSets; ++i)
        {
            WorldModelDoodadSet set;
            char nameBuffer[20];
            uint32 pad;

            reader.getSource()->read(nameBuffer, 20);
            reader
                >> io::read<uint32>(set.startIndex)
                >> io::read<uint32>(set.count)
                >> io::read<uint32>(pad);

            if (!reader)
            {
                return false;
            }

            set.name = String(nameBuffer);
            doodadSets.push_back(set);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadDoodadNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *DoodadNamesChunk);
        
        auto& doodadNames = m_worldModel.GetDoodadNames();
        doodadNames.clear();

        if (chunkSize > 0)
        {
            const size_t startPos = reader.getSource()->position();
            while (reader.getSource()->position() - startPos < chunkSize)
            {
                String name;
                if (!(reader >> io::read_string(name)))
                {
                    ELOG("Failed to read doodad name");
                    return false;
                }
                doodadNames.push_back(std::move(name));
            }
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadDoodadDefsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *DoodadDefsChunk);
        
        uint32 numDoodads;
        reader >> io::read<uint32>(numDoodads);
        
        auto& doodads = m_worldModel.GetDoodads();
        doodads.clear();
        doodads.reserve(numDoodads);

        for (size_t i = 0; i < numDoodads; ++i)
        {
            WorldModelDoodad doodad;
            float posX, posY, posZ;
            float rotX, rotY, rotZ, rotW;

            reader
                >> io::read<uint32>(doodad.nameIndex)
                >> io::read<float>(posX)
                >> io::read<float>(posY)
                >> io::read<float>(posZ)
                >> io::read<float>(rotX)
                >> io::read<float>(rotY)
                >> io::read<float>(rotZ)
                >> io::read<float>(rotW)
                >> io::read<float>(doodad.scale)
                >> io::read<uint32>(doodad.color);

            if (!reader)
            {
                return false;
            }

            doodad.position = Vector3(posX, posY, posZ);
            doodad.rotation = Quaternion(rotW, rotX, rotY, rotZ);
            doodads.push_back(doodad);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadFogChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *FogChunk);
        
        uint32 numFogs;
        reader >> io::read<uint32>(numFogs);
        
        auto& fogs = m_worldModel.GetFogs();
        fogs.clear();
        fogs.reserve(numFogs);

        for (size_t i = 0; i < numFogs; ++i)
        {
            WorldModelFog fog;
            uint32 flags;
            float posX, posY, posZ;

            reader
                >> io::read<uint32>(flags)
                >> io::read<float>(posX)
                >> io::read<float>(posY)
                >> io::read<float>(posZ)
                >> io::read<float>(fog.innerRadius)
                >> io::read<float>(fog.outerRadius)
                >> io::read<float>(fog.fogEnd)
                >> io::read<float>(fog.fogStartScalar)
                >> io::read<uint32>(fog.fogColor)
                >> io::read<float>(fog.underwaterFogEnd)
                >> io::read<float>(fog.underwaterFogStartScalar)
                >> io::read<uint32>(fog.underwaterFogColor);

            if (!reader)
            {
                return false;
            }

            fog.position = Vector3(posX, posY, posZ);
            fogs.push_back(fog);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadGroupChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *GroupChunk);

        auto& group = m_worldModel.AddGroup();

        // Read group header
        int32 groupNameOffset, descriptiveNameOffset;
        uint32 flags;
        float minX, minY, minZ, maxX, maxY, maxZ;
        uint16 portalStart, portalCount;
        uint16 transBatchCount, intBatchCount, extBatchCount, padding;
        uint32 fogIds, groupLiquid, uniqueID, flags2, padding2;

        reader
            >> io::read<int32>(groupNameOffset)
            >> io::read<int32>(descriptiveNameOffset)
            >> io::read<uint32>(flags)
            >> io::read<float>(minX)
            >> io::read<float>(minY)
            >> io::read<float>(minZ)
            >> io::read<float>(maxX)
            >> io::read<float>(maxY)
            >> io::read<float>(maxZ)
            >> io::read<uint16>(portalStart)
            >> io::read<uint16>(portalCount)
            >> io::read<uint16>(transBatchCount)
            >> io::read<uint16>(intBatchCount)
            >> io::read<uint16>(extBatchCount)
            >> io::read<uint16>(padding)
            >> io::read<uint32>(fogIds)
            >> io::read<uint32>(groupLiquid)
            >> io::read<uint32>(uniqueID)
            >> io::read<uint32>(flags2)
            >> io::read<uint32>(padding2);

        if (!reader)
        {
            return false;
        }

        group.SetFlags(flags);
        AABB bbox;
        bbox.min = Vector3(minX, minY, minZ);
        bbox.max = Vector3(maxX, maxY, maxZ);
        group.SetBoundingBox(bbox);

        // Store portal info for this group to be processed in OnReadFinished
        GroupPortalInfo groupPortalInfo;
        groupPortalInfo.portalStart = portalStart;
        groupPortalInfo.portalCount = portalCount;
        m_groupPortalInfos.push_back(groupPortalInfo);

        // Read sub-chunks until we've read the expected amount
        const size_t chunkEndPos = reader.getSource()->position() + (chunkSize - 68);
        
        while (reader.getSource()->position() < chunkEndPos && reader)
        {
            uint32 subChunkId, subChunkSize;
            reader
                >> io::read<uint32>(subChunkId)
                >> io::read<uint32>(subChunkSize);

            if (!reader)
            {
                break;
            }

            const size_t subChunkEndPos = reader.getSource()->position() + subChunkSize;

            if (subChunkId == *GroupNameChunk)
            {
                // Read group name
                std::string name;
                name.resize(subChunkSize);
                reader.getSource()->read(&name[0], subChunkSize);
                // Remove null terminator if present
                if (!name.empty() && name.back() == '\0')
                {
                    name.resize(name.size() - 1);
                }
                group.SetName(name);
            }
            else if (subChunkId == *MeshRefsChunk)
            {
                // Read mesh references
                uint32 count;
                reader >> io::read<uint32>(count);

                DLOG("Reading " << count << " mesh refs from chunk (size=" << subChunkSize << ")");

                for (uint32 i = 0; i < count; ++i)
                {
                    WorldModelMeshRef meshRef;

                    // Read mesh path
                    uint32 pathLen;
                    reader >> io::read<uint32>(pathLen);
                    meshRef.meshPath.resize(pathLen + 1);
                    reader.getSource()->read(&meshRef.meshPath[0], pathLen + 1);
                    if (!meshRef.meshPath.empty() && meshRef.meshPath.back() == '\0')
                    {
                        meshRef.meshPath.resize(pathLen);
                    }

                    // Read name
                    uint32 nameLen;
                    reader >> io::read<uint32>(nameLen);
                    meshRef.name.resize(nameLen + 1);
                    reader.getSource()->read(&meshRef.name[0], nameLen + 1);
                    if (!meshRef.name.empty() && meshRef.name.back() == '\0')
                    {
                        meshRef.name.resize(nameLen);
                    }

                    // Read material override
                    uint32 matLen;
                    reader >> io::read<uint32>(matLen);
                    meshRef.materialOverride.resize(matLen + 1);
                    reader.getSource()->read(&meshRef.materialOverride[0], matLen + 1);
                    if (!meshRef.materialOverride.empty() && meshRef.materialOverride.back() == '\0')
                    {
                        meshRef.materialOverride.resize(matLen);
                    }

                    // Read transform
                    float px, py, pz;
                    float rw, rx, ry, rz;
                    float sx, sy, sz;
                    uint8 visible;

                    reader
                        >> io::read<float>(px)
                        >> io::read<float>(py)
                        >> io::read<float>(pz)
                        >> io::read<float>(rw)
                        >> io::read<float>(rx)
                        >> io::read<float>(ry)
                        >> io::read<float>(rz)
                        >> io::read<float>(sx)
                        >> io::read<float>(sy)
                        >> io::read<float>(sz)
                        >> io::read<uint8>(visible);

                    meshRef.position = Vector3(px, py, pz);
                    meshRef.rotation = Quaternion(rw, rx, ry, rz);
                    meshRef.scale = Vector3(sx, sy, sz);
                    meshRef.visible = visible != 0;
                    group.AddMeshRef(meshRef);
                }
            }
            else if (subChunkId == *ContainmentVolumesChunk)
            {
                // Read containment volumes
                uint32 count;
                reader >> io::read<uint32>(count);

                DLOG("Reading " << count << " containment volumes from chunk");

                for (uint32 i = 0; i < count; ++i)
                {
                    ContainmentVolume volume;

                    // Read name
                    uint32 nameLen;
                    reader >> io::read<uint32>(nameLen);
                    volume.name.resize(nameLen + 1);
                    reader.getSource()->read(&volume.name[0], nameLen + 1);
                    if (!volume.name.empty() && volume.name.back() == '\0')
                    {
                        volume.name.resize(nameLen);
                    }

                    // Read bounding box
                    float minX, minY, minZ, maxX, maxY, maxZ;
                    reader
                        >> io::read<float>(minX)
                        >> io::read<float>(minY)
                        >> io::read<float>(minZ)
                        >> io::read<float>(maxX)
                        >> io::read<float>(maxY)
                        >> io::read<float>(maxZ);
                    
                    volume.boundingBox.min = Vector3(minX, minY, minZ);
                    volume.boundingBox.max = Vector3(maxX, maxY, maxZ);

                    // Read planes
                    uint32 planeCount;
                    reader >> io::read<uint32>(planeCount);
                    
                    volume.planes.reserve(planeCount);
                    for (uint32 j = 0; j < planeCount; ++j)
                    {
                        float nx, ny, nz, d;
                        reader
                            >> io::read<float>(nx)
                            >> io::read<float>(ny)
                            >> io::read<float>(nz)
                            >> io::read<float>(d);
                        
                        volume.planes.emplace_back(nx, ny, nz, d);
                    }

                    DLOG("  ContainmentVolume[" << i << "]: name='" << volume.name 
                         << "' planes=" << volume.planes.size());

                    group.AddContainmentVolume(volume);
                }
            }

            // Always seek to end of sub-chunk to ensure alignment for next iteration
            reader.getSource()->seek(subChunkEndPos);
        }

        return reader;
    }

    bool WorldModelDeserializer::ReadChildWMOsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
    {
        ASSERT(chunkHeader == *ChildWMOsChunk);

        uint32 count;
        reader >> io::read<uint32>(count);

        for (uint32 i = 0; i < count; ++i)
        {
            WorldModelChildRef childRef;

            // Read WMO path
            uint32 pathLen;
            reader >> io::read<uint32>(pathLen);
            childRef.wmoPath.resize(pathLen + 1);
            reader.getSource()->read(&childRef.wmoPath[0], pathLen + 1);
            if (!childRef.wmoPath.empty() && childRef.wmoPath.back() == '\0')
            {
                childRef.wmoPath.resize(pathLen);
            }

            // Read name
            uint32 nameLen;
            reader >> io::read<uint32>(nameLen);
            childRef.name.resize(nameLen + 1);
            reader.getSource()->read(&childRef.name[0], nameLen + 1);
            if (!childRef.name.empty() && childRef.name.back() == '\0')
            {
                childRef.name.resize(nameLen);
            }

            // Read transform
            float px, py, pz;
            float rw, rx, ry, rz;
            float sx, sy, sz;
            uint8 visible;

            reader
                >> io::read<float>(px)
                >> io::read<float>(py)
                >> io::read<float>(pz)
                >> io::read<float>(rw)
                >> io::read<float>(rx)
                >> io::read<float>(ry)
                >> io::read<float>(rz)
                >> io::read<float>(sx)
                >> io::read<float>(sy)
                >> io::read<float>(sz)
                >> io::read<uint8>(visible);

            if (!reader)
            {
                return false;
            }

            childRef.position = Vector3(px, py, pz);
            childRef.rotation = Quaternion(rw, rx, ry, rz);
            childRef.scale = Vector3(sx, sy, sz);
            childRef.visible = visible != 0;

            m_worldModel.AddChildRef(childRef);
        }

        return reader;
    }

    bool WorldModelDeserializer::OnReadFinished()
    {
        // Build portals from loaded data
        for (size_t i = 0; i < m_portalInfos.size(); ++i)
        {
            auto& portal = m_worldModel.AddPortal();
            const auto& info = m_portalInfos[i];

            // Calculate portal center from vertices
            Vector3 center = Vector3::Zero;

            for (uint16 j = 0; j < info.vertexCount; ++j)
            {
                const size_t vertexIndex = info.startVertex + j;
                if (vertexIndex < m_portalVertices.size())
                {
                    center += m_portalVertices[vertexIndex];
                }
            }

            if (info.vertexCount > 0)
            {
                center /= static_cast<float>(info.vertexCount);
            }

            Quaternion rotation(info.rotation[0], info.rotation[1], info.rotation[2], info.rotation[3]);
            portal.SetTransform(center, rotation, Vector3::UnitScale);
            portal.SetDimensions(info.width, info.height);
        }

        // Assign portal refs to groups
        for (size_t groupIdx = 0; groupIdx < m_groupPortalInfos.size() && groupIdx < m_worldModel.GetGroupCount(); ++groupIdx)
        {
            const auto& groupPortalInfo = m_groupPortalInfos[groupIdx];
            auto* group = m_worldModel.GetGroup(groupIdx);
            if (!group)
            {
                continue;
            }

            auto& portalRefs = group->GetPortalRefs();
            for (uint16 i = 0; i < groupPortalInfo.portalCount; ++i)
            {
                const size_t refIndex = groupPortalInfo.portalStart + i;
                if (refIndex < m_portalRefs.size())
                {
                    const auto& ref = m_portalRefs[refIndex];
                    WorldModelPortalRef wmRef;
                    wmRef.portalIndex = ref.portalIndex;
                    wmRef.groupIndex = ref.groupIndex;
                    wmRef.side = ref.side;
                    portalRefs.push_back(wmRef);
                }
            }
        }

        // Clear temporary data
        m_portalVertices.clear();
        m_portalInfos.clear();
        m_portalRefs.clear();
        m_groupPortalInfos.clear();

        RemoveAllChunkHandlers();
        return ChunkReader::OnReadFinished();
    }
}
