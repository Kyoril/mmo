#pragma once

#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct CreatureInfo
	{
		uint64 id;
		String name;
		String subname;
		/// Sound file played when this creature detects a stealthed player. Empty = default.
		String stealthAlertSound;

		/// @brief Builds the subtitle line shown for this creature in the user interface: the
		///	       creature's subname wrapped in angle brackets, as in "<General Goods>".
		/// @return The bracketed subname, or an empty string when the creature has no subname.
		[[nodiscard]] String GetSubtitle() const;
	};

	io::Writer& operator<<(io::Writer& writer, const CreatureInfo& info);
	io::Reader& operator>>(io::Reader& reader, CreatureInfo& outInfo);
}
