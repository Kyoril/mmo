
#include "action_button.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Reader& operator>>(io::Reader& reader, ActionButton& data)
	{
		return reader
			>> io::read<uint8>(data.type)
			>> io::read<uint16>(data.action)
			>> io::read<uint8>(data.state);
	}

	io::Writer& operator<<(io::Writer& writer, const ActionButton& data)
	{
		return writer
			<< io::write<uint8>(data.type)
			<< io::write<uint16>(data.action)
			<< io::write<uint8>(data.state);
	}
}
