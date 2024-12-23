// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"

#include <vector>


namespace mmo
{
	// Forwards
	class Rect;


	/// This class allows to add vertices that are batched together by the current texture.
	/// This means, that whenever you assign a new texture, new added vertices will form a
	/// new render batch.
	/// This class then manages a hardware vertex buffer and will update it's values when 
	/// needed to reflect the changes.
	class GeometryBuffer final : public NonCopyable
	{
	public:
		/// Shortcut for better readability
		typedef POS_COL_TEX_VERTEX Vertex;
		
	public:
		/// Default constructor initializes an empty default vertex buffer.
		GeometryBuffer();

	public:

		/// Draws the current hardware buffer. If the buffer is out of sync, it
		/// will also be updated.
		void Draw();

		/// Sets the current clipping rectangle for drawing the geometry buffer.
		void SetClippingRegion(const Rect& region);

		/// Appends a new vertex to the buffer.
		void AppendVertex(const Vertex& vertex);

		/// Appends multiple vertices at once to the buffer.
		void AppendGeometry(const Vertex* const buffer, uint32 count);

		/// Sets the active texture. Not that this will not immediatly start a new
		/// batch. This only happens when adding a new vertex after changing the
		/// active texture.
		void SetActiveTexture(const TexturePtr& texture);

		/// Resets all contents of the geometry buffer to free memory. Note that this
		/// will not destroy the existing hardware buffer!
		void Reset();

		/// Gets the active texture.
		TexturePtr GetActiveTexture() const;

		/// Gets the number of vertices in the buffer.
		uint32 GetVertexCount() const;

		/// Gets the number of batches in the buffer.
		uint32 GetBatchCount() const;

	private:
		/// Copies the contents of the vertices over to the hardware buffer, eventally
		/// reallocating it if there isn't enough space for the required vertices.
		void SyncHardwareBuffer();

	private:
		/// whether the hw buffer is in sync with the added geometry
		bool m_sync;

		/// type to track info for per-texture sub batches of geometry
		typedef std::pair<TexturePtr, uint32> BatchInfo;
		/// type of container that tracks BatchInfos.
		typedef std::vector<BatchInfo> BatchList;
		/// list of texture batches added to the geometry buffer
		BatchList m_batches;

		/// type of container used to queue the geometry
		typedef std::vector<Vertex> VertexList;
		/// container where added geometry is stored.
		VertexList m_vertices;

		/// The active texture for the current batch.
		TexturePtr m_activeTexture;

		/// The hardware (gpu) vertex buffer
		VertexBufferPtr m_hwBuffer;
	};
}
