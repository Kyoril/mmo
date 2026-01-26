// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "world_model.h"

namespace io
{
    class Reader;
    class Writer;
}

namespace mmo
{
    /// @brief File format version numbers for world model files.
    namespace world_model_version
    {
        enum Type
        {
            Latest = -1,

            /// @brief Initial version with basic group/portal support.
            Version_1_0 = 0x0100,
        };
    }

    typedef world_model_version::Type WorldModelVersion;

    /// @brief Serializes world model data to a file.
    class WorldModelSerializer
    {
    public:
        /// @brief Serializes a world model to a writer.
        /// @param worldModel The world model to serialize.
        /// @param writer The writer to serialize to.
        /// @param version The file format version to use.
        void Serialize(const WorldModel& worldModel, io::Writer& writer, WorldModelVersion version = world_model_version::Latest);
    };

    /// @brief Deserializes world model data from a file.
    class WorldModelDeserializer : public ChunkReader
    {
    public:
        /// @brief Creates a new world model deserializer.
        /// @param worldModel The world model to populate with loaded data.
        explicit WorldModelDeserializer(WorldModel& worldModel);

    protected:
        /// @brief Reads the version chunk (MVER).
        bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the header chunk (MOHD).
        bool ReadHeaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the material names chunk (MOTX).
        bool ReadMaterialNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the group info chunk (MOGI).
        bool ReadGroupInfoChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the portal vertices chunk (MOPV).
        bool ReadPortalVerticesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the portal info chunk (MOPT).
        bool ReadPortalInfoChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the portal refs chunk (MOPR).
        bool ReadPortalRefsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the lights chunk (MOLT).
        bool ReadLightsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the doodad sets chunk (MODS).
        bool ReadDoodadSetsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the doodad names chunk (MODN).
        bool ReadDoodadNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the doodad definitions chunk (MODD).
        bool ReadDoodadDefsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the fog chunk (MFOG).
        bool ReadFogChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads the child WMO references chunk (MCWR).
        bool ReadChildWMOsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Reads a group chunk (MOGP).
        bool ReadGroupChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
        
        /// @brief Called when reading is finished.
        bool OnReadFinished() override;

    private:
        WorldModel& m_worldModel;
        WorldModelVersion m_version;
        
        // Temporary data for building portals
        std::vector<Vector3> m_portalVertices;
        
        struct PortalInfo
        {
            uint16 startVertex;
            uint16 vertexCount;
            float planeNormal[3];
            float planeDist;
            float width;
            float height;
            float rotation[4]; // x, y, z, w quaternion
        };
        std::vector<PortalInfo> m_portalInfos;
        
        struct PortalRef
        {
            uint16 portalIndex;
            uint16 groupIndex;
            int16 side;
        };
        std::vector<PortalRef> m_portalRefs;
    };
}
