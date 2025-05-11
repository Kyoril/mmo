#pragma once

#include "base/typedefs.h"
#include "math/vector4.h"
#include <vector>

namespace io
{
    class Writer;
    class Reader;
}

namespace mmo
{
    namespace color_curve_version
    {
        enum Type
        {
            Latest = -1,
            Version_1_0 = 0x0100
        };
    }

    typedef color_curve_version::Type ColorCurveVersion;

    /**
     * @struct ColorKey
     * @brief Represents a key in a color curve.
     */
    struct ColorKey
    {
        float time;          ///< The time value of the key (x-coordinate)
        Vector4 color;       ///< The color value of the key (RGBA)
        Vector4 inTangent;   ///< The incoming tangent
        Vector4 outTangent;  ///< The outgoing tangent
        uint8 tangentMode;   ///< How tangents are calculated (0 = auto, 1 = user)

        ColorKey() = default;
        
        ColorKey(float t, const Vector4& col, 
                const Vector4& inTan = Vector4::Zero, 
                const Vector4& outTan = Vector4::Zero,
                uint8 tanMode = 0)
            : time(t)
            , color(col)
            , inTangent(inTan)
            , outTangent(outTan)
            , tangentMode(tanMode)
        {
        }
        
        bool operator==(const ColorKey& other) const
        {
            return time == other.time &&
                color == other.color &&
                inTangent == other.inTangent &&
                outTangent == other.outTangent &&
                tangentMode == other.tangentMode;
        }
    };

    /**
     * @class ColorCurve
     * @brief Stores and manipulates color values interpolated between keyframes.
     * 
     * ColorCurve allows for storing color values at specific time points (keyframes)
     * and provides methods for sampling interpolated values between keyframes.
     * The class also supports serialization to/from binary data.
     */
    class ColorCurve final
    {
    public:
        /**
         * @brief Default constructor.
         */
        ColorCurve();

        /**
         * @brief Creates a color curve with default start and end keys.
         * @param startColor The color at time 0.0
         * @param endColor The color at time 1.0
         */
        ColorCurve(const Vector4& startColor, const Vector4& endColor);

        /**
         * @brief Creates a color curve initialized with the provided keys.
         * @param keys An array of keys to initialize the curve with.
         */
        explicit ColorCurve(std::vector<ColorKey> keys);

        /**
         * @brief Gets all keys in the curve.
         * @return A const reference to the internal key collection.
         */
        const std::vector<ColorKey>& GetKeys() const { return m_keys; }

        /**
         * @brief Gets the number of keys in the curve.
         * @return The number of keys.
         */
        size_t GetKeyCount() const { return m_keys.size(); }

        /**
         * @brief Gets a key at the specified index.
         * @param index The zero-based index of the key to retrieve.
         * @return A reference to the key at the specified index.
         */
        const ColorKey& GetKey(size_t index) const;

        /**
         * @brief Gets the time of the first key in the curve.
         * @return The time of the first key, or 0.0f if there are no keys.
         */
        float GetStartTime() const;

        /**
         * @brief Gets the time of the last key in the curve.
         * @return The time of the last key, or 1.0f if there are no keys.
         */
        float GetEndTime() const;

        /**
         * @brief Adds a new key to the curve.
         * @param time The time value of the key.
         * @param color The color value of the key.
         * @param inTangent The incoming tangent.
         * @param outTangent The outgoing tangent.
         * @param tangentMode How tangents are calculated.
         * @return The index of the newly added key.
         */
        size_t AddKey(float time, const Vector4& color, 
                     const Vector4& inTangent = Vector4::Zero, 
                     const Vector4& outTangent = Vector4::Zero,
                     uint8 tangentMode = 0);

        /**
         * @brief Adds a key to the curve.
         * @param key The key to add.
         * @return The index of the newly added key.
         */
        size_t AddKey(const ColorKey& key);

        /**
         * @brief Removes a key at the specified index.
         * @param index The zero-based index of the key to remove.
         * @return True if the key was successfully removed, false otherwise.
         */
        bool RemoveKey(size_t index);

        /**
         * @brief Updates a key at the specified index.
         * @param index The zero-based index of the key to update.
         * @param time The new time value.
         * @param color The new color value.
         * @param inTangent The new incoming tangent.
         * @param outTangent The new outgoing tangent.
         * @param tangentMode The new tangent mode.
         * @return True if the key was successfully updated, false otherwise.
         */
        bool UpdateKey(size_t index, float time, const Vector4& color, 
                      const Vector4& inTangent = Vector4::Zero, 
                      const Vector4& outTangent = Vector4::Zero,
                      uint8 tangentMode = 0);

        /**
         * @brief Updates a key at the specified index.
         * @param index The zero-based index of the key to update.
         * @param key The new key data.
         * @return True if the key was successfully updated, false otherwise.
         */
        bool UpdateKey(size_t index, const ColorKey& key);

        /**
         * @brief Gets the interpolated color value at the specified time.
         * @param time The time at which to sample the curve.
         * @return The interpolated color value.
         * 
         * If time is before the first key, returns the first key's color.
         * If time is after the last key, returns the last key's color.
         * Otherwise, interpolates between the two surrounding keys.
         */
        Vector4 Evaluate(float time) const;

        /**
         * @brief Checks if the curve has any keys.
         * @return True if the curve has at least one key, false otherwise.
         */
        bool IsEmpty() const { return m_keys.empty(); }

        /**
         * @brief Clears all keys from the curve.
         */
        void Clear() { m_keys.clear(); }

        /**
         * @brief Sorts the keys by time.
         * 
         * This should be called after manually modifying the time values of keys to ensure
         * the curve maintains proper order for correct interpolation.
         */
        void SortKeys();

        /**
         * @brief Automatically calculates tangents for all keys in the curve.
         */
        void CalculateTangents();

        /**
         * @brief Deserializes a color curve from binary data.
         * @param reader The binary reader to read from.
         * @return True if deserialization was successful, false otherwise.
         */
        bool Deserialize(io::Reader& reader);

        /**
         * @brief Serializes the color curve to binary data.
         * @param writer The binary writer to write to.
         * @param version The version format to use for serialization.
         */
        void Serialize(io::Writer& writer, ColorCurveVersion version = color_curve_version::Latest) const;

        /**
         * @brief Equality comparison operator.
         * @param other The color curve to compare with.
         * @return True if the curves are equivalent, false otherwise.
         */
        bool operator==(const ColorCurve& other) const;

        /**
         * @brief Inequality comparison operator.
         * @param other The color curve to compare with.
         * @return True if the curves are not equivalent, false otherwise.
         */
        bool operator!=(const ColorCurve& other) const { return !(*this == other); }

    private:
        /**
         * @brief Finds the index of the key that contains the specified time.
         * @param time The time to search for.
         * @param outLeftIndex The index of the key just before the time.
         * @param outRightIndex The index of the key just after the time.
         * @return True if surrounding keys were found, false otherwise.
         */
        bool FindKeyIndicesForTime(float time, size_t& outLeftIndex, size_t& outRightIndex) const;

        /**
         * @brief Hermite interpolation between two keys.
         * @param key1 The first key.
         * @param key2 The second key.
         * @param time The time to interpolate at.
         * @return The interpolated color value.
         */
        Vector4 HermiteInterpolate(const ColorKey& key1, const ColorKey& key2, float time) const;

        /**
         * @brief Cubic interpolation helper function.
         * @param p0 Start point
         * @param p1 End point
         * @param m0 Start tangent
         * @param m1 End tangent
         * @param t Normalized time [0..1]
         * @return The interpolated value.
         */
        float CubicInterpolate(float p0, float p1, float m0, float m1, float t) const;

        /**
         * @brief Calculates auto-tangents for a key based on neighboring keys.
         * @param prevKey Previous key (or nullptr if none)
         * @param key The key to calculate tangents for
         * @param nextKey Next key (or nullptr if none)
         * @param outInTangent Output parameter for the calculated in-tangent
         * @param outOutTangent Output parameter for the calculated out-tangent
         */
        void CalculateAutoTangents(const ColorKey* prevKey, const ColorKey& key, 
                                   const ColorKey* nextKey, Vector4& outInTangent, 
                                   Vector4& outOutTangent) const;

    private:
        std::vector<ColorKey> m_keys; ///< The keys that define the curve
    };
}
