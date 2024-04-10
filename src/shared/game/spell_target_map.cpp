
#include "spell_target_map.h"

namespace mmo
{
	SpellTargetMap::SpellTargetMap()
		: m_targetMap(0)
		, m_unitTarget(0)
		, m_goTarget(0)
		, m_itemTarget(0)
		, m_corpseTarget(0)
		, m_srcX(0.0f), m_srcY(0.0f), m_srcZ(0.0f)
		, m_dstX(0.0f), m_dstY(0.0f), m_dstZ(0.0f)
	{
	}

	SpellTargetMap::SpellTargetMap(const SpellTargetMap& other) = default;

	void SpellTargetMap::GetSourceLocation(float& out_X, float& out_Y, float& out_Z) const
	{
		out_X = m_srcX;
		out_Y = m_srcY;
		out_Z = m_srcZ;
	}

	void SpellTargetMap::GetDestLocation(float& out_X, float& out_Y, float& out_Z) const
	{
		out_X = m_dstX;
		out_Y = m_dstY;
		out_Z = m_dstZ;
	}

	SpellTargetMap& SpellTargetMap::operator=(const SpellTargetMap& other)
	= default;

	io::Reader& operator>>(io::Reader& r, SpellTargetMap& targetMap)
	{
		if (!(r >> io::read<uint32>(targetMap.m_targetMap)))
		{
			return r;
		}

		// No targets
		if (targetMap.m_targetMap == spell_cast_target_flags::Self) {
			return r;
		}

		// Unit target
		if (targetMap.m_targetMap & (spell_cast_target_flags::Unit))
		{
			targetMap.m_unitTarget = 0;
			uint8 guidMark = 0;
			if (!(r >> io::read<uint8>(guidMark)))
			{
				return r;
			}

			for (int i = 0; i < 8; ++i)
			{
				if (guidMark & (static_cast<uint8>(1) << i))
				{
					uint8 bit;
					if (!(r >> io::read<uint8>(bit)))
					{
						return r;
					}

					targetMap.m_unitTarget |= (static_cast<uint64>(bit) << (i * 8));
				}
			}
		}

		// Object target
		if (targetMap.m_targetMap & (spell_cast_target_flags::Object))
		{
			targetMap.m_goTarget = 0;
			uint8 guidMark = 0;
			if (!(r >> io::read<uint8>(guidMark)))
			{
				return r;
			}

			for (int i = 0; i < 8; ++i)
			{
				if (guidMark & (static_cast<uint8>(1) << i))
				{
					uint8 bit;
					if (!(r >> io::read<uint8>(bit)))
					{
						return r;
					}
					targetMap.m_goTarget |= (static_cast<uint64>(bit) << (i * 8));
				}
			}
		}

		// Item target
		if (targetMap.m_targetMap & (spell_cast_target_flags::Item | spell_cast_target_flags::TradeItem))
		{
			targetMap.m_itemTarget = 0;
			uint8 guidMark = 0;
			if (!(r >> io::read<uint8>(guidMark)))
			{
				return r;
			}

			for (int i = 0; i < 8; ++i)
			{
				if (guidMark & (static_cast<uint8>(1) << i))
				{
					uint8 bit;
					if (!(r >> io::read<uint8>(bit)))
					{
						return r;
					}
					targetMap.m_itemTarget |= (static_cast<uint64>(bit) << (i * 8));
				}
			}
		}

		// Source location target
		if (targetMap.m_targetMap & spell_cast_target_flags::SourceLocation)
		{
			if (!(r
				>> io::read<float>(targetMap.m_srcX)
				>> io::read<float>(targetMap.m_srcY)
				>> io::read<float>(targetMap.m_srcZ)))
			{
				return r;
			}
		}

		// Dest location target
		if (targetMap.m_targetMap & spell_cast_target_flags::DestLocation)
		{
			if (!(r
				>> io::read<float>(targetMap.m_dstX)
				>> io::read<float>(targetMap.m_dstY)
				>> io::read<float>(targetMap.m_dstZ)))
			{
				return r;
			}
		}

		// String target
		if (targetMap.m_targetMap & spell_cast_target_flags::String)
		{
			char c = 0x00;
			do
			{
				if (!(r >> c))
				{
					return r;
				}
				if (c != 0)
				{
					targetMap.m_stringTarget.push_back(c);
				}
			} while (c != 0);
		}

		// Corpse target
		if (targetMap.m_targetMap & spell_cast_target_flags::Corpse)
		{
			targetMap.m_corpseTarget = 0;
			uint8 guidMark = 0;
			if (!(r >> io::read<uint8>(guidMark)))
			{
				return r;
			}

			for (int i = 0; i < 8; ++i)
			{
				if (guidMark & (static_cast<uint8>(1) << i))
				{
					uint8 bit;
					if (!(r >> io::read<uint8>(bit)))
					{
						return r;
					}

					targetMap.m_corpseTarget |= (static_cast<uint64>(bit) << (i * 8));
				}
			}
		}

		return r;
	}

	io::Writer& operator<<(io::Writer& w, SpellTargetMap const& targetMap)
	{
		namespace scf = spell_cast_target_flags;

		// Write mask
		w << io::write<uint32>(targetMap.m_targetMap);

		// Write GUID target values
		if (targetMap.m_targetMap & (scf::Unit | scf::Object | scf::Corpse))
		{
			if (targetMap.m_targetMap & scf::Unit)
			{
				// Write packed Unit GUID
				w << io::write_packed_guid(targetMap.m_unitTarget);
			}
			else if (targetMap.m_targetMap & (scf::Object))
			{
				// Write packed GO GUID
				w << io::write_packed_guid(targetMap.m_goTarget);
			}
			else if (targetMap.m_targetMap & (scf::Corpse))
			{
				// Write packed corpse GUID
				w << io::write_packed_guid(targetMap.m_corpseTarget);
			}
			else
			{
				// No GUID
				w << io::write<uint8>(0);
			}
		}

		// Item GUID
		if (targetMap.m_targetMap & (scf::Item | scf::TradeItem))
		{
			// Write packed item GUID
			w << io::write_packed_guid(targetMap.m_itemTarget);
		}

		// Source location
		if (targetMap.m_targetMap & scf::SourceLocation)
		{
			w
				<< io::write<float>(targetMap.m_srcX)
				<< io::write<float>(targetMap.m_srcY)
				<< io::write<float>(targetMap.m_srcZ);
		}

		// Dest location
		if (targetMap.m_targetMap & scf::DestLocation)
		{
			w
				<< io::write<float>(targetMap.m_dstX)
				<< io::write<float>(targetMap.m_dstY)
				<< io::write<float>(targetMap.m_dstZ);
		}

		// String target
		if (targetMap.m_targetMap & scf::String)
		{
			w
				<< io::write_range(targetMap.m_stringTarget) << io::write<uint8>(0);
		}

		return w;
	}
}