#pragma once

#include "base/typedefs.h"

#include <array>
#include <map>

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	/// Enumerates possible states of an action bar button.
	namespace action_button_update_state
	{
		enum Enum
		{
			///
			Unchanged = 0,

			///
			Changed = 1,

			///
			New = 2,

			///
			Deleted = 3
		};
	}

	typedef action_button_update_state::Enum ActionButtonUpdateState;

	namespace action_button_type
	{
		enum Enum
		{
			None = 0,

			///
			Spell = 1,

			///
			Item = 2,

			/// Performs an animated emote (the action is the emote entry id).
			Emote = 3
		};
	}

	typedef action_button_type::Enum ActionButtonType;

	/// Number of action buttons a single action bar holds.
	constexpr uint32 ActionButtonsPerBar = 12;

	/// Number of action bars a character has: the main bar plus the second bar above it, which the
	/// player can show through the interface options.
	constexpr uint32 ActionBarCount = 2;

	/// Total number of action button slots a character owns across all action bars.
	constexpr uint32 MaxActionButtons = ActionButtonsPerBar * ActionBarCount;

	/// Defines data of an action button.
	struct ActionButton final
	{
		/// This is the buttons entry (spell or item entry), or 0 if no action.
		uint16 action = 0;

		/// This is the button type.
		ActionButtonType type = action_button_type::None;

		/// The button state (unused right now).
		ActionButtonUpdateState state = action_button_update_state::Unchanged;

		/// Default constructor.
		ActionButton() = default;

		/// Custom constructor.
		ActionButton(const uint16 action, const ActionButtonType type)
			: action(action)
			, type(type)
		{
		}
	};

	io::Reader& operator>>(io::Reader& reader, ActionButton& data);
	io::Writer& operator<<(io::Writer& writer, const ActionButton& data);

	/// Maps action buttons by their slots.
	typedef std::array<ActionButton, MaxActionButtons> ActionButtons;
}