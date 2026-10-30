// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "player.h"

#include "base/macros.h"
#include "base/non_copyable.h"
#include "game/subsystem.h"

#include <memory>
#include <optional>
#include <vector>

namespace mmo
{
	/// A self-contained gameplay feature of the realm server (friends, bug reports, ...).
	///
	/// A module owns its client packet handlers and reacts to the player's session through the hooks
	/// below, so the feature lives in one place instead of being spread across Player. Modules exist
	/// once per realm process; per-character state belongs in the module, keyed by character guid,
	/// never in Player.
	///
	/// A module that names a subsystem can be switched off at runtime (GM `subsystem` command, realm
	/// REST `/subsystem`). Its packet handlers then answer "disabled" instead of acting, but the
	/// data hooks (OnCharacterDeleting, OnLevelChanged) keep running so switching a feature off never
	/// leaves its data inconsistent.
	class RealmModule : public NonCopyable
	{
	public:
		virtual ~RealmModule() = default;

		/// Short name used in log output.
		[[nodiscard]] virtual const char* GetName() const = 0;

		/// The subsystem that switches this module on and off, or nullopt if it is always on.
		[[nodiscard]] virtual std::optional<game::Subsystem> GetSubsystem() const { return std::nullopt; }

		/// Whether the module is currently available to the given player.
		[[nodiscard]] bool IsAvailableTo(const Player& player) const
		{
			const auto subsystem = GetSubsystem();
			return !subsystem || player.IsSubsystemAvailable(*subsystem);
		}

	public:
		/// Registers the module's client packet handlers. Called whenever the player's in-world
		/// handlers are enabled; everything added to @p handlers is removed again when they are
		/// disabled (logout, world node lost).
		virtual void RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers) {}

		/// The player's character entered the world after logging in.
		virtual void OnEnteredWorld(Player& player) {}

		/// The player's character left the world (logout to character selection or disconnect).
		/// Character data is still available.
		virtual void OnLeftWorld(Player& player) {}

		/// The player's character changed its level. Runs even while the module is unavailable.
		virtual void OnLevelChanged(Player& player, uint32 previousLevel) {}

		/// A character of the player's account is about to be deleted. Runs even while the module
		/// is unavailable, so the module can clean up data that refers to the character.
		virtual void OnCharacterDeleting(Player& player, uint64 characterGuid) {}
	};

	/// The set of modules of a realm process. Forwards every hook to each module in the order the
	/// modules were added.
	class RealmModules final : public NonCopyable
	{
	public:
		/// Adds a module. Modules are added at startup, before any player connects.
		void Add(std::unique_ptr<RealmModule> module)
		{
			ASSERT(module);
			m_modules.push_back(std::move(module));
		}

		/// Gets the number of modules.
		[[nodiscard]] size_t GetCount() const { return m_modules.size(); }

		void RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers) const
		{
			for (const auto& module : m_modules)
			{
				module->RegisterPacketHandlers(player, handlers);
			}
		}

		void OnEnteredWorld(Player& player) const
		{
			for (const auto& module : m_modules)
			{
				module->OnEnteredWorld(player);
			}
		}

		void OnLeftWorld(Player& player) const
		{
			for (const auto& module : m_modules)
			{
				module->OnLeftWorld(player);
			}
		}

		void OnLevelChanged(Player& player, const uint32 previousLevel) const
		{
			for (const auto& module : m_modules)
			{
				module->OnLevelChanged(player, previousLevel);
			}
		}

		void OnCharacterDeleting(Player& player, const uint64 characterGuid) const
		{
			for (const auto& module : m_modules)
			{
				module->OnCharacterDeleting(player, characterGuid);
			}
		}

	private:
		std::vector<std::unique_ptr<RealmModule>> m_modules;
	};
}
