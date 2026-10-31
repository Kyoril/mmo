#include "material_function_manager.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "binary_io/stream_sink.h"
#include "base/chunk_reader.h"
#include "base/chunk_writer.h"

namespace mmo
{
    static std::unique_ptr<MaterialFunctionManager> s_instance = nullptr;

    MaterialFunctionManager& MaterialFunctionManager::Get()
    {
        if (!s_instance)
        {
            s_instance = std::make_unique<MaterialFunctionManager>();
        }

        return *s_instance;
    }

    MaterialFunctionPtr MaterialFunctionManager::Load(std::string_view filename)
    {
        // Normalize the filename and check if it's already loaded
        const std::string normalizedFilename(filename);
        auto it = m_materialFunctions.find(normalizedFilename);
        if (it != m_materialFunctions.end())
        {
            return it->second;
        }

        // Load the material function from file
        auto file = AssetRegistry::OpenFile(normalizedFilename);
        if (!file)
        {
            ELOG("Unable to open material function file: " << normalizedFilename);
            return nullptr;
        }

        // Create a new material function
        auto materialFunction = std::make_shared<MaterialFunction>(normalizedFilename);

        // Read the file
        io::StreamSource source(*file);
        io::Reader reader(source);

        // Create a chunk reader
        ChunkReader chunkReader;
        chunkReader.SetIgnoreUnhandledChunks(true);

        bool success = false;

        // Register chunk handlers
        chunkReader.AddChunkHandler(*ChunkMagic({'I', 'N', 'P', 'S'}), false, [&materialFunction](io::Reader& reader, uint32, uint32) -> bool
        {
            // Read the number of inputs
            uint32 numInputs = 0;
            if (!(reader >> numInputs))
            {
                ELOG("Failed to read material function input count");
                return false;
            }

            // Read each input
            for (uint32 i = 0; i < numInputs; ++i)
            {
                std::string name;
                uint8 type = 0;

                if (!(reader >> io::read_container<uint8>(name) >> io::read<uint8>(type)))
                {
                    ELOG("Failed to read material function input");
                    return false;
                }

                materialFunction->AddInputParam(name, static_cast<MaterialFunctionParamType>(type));
            }

            return true;
        });

        chunkReader.AddChunkHandler(*ChunkMagic({'O', 'U', 'T', 'P'}), false, [&materialFunction](io::Reader& reader, uint32, uint32) -> bool
        {
            // Read the number of inputs
            uint32 numOutputs = 0;
            if (!(reader >> numOutputs))
            {
                ELOG("Failed to read material function output count");
                return false;
            }

            // Read each output
            for (uint32 i = 0; i < numOutputs; ++i)
            {
                // Read the output
                std::string name;
                uint8 type = 0;

                if (!(reader >> io::read_container<uint8>(name) >> io::read<uint8>(type)))
                {
                    ELOG("Failed to read material function output");
                    return false;
                }

                materialFunction->AddOutputParam(name, static_cast<MaterialFunctionParamType>(type));
            }
            return true;
        });

        // Read the chunks
        success = chunkReader.Read(reader);
        if (!success)
        {
            ELOG("Failed to read material function file: " << normalizedFilename);
            return nullptr;
        }

        // Add the material function to the cache and return it
        m_materialFunctions[normalizedFilename] = materialFunction;
        return materialFunction;
    }

    MaterialFunctionPtr MaterialFunctionManager::CreateManual(const std::string_view name)
    {
        auto materialFunction = std::make_shared<MaterialFunction>(name);
        m_materialFunctions[std::string(name)] = materialFunction;
        return materialFunction;
    }

    void MaterialFunctionManager::Remove(std::string_view filename)
    {
        const std::string normalizedFilename(filename);
        m_materialFunctions.erase(normalizedFilename);
    }

    void MaterialFunctionManager::RemoveAllUnreferenced()
    {
        auto it = m_materialFunctions.begin();
        while (it != m_materialFunctions.end())
        {
            if (it->second.use_count() == 1)
            {
                it = m_materialFunctions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}