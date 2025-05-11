#include "color_curve.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "base/chunk_writer.h"
#include "base/chunk_reader.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace mmo
{
    // Define chunk magic values for serialization
    static const ChunkMagic ColorCurveVersionChunk = MakeChunkMagic('CVER');
    static const ChunkMagic ColorCurveKeysChunk = MakeChunkMagic('CKEY');
    
    ColorCurve::ColorCurve()
    {
        // Start with a default black-to-white gradient
        AddKey(0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        AddKey(1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        CalculateTangents();
    }

    ColorCurve::ColorCurve(const Vector4& startColor, const Vector4& endColor)
    {
        // Initialize with provided start and end colors
        AddKey(0.0f, startColor);
        AddKey(1.0f, endColor);
        CalculateTangents();
    }

    ColorCurve::ColorCurve(std::vector<ColorKey> keys)
        : m_keys(std::move(keys))
    {
        // Sort keys by time to ensure proper order
        SortKeys();
        
        // If no keys were provided, add default ones
        if (m_keys.empty())
        {
            AddKey(0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
            AddKey(1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        
        CalculateTangents();
    }

    const ColorKey& ColorCurve::GetKey(size_t index) const
    {
        assert(index < m_keys.size() && "Key index out of range");
        return m_keys[index];
    }

    float ColorCurve::GetStartTime() const
    {
        return m_keys.empty() ? 0.0f : m_keys.front().time;
    }

    float ColorCurve::GetEndTime() const
    {
        return m_keys.empty() ? 1.0f : m_keys.back().time;
    }

    size_t ColorCurve::AddKey(float time, const Vector4& color, 
                             const Vector4& inTangent, const Vector4& outTangent,
                             uint8 tangentMode)
    {
        ColorKey key(time, color, inTangent, outTangent, tangentMode);
        return AddKey(key);
    }

    size_t ColorCurve::AddKey(const ColorKey& key)
    {
        // Find where to insert the new key to maintain sorted order
        auto it = std::lower_bound(m_keys.begin(), m_keys.end(), key,
            [](const ColorKey& a, const ColorKey& b) { return a.time < b.time; });
        
        // If a key already exists at this time, update it instead
        if (it != m_keys.end() && std::abs(it->time - key.time) < 1e-5f)
        {
            *it = key;
            return static_cast<size_t>(it - m_keys.begin());
        }
        
        // Insert the new key
        size_t insertIndex = static_cast<size_t>(it - m_keys.begin());
        m_keys.insert(it, key);
        
        // If tangent mode is auto, recalculate tangents
        if (key.tangentMode == 0)
        {
            CalculateTangents();
        }
        
        return insertIndex;
    }

    bool ColorCurve::RemoveKey(size_t index)
    {
        if (index >= m_keys.size())
        {
            return false;
        }
        
        m_keys.erase(m_keys.begin() + index);
        
        // If we have fewer than 2 keys after removal, add default ones
        if (m_keys.size() < 2)
        {
            if (m_keys.empty())
            {
                AddKey(0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
                AddKey(1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else if (m_keys.size() == 1)
            {
                // Add a second key at the opposite end
                float time = m_keys[0].time < 0.5f ? 1.0f : 0.0f;
                AddKey(time, Vector4(time, time, time, 1.0f));
            }
        }
        
        // Recalculate tangents for affected keys
        CalculateTangents();
        
        return true;
    }

    bool ColorCurve::UpdateKey(size_t index, float time, const Vector4& color, 
                              const Vector4& inTangent, const Vector4& outTangent,
                              uint8 tangentMode)
    {
        if (index >= m_keys.size())
        {
            return false;
        }
        
        // Remove the key and add it back with the new time to ensure proper ordering
        ColorKey updatedKey(time, color, inTangent, outTangent, tangentMode);
        m_keys.erase(m_keys.begin() + index);
        AddKey(updatedKey);
        
        // If tangent mode is auto, recalculate tangents
        if (tangentMode == 0)
        {
            CalculateTangents();
        }
        
        return true;
    }

    bool ColorCurve::UpdateKey(size_t index, const ColorKey& key)
    {
        return UpdateKey(index, key.time, key.color, key.inTangent, key.outTangent, key.tangentMode);
    }

    Vector4 ColorCurve::Evaluate(float time) const
    {
        if (m_keys.empty())
        {
            return Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        
        if (m_keys.size() == 1)
        {
            return m_keys[0].color;
        }
        
        // Return first key color if time is before the first key
        if (time <= m_keys[0].time)
        {
            return m_keys[0].color;
        }
        
        // Return last key color if time is after or equal to the last key
        if (time >= m_keys.back().time)
        {
            return m_keys.back().color;
        }
        
        size_t leftIndex = 0;
        size_t rightIndex = 0;
        
        if (FindKeyIndicesForTime(time, leftIndex, rightIndex))
        {
            const ColorKey& leftKey = m_keys[leftIndex];
            const ColorKey& rightKey = m_keys[rightIndex];
            
            return HermiteInterpolate(leftKey, rightKey, time);
        }
        
        // Fallback if we couldn't find valid indices
        return m_keys[0].color;
    }

    void ColorCurve::SortKeys()
    {
        std::sort(m_keys.begin(), m_keys.end(),
            [](const ColorKey& a, const ColorKey& b) { return a.time < b.time; });
    }

    void ColorCurve::CalculateTangents()
    {
        if (m_keys.size() < 2)
        {
            return;
        }
        
        // Calculate tangents for each key based on its neighbors
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            // Skip keys with manual tangent mode
            if (m_keys[i].tangentMode != 0)
            {
                continue;
            }
            
            const ColorKey* prevKey = (i > 0) ? &m_keys[i-1] : nullptr;
            const ColorKey* nextKey = (i < m_keys.size() - 1) ? &m_keys[i+1] : nullptr;
            
            Vector4 inTangent, outTangent;
            CalculateAutoTangents(prevKey, m_keys[i], nextKey, inTangent, outTangent);
            
            m_keys[i].inTangent = inTangent;
            m_keys[i].outTangent = outTangent;
        }
    }

    bool ColorCurve::Deserialize(io::Reader& reader)
    {
        // Create a chunk reader to handle the file format
        class ColorCurveReader : public ChunkReader
        {
        public:
            explicit ColorCurveReader(ColorCurve& curve) : m_curve(curve) {}
            
            bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
            {
                uint32 version;
                reader >> io::read<uint32>(version);
                
                // Check if we can handle this version
                if (version > color_curve_version::Version_1_0)
                {
                    WLOG("Color curve using a newer version format (" << version << ") than this client supports (" 
                        << color_curve_version::Version_1_0 << ")");
                }
                
                return reader;
            }
            
            bool ReadKeysChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
            {
                uint32 keyCount;
                if (!(reader >> io::read<uint32>(keyCount)))
                {
                    ELOG("Failed to read color curve key count");
                    return false;
                }
                
                // Clear existing keys
                m_curve.m_keys.clear();
                
                // Read each key
                for (uint32 i = 0; i < keyCount; ++i)
                {
                    ColorKey key;
                    
                    if (!(reader
                        >> io::read<float>(key.time)
                        >> io::read<float>(key.color.x)
                        >> io::read<float>(key.color.y)
                        >> io::read<float>(key.color.z)
                        >> io::read<float>(key.color.w)
                        >> io::read<float>(key.inTangent.x)
                        >> io::read<float>(key.inTangent.y)
                        >> io::read<float>(key.inTangent.z)
                        >> io::read<float>(key.inTangent.w)
                        >> io::read<float>(key.outTangent.x)
                        >> io::read<float>(key.outTangent.y)
                        >> io::read<float>(key.outTangent.z)
                        >> io::read<float>(key.outTangent.w)
                        >> io::read<uint8>(key.tangentMode)))
                    {
                        ELOG("Failed to read color curve key");
                        return false;
                    }
                    
                    m_curve.m_keys.push_back(key);
                }
                
                // Make sure keys are sorted
                m_curve.SortKeys();
                
                return true;
            }
            
        private:
            ColorCurve& m_curve;
        };
        
        // Create the reader and run it
        ColorCurveReader curveReader(*this);
        curveReader.AddChunkHandler(*ColorCurveVersionChunk, true, curveReader, &ColorCurveReader::ReadVersionChunk);
        curveReader.AddChunkHandler(*ColorCurveKeysChunk, true, curveReader, &ColorCurveReader::ReadKeysChunk);
        
        if (!curveReader.Read(reader))
        {
            ELOG("Failed to read color curve");
            return false;
        }
        
        return true;
    }

    void ColorCurve::Serialize(io::Writer& writer, ColorCurveVersion version) const
    {
        // Always use latest version if not specified
        if (version == color_curve_version::Latest)
        {
            version = color_curve_version::Version_1_0;
        }
        
        // Write version chunk
        {
            ChunkWriter versionChunkWriter(ColorCurveVersionChunk, writer);
            writer << io::write<uint32>(version);
            versionChunkWriter.Finish();
        }
        
        // Write keys chunk
        {
            ChunkWriter keysChunkWriter(ColorCurveKeysChunk, writer);
            
            // Write number of keys
            writer << io::write<uint32>(static_cast<uint32>(m_keys.size()));
            
            // Write each key
            for (const ColorKey& key : m_keys)
            {
                writer
                    << io::write<float>(key.time)
                    << io::write<float>(key.color.x)
                    << io::write<float>(key.color.y)
                    << io::write<float>(key.color.z)
                    << io::write<float>(key.color.w)
                    << io::write<float>(key.inTangent.x)
                    << io::write<float>(key.inTangent.y)
                    << io::write<float>(key.inTangent.z)
                    << io::write<float>(key.inTangent.w)
                    << io::write<float>(key.outTangent.x)
                    << io::write<float>(key.outTangent.y)
                    << io::write<float>(key.outTangent.z)
                    << io::write<float>(key.outTangent.w)
                    << io::write<uint8>(key.tangentMode);
            }
            
            keysChunkWriter.Finish();
        }
    }

    bool ColorCurve::operator==(const ColorCurve& other) const
    {
        if (m_keys.size() != other.m_keys.size())
        {
            return false;
        }
        
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            if (!(m_keys[i] == other.m_keys[i]))
            {
                return false;
            }
        }
        
        return true;
    }

    bool ColorCurve::FindKeyIndicesForTime(float time, size_t& outLeftIndex, size_t& outRightIndex) const
    {
        if (m_keys.size() < 2 || time < m_keys.front().time || time > m_keys.back().time)
        {
            return false;
        }
        
        // Find the first key with time greater than the input time
        auto it = std::upper_bound(m_keys.begin(), m_keys.end(), time,
            [](float t, const ColorKey& key) { return t < key.time; });
        
        if (it == m_keys.begin() || it == m_keys.end())
        {
            return false;
        }
        
        outRightIndex = static_cast<size_t>(it - m_keys.begin());
        outLeftIndex = outRightIndex - 1;
        
        return true;
    }

    Vector4 ColorCurve::HermiteInterpolate(const ColorKey& key1, const ColorKey& key2, float time) const
    {
        // Calculate normalized t in [0,1] range between the two keys
        float deltaTime = key2.time - key1.time;
        if (deltaTime <= 0.0f)
        {
            return key1.color;
        }
        
        float t = (time - key1.time) / deltaTime;
        
        // Scale the tangents by the time difference
        Vector4 m0 = key1.outTangent * deltaTime;
        Vector4 m1 = key2.inTangent * deltaTime;
        
        // Perform cubic interpolation for each component
        Vector4 result;
        result.x = CubicInterpolate(key1.color.x, key2.color.x, m0.x, m1.x, t);
        result.y = CubicInterpolate(key1.color.y, key2.color.y, m0.y, m1.y, t);
        result.z = CubicInterpolate(key1.color.z, key2.color.z, m0.z, m1.z, t);
        result.w = CubicInterpolate(key1.color.w, key2.color.w, m0.w, m1.w, t);
        
        return result;
    }

    float ColorCurve::CubicInterpolate(float p0, float p1, float m0, float m1, float t) const
    {
        // Calculate cubic coefficients
        float t2 = t * t;
        float t3 = t2 * t;
        
        // Hermite basis functions
        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 = t3 - 2.0f * t2 + t;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 = t3 - t2;
        
        // Perform cubic interpolation
        return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
    }

    void ColorCurve::CalculateAutoTangents(const ColorKey* prevKey, const ColorKey& key, 
                                          const ColorKey* nextKey, Vector4& outInTangent, 
                                          Vector4& outOutTangent) const
    {
        // Default tangents to zero
        outInTangent = Vector4::Zero;
        outOutTangent = Vector4::Zero;
        
        if (!prevKey && !nextKey)
        {
            // Isolated key, no need for tangents
            return;
        }
        
        if (!prevKey)
        {
            // First key - use one-sided difference
            float deltaTime = nextKey->time - key.time;
            if (deltaTime > 0.0f)
            {
                Vector4 delta = (nextKey->color - key.color) / deltaTime;
                outInTangent = delta;
                outOutTangent = delta;
            }
            return;
        }
        
        if (!nextKey)
        {
            // Last key - use one-sided difference
            float deltaTime = key.time - prevKey->time;
            if (deltaTime > 0.0f)
            {
                Vector4 delta = (key.color - prevKey->color) / deltaTime;
                outInTangent = delta;
                outOutTangent = delta;
            }
            return;
        }
        
        // Interior key - use Catmull-Rom splines
        float dt_prev = key.time - prevKey->time;
        float dt_next = nextKey->time - key.time;
        
        if (dt_prev > 0.0f && dt_next > 0.0f)
        {
            // Calculate smooth tangent
            Vector4 m0 = (key.color - prevKey->color) / dt_prev;
            Vector4 m1 = (nextKey->color - key.color) / dt_next;
            
            // Average the two tangents for continuity
            Vector4 tangent = (m0 + m1) * 0.5f;
            
            outInTangent = tangent;
            outOutTangent = tangent;
        }
    }
}
