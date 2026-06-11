// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "float_curve.h"
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
	static const ChunkMagic FloatCurveVersionChunk = MakeChunkMagic('FVER');
	static const ChunkMagic FloatCurveKeysChunk = MakeChunkMagic('FKEY');

	FloatCurve::FloatCurve()
	{
		AddKey(0.0f, 0.0f);
		AddKey(1.0f, 1.0f);
		CalculateTangents();
	}

	FloatCurve::FloatCurve(float startValue, float endValue)
	{
		AddKey(0.0f, startValue);
		AddKey(1.0f, endValue);
		CalculateTangents();
	}

	FloatCurve::FloatCurve(std::vector<FloatKey> keys)
		: m_keys(std::move(keys))
	{
		SortKeys();

		if (m_keys.empty())
		{
			AddKey(0.0f, 0.0f);
			AddKey(1.0f, 1.0f);
		}

		CalculateTangents();
	}

	const FloatKey& FloatCurve::GetKey(size_t index) const
	{
		assert(index < m_keys.size() && "Key index out of range");
		return m_keys[index];
	}

	float FloatCurve::GetStartTime() const
	{
		return m_keys.empty() ? 0.0f : m_keys.front().time;
	}

	float FloatCurve::GetEndTime() const
	{
		return m_keys.empty() ? 1.0f : m_keys.back().time;
	}

	size_t FloatCurve::AddKey(float time, float value, float inTangent, float outTangent, uint8 tangentMode)
	{
		return AddKey(FloatKey(time, value, inTangent, outTangent, tangentMode));
	}

	size_t FloatCurve::AddKey(const FloatKey& key)
	{
		auto it = std::lower_bound(m_keys.begin(), m_keys.end(), key,
			[](const FloatKey& a, const FloatKey& b) { return a.time < b.time; });

		if (it != m_keys.end() && std::abs(it->time - key.time) < 1e-5f)
		{
			*it = key;
			return static_cast<size_t>(it - m_keys.begin());
		}

		const size_t insertIndex = static_cast<size_t>(it - m_keys.begin());
		m_keys.insert(it, key);

		if (key.tangentMode == 0)
		{
			CalculateTangents();
		}

		return insertIndex;
	}

	bool FloatCurve::RemoveKey(size_t index)
	{
		if (index >= m_keys.size())
		{
			return false;
		}

		m_keys.erase(m_keys.begin() + index);

		if (m_keys.size() < 2)
		{
			if (m_keys.empty())
			{
				AddKey(0.0f, 0.0f);
				AddKey(1.0f, 1.0f);
			}
			else if (m_keys.size() == 1)
			{
				const float time = m_keys[0].time < 0.5f ? 1.0f : 0.0f;
				AddKey(time, m_keys[0].value);
			}
		}

		CalculateTangents();
		return true;
	}

	bool FloatCurve::UpdateKey(size_t index, float time, float value, float inTangent, float outTangent, uint8 tangentMode)
	{
		if (index >= m_keys.size())
		{
			return false;
		}

		const FloatKey updatedKey(time, value, inTangent, outTangent, tangentMode);
		m_keys.erase(m_keys.begin() + index);
		AddKey(updatedKey);

		if (tangentMode == 0)
		{
			CalculateTangents();
		}

		return true;
	}

	bool FloatCurve::UpdateKey(size_t index, const FloatKey& key)
	{
		return UpdateKey(index, key.time, key.value, key.inTangent, key.outTangent, key.tangentMode);
	}

	float FloatCurve::Evaluate(float time) const
	{
		if (m_keys.empty())
		{
			return 0.0f;
		}

		if (m_keys.size() == 1)
		{
			return m_keys[0].value;
		}

		if (time <= m_keys[0].time)
		{
			return m_keys[0].value;
		}

		if (time >= m_keys.back().time)
		{
			return m_keys.back().value;
		}

		auto it = std::upper_bound(m_keys.begin(), m_keys.end(), time,
			[](float t, const FloatKey& key) { return t < key.time; });

		if (it == m_keys.begin())
		{
			return m_keys[0].value;
		}

		if (it == m_keys.end())
		{
			return m_keys.back().value;
		}

		const FloatKey& rightKey = *it;
		const FloatKey& leftKey = *(it - 1);

		return HermiteInterpolate(leftKey, rightKey, time);
	}

	void FloatCurve::SortKeys()
	{
		std::sort(m_keys.begin(), m_keys.end(),
			[](const FloatKey& a, const FloatKey& b) { return a.time < b.time; });
	}

	void FloatCurve::CalculateTangents()
	{
		if (m_keys.size() < 2)
		{
			return;
		}

		for (size_t i = 0; i < m_keys.size(); ++i)
		{
			if (m_keys[i].tangentMode != 0)
			{
				continue;
			}

			const FloatKey* prevKey = (i > 0) ? &m_keys[i - 1] : nullptr;
			const FloatKey* nextKey = (i < m_keys.size() - 1) ? &m_keys[i + 1] : nullptr;

			float inTangent = 0.0f, outTangent = 0.0f;
			CalculateAutoTangents(prevKey, m_keys[i], nextKey, inTangent, outTangent);

			m_keys[i].inTangent = inTangent;
			m_keys[i].outTangent = outTangent;
		}
	}

	bool FloatCurve::Deserialize(io::Reader& reader)
	{
		class FloatCurveReader : public ChunkReader
		{
		public:
			explicit FloatCurveReader(FloatCurve& curve) : m_curve(curve) {}

			bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
			{
				uint32 version;
				reader >> io::read<uint32>(version);

				if (version > float_curve_version::Version_1_0)
				{
					WLOG("Float curve using a newer version format (" << version << ") than supported");
				}

				return reader;
			}

			bool ReadKeysChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
			{
				uint32 keyCount;
				if (!(reader >> io::read<uint32>(keyCount)))
				{
					ELOG("Failed to read float curve key count");
					return false;
				}

				m_curve.m_keys.clear();

				for (uint32 i = 0; i < keyCount; ++i)
				{
					FloatKey key;
					if (!(reader
						>> io::read<float>(key.time)
						>> io::read<float>(key.value)
						>> io::read<float>(key.inTangent)
						>> io::read<float>(key.outTangent)
						>> io::read<uint8>(key.tangentMode)))
					{
						ELOG("Failed to read float curve key");
						return false;
					}

					m_curve.m_keys.push_back(key);
				}

				m_curve.SortKeys();
				return true;
			}

		private:
			FloatCurve& m_curve;
		};

		FloatCurveReader curveReader(*this);
		curveReader.AddChunkHandler(*FloatCurveVersionChunk, true, curveReader, &FloatCurveReader::ReadVersionChunk);
		curveReader.AddChunkHandler(*FloatCurveKeysChunk, true, curveReader, &FloatCurveReader::ReadKeysChunk);

		if (!curveReader.Read(reader))
		{
			ELOG("Failed to read float curve");
			return false;
		}

		return true;
	}

	void FloatCurve::Serialize(io::Writer& writer, FloatCurveVersion version) const
	{
		if (version == float_curve_version::Latest)
		{
			version = float_curve_version::Version_1_0;
		}

		{
			ChunkWriter versionChunkWriter(FloatCurveVersionChunk, writer);
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		{
			ChunkWriter keysChunkWriter(FloatCurveKeysChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(m_keys.size()));

			for (const FloatKey& key : m_keys)
			{
				writer
					<< io::write<float>(key.time)
					<< io::write<float>(key.value)
					<< io::write<float>(key.inTangent)
					<< io::write<float>(key.outTangent)
					<< io::write<uint8>(key.tangentMode);
			}

			keysChunkWriter.Finish();
		}
	}

	bool FloatCurve::operator==(const FloatCurve& other) const
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

	float FloatCurve::HermiteInterpolate(const FloatKey& key1, const FloatKey& key2, float time) const
	{
		const float deltaTime = key2.time - key1.time;
		if (deltaTime <= 0.0f)
		{
			return key1.value;
		}

		const float t = (time - key1.time) / deltaTime;

		if (t < 0.001f)
		{
			return key1.value;
		}

		if (t > 0.999f)
		{
			return key2.value;
		}

		const float m0 = key1.outTangent * deltaTime;
		const float m1 = key2.inTangent * deltaTime;

		const float t2 = t * t;
		const float t3 = t2 * t;
		const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 = t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 = t3 - t2;

		return h00 * key1.value + h10 * m0 + h01 * key2.value + h11 * m1;
	}

	void FloatCurve::CalculateAutoTangents(const FloatKey* prevKey, const FloatKey& key, const FloatKey* nextKey,
		float& outInTangent, float& outOutTangent) const
	{
		outInTangent = 0.0f;
		outOutTangent = 0.0f;

		if (!prevKey && !nextKey)
		{
			return;
		}

		if (!prevKey)
		{
			const float deltaTime = nextKey->time - key.time;
			if (deltaTime > 0.0f)
			{
				const float delta = (nextKey->value - key.value) / deltaTime;
				outInTangent = delta;
				outOutTangent = delta;
			}
			return;
		}

		if (!nextKey)
		{
			const float deltaTime = key.time - prevKey->time;
			if (deltaTime > 0.0f)
			{
				const float delta = (key.value - prevKey->value) / deltaTime;
				outInTangent = delta;
				outOutTangent = delta;
			}
			return;
		}

		const float dtPrev = key.time - prevKey->time;
		const float dtNext = nextKey->time - key.time;

		if (dtPrev > 0.0f && dtNext > 0.0f)
		{
			const float m0 = (key.value - prevKey->value) / dtPrev;
			const float m1 = (nextKey->value - key.value) / dtNext;
			const float tangent = (m0 + m1) * 0.5f;
			outInTangent = tangent;
			outOutTangent = tangent;
		}
	}
}
