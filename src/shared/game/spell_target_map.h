#pragma once

#include "base/typedefs.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"

#include "spell.h"

namespace mmo
{
	namespace spell_interrupt_flags
	{
		enum Type
		{
			/// Used when cast is cancelled for no specific reason (always interrupts the cast)
			Any = 0x00,

			/// Interrupted on movement
			Movement = 0x01,

			/// Affected by spell delay?
			PushBack = 0x02,

			/// Kick / Counter Spell
			Interrupt = 0x04,

			/// Interrupted on auto attack?
			AutoAttack = 0x08,

			/// Interrupted on direct damage
			Damage = 0x10
		};
	}

	typedef spell_interrupt_flags::Type SpellInterruptFlags;

	namespace spell_effect_targets
	{
		enum Type
		{
			Caster,
			NearbyEnemy,
			NearbyParty,
			NearbyAlly,
			Pet,
			TargetEnemy,
			SourceArea,
			TargetArea,
			Home,
			SourceAreaEnemy,
			TargetAreaEnemy,
			DatabaseLocation,
			CasterLocation,
			CasterAreaParty,
			TargetAlly,
			ObjectTarget,
			ConeEnemy,
			TargetAny,
			Instigator,

			Count_
		};
	}

	typedef spell_effect_targets::Type SpellEffectTargets;

	class SpellTargetMap final
	{
		friend io::Writer& operator << (io::Writer& w, SpellTargetMap const& targetMap);
		friend io::Reader& operator >> (io::Reader& r, SpellTargetMap& targetMap);

	public:
		explicit SpellTargetMap();
		SpellTargetMap(const SpellTargetMap& other);

		///
		uint32 GetTargetMap() const
		{
			return m_targetMap;
		}

		///
		uint64 GetUnitTarget() const
		{
			return m_unitTarget;
		}

		///
		uint64 GetGOTarget() const
		{
			return m_goTarget;
		}

		///
		uint64 GetItemTarget() const
		{
			return m_itemTarget;
		}

		///
		uint64 GetCorpseTarget() const
		{
			return m_corpseTarget;
		}

		///
		void GetSourceLocation(float& out_X, float& out_Y, float& out_Z) const;

		///
		void GetDestLocation(float& out_X, float& out_Y, float& out_Z) const;

		///
		const String& GetStringTarget() const
		{
			return m_stringTarget;
		}

		/// Determines whether a unit target guid is provided.
		const bool HasUnitTarget() const
		{
			return m_unitTarget != 0;
		}

		/// Determines whether a game object target guid is provided.
		const bool HasGOTarget() const
		{
			return m_goTarget != 0;
		}

		/// Determines whether an item target guid is provided.
		const bool HasItemTarget() const
		{
			return m_itemTarget != 0;
		}

		/// Determines whether a corpse target guid is provided.
		const bool HasCorpseTarget() const
		{
			return m_corpseTarget != 0;
		}

		/// Determines whether a source location is provided.
		const bool HasSourceTarget() const
		{
			return (m_targetMap & spell_cast_target_flags::SourceLocation) != 0;
		}

		/// Determines whether a dest location is provided.
		const bool HasDestTarget() const
		{
			return (m_targetMap & spell_cast_target_flags::DestLocation) != 0;
		}

		/// Determines whether a string target is provided.
		const bool HasStringTarget() const
		{
			return (m_targetMap & spell_cast_target_flags::String) != 0;
		}

		virtual SpellTargetMap& operator =(const SpellTargetMap& other);

	public:
		void SetTargetMap(const uint32 targetMap)
		{
			m_targetMap = targetMap;
		}

		void SetUnitTarget(const uint64 unitTarget)
		{
			m_unitTarget = unitTarget;
		}

		void SetObjectTarget(const uint64 unitTarget)
		{
			m_goTarget = unitTarget;
		}

	private:
		uint32 m_targetMap;
		uint64 m_unitTarget;
		uint64 m_goTarget;
		uint64 m_itemTarget;
		uint64 m_corpseTarget;
		float m_srcX, m_srcY, m_srcZ;
		float m_dstX, m_dstY, m_dstZ;
		String m_stringTarget;
	};

	io::Writer& operator << (io::Writer& w, SpellTargetMap const& targetMap);
	io::Reader& operator >> (io::Reader& r, SpellTargetMap& targetMap);
}