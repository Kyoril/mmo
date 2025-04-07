// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file typedefs.h
 *
 * @brief Defines common type definitions used throughout the codebase.
 *
 * This file contains type aliases for standard types to ensure consistent
 * naming and usage across the codebase. It includes fixed-size integer types,
 * string types, and game-specific type definitions.
 */

#pragma once

#include <cstdint>
#include <string>

namespace mmo
{
	/**
	 * @name Fixed-size unsigned integer types
	 * @{
	 */
	typedef std::uint8_t uint8;    ///< 8-bit unsigned integer
	typedef std::uint16_t uint16;  ///< 16-bit unsigned integer
	typedef std::uint32_t uint32;  ///< 32-bit unsigned integer
	typedef std::uint64_t uint64;  ///< 64-bit unsigned integer
	/** @} */

	/**
	 * @name Fixed-size signed integer types
	 * @{
	 */
	typedef std::int8_t int8;      ///< 8-bit signed integer
	typedef std::int16_t int16;    ///< 16-bit signed integer
	typedef std::int32_t int32;    ///< 32-bit signed integer
	typedef std::int64_t int64;    ///< 64-bit signed integer
	/** @} */

	/**
	 * @name String types
	 * @{
	 */
	typedef std::string String;    ///< Standard string type
	/** @} */

	/**
	 * @name Game-specific types
	 * @{
	 */
	typedef uint32 Identifier;     ///< General purpose identifier
	typedef uint64 ObjectGuid;     ///< Globally unique identifier for game objects
	typedef uint64 DatabaseId;     ///< Database record identifier
	typedef uint64 GameTime;       ///< Time representation in milliseconds
	typedef uint8 PacketBegin;     ///< Network packet beginning marker
	typedef uint8 PacketId;        ///< Network packet identifier
	typedef uint32 PacketSize;     ///< Network packet size
	/** @} */
}

/**
 * @name Global fixed-size integer types
 * @brief Global aliases for the mmo namespace integer types.
 * 
 * These global typedefs provide convenient access to the fixed-size integer
 * types defined in the mmo namespace without requiring the namespace prefix.
 * They are provided for backward compatibility and ease of use.
 * @{
 */
typedef mmo::uint8 uint8;      ///< Global alias for mmo::uint8
typedef mmo::uint16 uint16;    ///< Global alias for mmo::uint16
typedef mmo::uint32 uint32;    ///< Global alias for mmo::uint32
typedef mmo::uint64 uint64;    ///< Global alias for mmo::uint64
typedef mmo::int8 int8;        ///< Global alias for mmo::int8
typedef mmo::int16 int16;      ///< Global alias for mmo::int16
typedef mmo::int32 int32;      ///< Global alias for mmo::int32
typedef mmo::int64 int64;      ///< Global alias for mmo::int64
/** @} */
