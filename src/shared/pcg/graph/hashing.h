// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include "math/path_spline.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <cstring>
#include <set>

namespace mmo::pcg::graph
{
	/// @brief FNV-1a accumulator shared by every graph hash (content, controls, cache keys). Values are fed
	///        byte-wise in a fixed little-endian layout, so results are platform independent. Hash values are
	///        not stable across versions and must not be persisted.
	class Hasher
	{
	public:
		/// @brief Feeds raw bytes.
		void Bytes(const void* data, const size_t size)
		{
			const uint8* p = static_cast<const uint8*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				m_state ^= p[i];
				m_state *= 1099511628211ull;
			}
		}

		/// @brief Feeds a 64 bit value (little-endian bytes).
		void U64(const uint64 v)
		{
			for (int i = 0; i < 8; ++i)
			{
				m_state ^= static_cast<uint8>((v >> (i * 8)) & 0xFF);
				m_state *= 1099511628211ull;
			}
		}

		/// @brief Feeds the bit pattern of a float.
		void F32(const float v)
		{
			uint32 bits = 0;
			std::memcpy(&bits, &v, sizeof(bits));
			U64(bits);
		}

		/// @brief Feeds x, y, z.
		void Vec(const Vector3& v)
		{
			F32(v.x);
			F32(v.y);
			F32(v.z);
		}

		/// @brief Feeds w, x, y, z.
		void Quat(const Quaternion& q)
		{
			F32(q.w);
			F32(q.x);
			F32(q.y);
			F32(q.z);
		}

		/// @brief Feeds the length, then the characters.
		void Str(const String& s)
		{
			U64(s.size());
			Bytes(s.data(), s.size());
		}

		/// @brief Feeds the count, then every tag in order.
		void Tags(const std::set<String>& tags)
		{
			U64(tags.size());
			for (const String& t : tags)
			{
				Str(t);
			}
		}

		/// @brief Feeds the type index, then the value.
		void Value(const AttributeValue& v);

		/// @brief Feeds the count, then every name and value in order.
		void Attributes(const AttributeSet& set);

		/// @brief Feeds a spline: closed flag, interpolation and every field of every point.
		void Spline(const PathSpline& spline);

		/// @brief Gets the hash of everything fed so far.
		uint64 Result() const
		{
			return m_state;
		}

	private:
		uint64 m_state = 14695981039346656037ull;
	};

	/// @brief FNV-1a over the characters of a string (no length prefix).
	uint64 StringHash(const String& s);

	/// @brief Hash of an attribute set (see Hasher::Attributes).
	uint64 HashAttributes(const AttributeSet& attributes);

	/// @brief Hash of a spline (see Hasher::Spline).
	uint64 HashSpline(const PathSpline& spline);
}
