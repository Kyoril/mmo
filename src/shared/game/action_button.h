#pragma once

#include "base/typedefs.h"

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
			Item = 2
		};
	}

	typedef action_button_type::Enum ActionButtonType;

	constexpr uint32 MaxActionButtons = 12;

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
		ActionButton()
			: action(0)
			, type(action_button_type::None)
			, state(action_button_update_state::New)
		{
		}

		/// Custom constructor.
		ActionButton(uint16 action_, ActionButtonType type_)
			: action(action_)
			, type(type_)
			, state(action_button_update_state::New)
		{
		}
	};

	io::Reader& operator>>(io::Reader& reader, ActionButton& data);
	io::Writer& operator<<(io::Writer& writer, const ActionButton& data);

	/// Maps action buttons by their slots.
	typedef std::array<ActionButton, MaxActionButtons> ActionButtons;
}