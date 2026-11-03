// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "player.h"

#include "base/macros.h"
#include "base/non_copyable.h"
#include "game/chat_type.h"
#include "game/subsystem.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace mmo
{
	class World;

	/// A self-contained gameplay feature of the realm server (friends, bug reports, ...).
	///
	/// A module owns its client packet handlers and reacts to the player's session through the hooks
	/// below, so the feature lives in one place instead of being spread across Player. Modules exist
	/// once per realm process; per-character state belongs in the module, keyed by character guid,
	/// never in Player.
	///
	/// A module that names a subsystem can be switched off at runtime (GM `subsystem` command, realm
	/// REST `/subsystem`). Its packet handlers then answer "disabled" instead of acting, but the
	/// data hooks (OnCharacterLoading, OnJoiningWorld, OnCharacterDeleting, OnLevelChanged) keep
	/// running so switching a feature off never leaves its data inconsistent.
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

		/// The player's character is about to enter the world (login): the module loads the
		/// per-character state it hands to the world node. The character's world join is requested
		/// once every module called @p loaded, which each must do exactly once, also when loading
		/// failed. Runs even while the module is unavailable.
		/// @param characterGuid The character entering the world (its data is loaded and checked).
		/// @param loaded Continues the login.
		virtual void OnCharacterLoading(Player& player, uint64 characterGuid, std::function<void()> loaded) { loaded(); }

		/// The player's character was just asked to join a world node (login or map transfer).
		/// Packets sent to @p world now arrive there right after the join request, before anything
		/// of the character is saved. Runs even while the module is unavailable.
		virtual void OnJoiningWorld(Player& player, World& world) {}

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

		/// The player's character sent a chat message. Returns true if the module owns the chat
		/// type (guild chat, for example) and handled it, even if it refused the message; Player
		/// then does nothing more with it.
		virtual bool OnChatMessage(Player& player, ChatType type, const String& message) { return false; }
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

		/// Lets every module load its per-character state, then calls @p loaded once.
		void OnCharacterLoading(Player& player, const uint64 characterGuid, std::function<void()> loaded) const
		{
			if (m_modules.empty())
			{
				loaded();
				return;
			}

			// Modules may finish in any order (and asynchronously); the last one continues.
			auto remaining = std::make_shared<size_t>(m_modules.size());
			auto continuation = std::make_shared<std::function<void()>>(std::move(loaded));
			for (const auto& module : m_modules)
			{
				module->OnCharacterLoading(player, characterGuid, [remaining, continuation]()
				{
					ASSERT(*remaining > 0);
					if (--*remaining == 0)
					{
						(*continuation)();
					}
				});
			}
		}

		void OnJoiningWorld(Player& player, World& world) const
		{
			for (const auto& module : m_modules)
			{
				module->OnJoiningWorld(player, world);
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

		/// Offers a chat message to each module in turn. Returns true once one handled it.
		bool OnChatMessage(Player& player, const ChatType type, const String& message) const
		{
			for (const auto& module : m_modules)
			{
				if (module->OnChatMessage(player, type, message))
				{
					return true;
				}
			}

			return false;
		}

	private:
		std::vector<std::unique_ptr<RealmModule>> m_modules;
	};
}
