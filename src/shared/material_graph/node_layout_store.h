// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Packs a color into a 32 bit value using the byte order of ImGui's IM_COL32 (red in the low byte),
	///        rounding each channel exactly like ImGui's ImColor conversion does. The material graph is
	///        independent of ImGui, but the editor draws these values with it.
	/// @param r Red channel in [0..1].
	/// @param g Green channel in [0..1].
	/// @param b Blue channel in [0..1].
	/// @param a Alpha channel in [0..1].
	constexpr uint32 PackNodeColor(float r, float g, float b, float a)
	{
		const auto toByte = [](float value) constexpr -> uint32
		{
			const float saturated = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
			return static_cast<uint32>(saturated * 255.0f + 0.5f);
		};

		return (toByte(a) << 24) | (toByte(b) << 16) | (toByte(g) << 8) | toByte(r);
	}

	/// @brief Where the material graph reads and writes the position and size of its nodes. Node layout is
	///        owned by whatever displays the graph (the node editor), the graph itself only persists it.
	class INodeLayoutStore
	{
	public:
		virtual ~INodeLayoutStore() = default;

		/// @brief Gets the current bounds of a node.
		/// @param nodeId Id of the node.
		/// @param x Receives the x position.
		/// @param y Receives the y position.
		/// @param w Receives the width.
		/// @param h Receives the height.
		/// @return false if the node has no known bounds yet (it was never displayed).
		virtual bool GetNodeBounds(uint32 nodeId, float& x, float& y, float& w, float& h) const = 0;

		/// @brief Sets the bounds of a node, used to restore a saved layout.
		/// @param nodeId Id of the node.
		/// @param x The x position.
		/// @param y The y position.
		/// @param w The width.
		/// @param h The height.
		virtual void SetNodeBounds(uint32 nodeId, float x, float y, float w, float h) = 0;
	};
}
