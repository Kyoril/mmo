// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstdint>
#include <string>

namespace mmo
{
	// Unsigned typedefs
	typedef std::uint8_t uint8;
	typedef std::uint16_t uint16;
	typedef std::uint32_t uint32;
	typedef std::uint64_t uint64;

	// Signed typedefs
	typedef std::int8_t int8;
	typedef std::int16_t int16;
	typedef std::int32_t int32;
	typedef std::int64_t int64;

	// String
	typedef std::string String;

	// Game
	typedef uint32 Identifier;
	typedef uint64 ObjectGuid;
	typedef uint64 DatabaseId;
	typedef uint64 GameTime;
	typedef uint8 PacketBegin;
	typedef uint8 PacketId;
	typedef uint32 PacketSize;
}

// Unsigned typedefs
typedef mmo::uint8 uint8;
typedef mmo::uint16 uint16;
typedef mmo::uint32 uint32;
typedef mmo::uint64 uint64;
typedef mmo::int8 int8;
typedef mmo::int16 int16;
typedef mmo::int32 int32;
typedef mmo::int64 int64;
