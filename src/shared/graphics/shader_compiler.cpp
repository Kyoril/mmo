
#include "shader_compiler.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const ShaderCode& code)
	{
		return writer
			<< io::write_dynamic_range<uint8>(code.format)
			<< io::write_dynamic_range<uint32>(code.data);
	}

	io::Reader& operator>>(io::Reader& reader, ShaderCode& code)
	{
		return reader
			>> io::read_container<uint8>(code.format)
			>> io::read_container<uint32>(code.data);
	}
}
