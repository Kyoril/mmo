// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include <vector>

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{
	namespace float_curve_version
	{
		enum Type
		{
			Latest = -1,
			Version_1_0 = 0x0100
		};
	}

	typedef float_curve_version::Type FloatCurveVersion;

	/**
	 * @struct FloatKey
	 * @brief Represents a single keyframe in a scalar (float) curve.
	 */
	struct FloatKey
	{
		float time;         ///< The normalized time of the key (x-coordinate), typically [0..1]
		float value;        ///< The scalar value of the key
		float inTangent;    ///< The incoming tangent
		float outTangent;   ///< The outgoing tangent
		uint8 tangentMode;  ///< How tangents are calculated (0 = auto, 1 = user)

		FloatKey() = default;

		FloatKey(float t, float v, float inTan = 0.0f, float outTan = 0.0f, uint8 tanMode = 0)
			: time(t)
			, value(v)
			, inTangent(inTan)
			, outTangent(outTan)
			, tangentMode(tanMode)
		{
		}

		bool operator==(const FloatKey& other) const
		{
			return time == other.time &&
				value == other.value &&
				inTangent == other.inTangent &&
				outTangent == other.outTangent &&
				tangentMode == other.tangentMode;
		}
	};

	/**
	 * @class FloatCurve
	 * @brief Stores and interpolates a scalar value across normalized time using Hermite keys.
	 *
	 * This is the scalar counterpart of @ref ColorCurve and is used for animating particle
	 * properties such as size or alpha over a particle's lifetime. The curve always keeps at
	 * least two keys so it can be evaluated safely.
	 */
	class FloatCurve final
	{
	public:
		/// @brief Creates a flat curve from 0.0 at t=0 to 1.0 at t=1.
		FloatCurve();

		/// @brief Creates a curve interpolating between a start and an end value.
		FloatCurve(float startValue, float endValue);

		/// @brief Creates a curve initialized with the provided keys.
		explicit FloatCurve(std::vector<FloatKey> keys);

		[[nodiscard]] const std::vector<FloatKey>& GetKeys() const { return m_keys; }

		[[nodiscard]] size_t GetKeyCount() const { return m_keys.size(); }

		[[nodiscard]] const FloatKey& GetKey(size_t index) const;

		[[nodiscard]] float GetStartTime() const;

		[[nodiscard]] float GetEndTime() const;

		/// @brief Adds a key and returns its index. Keeps keys time-sorted.
		size_t AddKey(float time, float value, float inTangent = 0.0f, float outTangent = 0.0f, uint8 tangentMode = 0);

		size_t AddKey(const FloatKey& key);

		/// @brief Removes a key by index. The curve keeps at least two keys.
		bool RemoveKey(size_t index);

		bool UpdateKey(size_t index, float time, float value, float inTangent = 0.0f, float outTangent = 0.0f, uint8 tangentMode = 0);

		bool UpdateKey(size_t index, const FloatKey& key);

		/// @brief Evaluates the curve at a given (typically normalized) time.
		[[nodiscard]] float Evaluate(float time) const;

		[[nodiscard]] bool IsEmpty() const { return m_keys.empty(); }

		void Clear() { m_keys.clear(); }

		void SortKeys();

		void CalculateTangents();

		bool Deserialize(io::Reader& reader);

		void Serialize(io::Writer& writer, FloatCurveVersion version = float_curve_version::Latest) const;

		bool operator==(const FloatCurve& other) const;

		bool operator!=(const FloatCurve& other) const { return !(*this == other); }

	private:
		float HermiteInterpolate(const FloatKey& key1, const FloatKey& key2, float time) const;

		void CalculateAutoTangents(const FloatKey* prevKey, const FloatKey& key, const FloatKey* nextKey,
			float& outInTangent, float& outOutTangent) const;

	private:
		std::vector<FloatKey> m_keys;
	};
}
