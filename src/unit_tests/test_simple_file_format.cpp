// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "simple_file_format/sff_read_parser.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_write_writer.h"

#include <cmath>
#include <cfloat>
#include <sstream>


namespace
{
	void CheckEscaping(const std::string &encoded, const std::string &decoded)
	{
		using namespace sff;

		{
			typedef std::string::const_iterator Iterator;

			read::Token<Iterator> token(read::token_type::String, encoded.begin(), encoded.end());
			const std::string result = read::Parser<Iterator>::decodeStringLiteral(token);
			CHECK(result == decoded);
		}

		{
			const auto result = write::escapeString(decoded, sff::write::QuotedStringReplacements);
			CHECK(result == encoded);
		}
	}
	
	void CheckFloatParsing(const std::string &source, double expected)
	{
		REQUIRE(!source.empty());
		const bool isNegative = (source.front() == '-');

		typedef std::string::const_iterator Iterator;

		const sff::read::Token<Iterator> token(
			sff::read::token_type::Decimal,
			source.begin() + isNegative,
			source.end());
		
		const sff::read::tree::Integer<Iterator> integer(isNegative, token);
		const double parsed = integer.getValue<double>();

		CHECK(::fabs(expected - parsed) <= FLT_EPSILON);
	}

	std::string FormatFloat(double original)
	{
		std::ostringstream buffer;
		sff::write::Writer<char> writer(buffer);
		writer.writeValue(original);
		return buffer.str();
	}
}



TEST_CASE("SFFEscaping", "[simple_file_format]")
{
	CheckEscaping("", "");
	CheckEscaping("abc", "abc");
	CheckEscaping("123", "123");
	CheckEscaping("\\t", "\t");
	CheckEscaping("\\n", "\n");
	CheckEscaping("\\r", "\r");
	CheckEscaping("\\\\", "\\");
	CheckEscaping("\\\"", "\"");
	CheckEscaping("\\\'", "\'");
	CheckEscaping("abc\\t", "abc\t");
	CheckEscaping("abc\\n", "abc\n");
	CheckEscaping("abc\\r", "abc\r");
	CheckEscaping("abc\\\\", "abc\\");
	CheckEscaping("abc\\\"", "abc\"");
	CheckEscaping("abc\\\'", "abc\'");
	CheckEscaping("abc\\t1", "abc\t1");
	CheckEscaping("abc\\n12", "abc\n12");
	CheckEscaping("abc\\r123", "abc\r123");
	CheckEscaping("abc\\\\1", "abc\\1");
	CheckEscaping("abc\\\"12", "abc\"12");
	CheckEscaping("abc\\\'123", "abc\'123");
}

TEST_CASE("SFFFloatTest", "[simple_file_format]")
{
	CheckFloatParsing("0", 0.0);
	CheckFloatParsing("-0", -0.0);
	CheckFloatParsing("1", 1.0);
	CheckFloatParsing("-1", -1.0);
	CheckFloatParsing("123456", 123456.0);
	CheckFloatParsing("-123456", -123456.0);
	CheckFloatParsing("123456.0", 123456.0);
	CheckFloatParsing("-123456.0", -123456.0);
	CheckFloatParsing("0.123456", 0.123456);
	CheckFloatParsing("-0.123456", -0.123456);
}

TEST_CASE("SFFFloatFormatting", "[simple_file_format]")
{
	CHECK("-431602000" == FormatFloat(-4.31602e+008));
	CHECK("0" == FormatFloat(0));
	CHECK("0.5" == FormatFloat(0.5));
	CHECK("0.00005" == FormatFloat(0.00005));
}
