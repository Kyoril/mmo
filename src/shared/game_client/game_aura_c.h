#pragma once

#include "base/signal.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace mmo
{
	class GameUnitC;

	namespace proto_client
	{
		class SpellEntry;
	}

	/// @brief Represents an aura applied to a game unit on the client side.
	/// An aura is a temporary effect from a spell that persists for a certain duration.
	class GameAuraC final : public NonCopyable
	{
	public:
		/// @brief Signal emitted when the aura is removed.
		signal<void()> removed;

	public:
		/// @brief Creates a new GameAuraC instance.
		/// @param owner The unit that owns this aura.
		/// @param spell The spell that created this aura.
		/// @param caster The GUID of the unit that cast the spell.
		/// @param expiration The expiration time in milliseconds (0 for permanent auras).
		explicit GameAuraC(GameUnitC& owner, const proto_client::SpellEntry& spell, uint64 caster, GameTime expiration);

		/// @brief Destructor that ensures the aura is properly cleaned up.
		~GameAuraC() override;

	public:
		/// @brief Checks if this aura can expire.
		/// @return True if the aura has an expiration time, false if it's permanent.
		bool CanExpire() const 
		{ 
			return m_expiration > 0; 
		}

		/// @brief Gets the expiration time of this aura.
		/// @return The expiration time in milliseconds since epoch.
		GameTime GetExpiration() const 
		{ 
			return m_expiration; 
		}

		/// @brief Checks if this aura has expired.
		/// @return True if the aura has expired and should be removed.
		bool IsExpired() const;

		/// @brief Gets the spell that created this aura.
		/// @return Pointer to the spell entry, never null.
		const proto_client::SpellEntry* GetSpell() const 
		{ 
			return m_spell; 
		}

		/// @brief Gets the GUID of the unit that cast the spell creating this aura.
		/// @return The caster's GUID.
		uint64 GetCasterId() const 
		{ 
			return m_casterId; 
		}

		/// @brief Gets the GUID of the unit that this aura is applied to.
		/// @return The target's GUID.
		uint64 GetTargetId() const 
		{ 
			return m_targetId; 
		}

	private:
		/// @brief Pointer to the spell that created this aura.
		const proto_client::SpellEntry* m_spell;

		/// @brief Expiration time in milliseconds since epoch (0 for permanent auras).
		GameTime m_expiration;

		/// @brief GUID of the unit that cast the spell.
		uint64 m_casterId;

		/// @brief GUID of the unit this aura is applied to.
		uint64 m_targetId;

		/// @brief Connection to the owner's removed signal.
		scoped_connection m_onOwnerRemoved;
	};
}
