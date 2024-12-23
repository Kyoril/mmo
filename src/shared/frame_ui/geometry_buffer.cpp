// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "geometry_buffer.h"

#include "base/macros.h"


namespace mmo
{
	GeometryBuffer::GeometryBuffer()
		: m_sync(false)
	{
		m_hwBuffer = GraphicsDevice::Get().CreateVertexBuffer(64, sizeof(Vertex), BufferUsage::DynamicWriteOnlyDiscardable, nullptr);
	}

	void GeometryBuffer::Draw()
	{
		// Obtain the current graphics device object
		auto& gx = GraphicsDevice::Get();

		// Set up clipping for this buffer
		// TODO

		// Eventually update the hardware vertex buffer
		if (!m_sync)
		{
			SyncHardwareBuffer();
		}

		// Current vertex format
		constexpr auto format = VertexFormat::PosColorTex1;
		gx.SetBlendMode(BlendMode::Alpha);
		gx.SetVertexFormat(format);

		// draw the batches
		uint32 pos = 0;
		for (const auto& batch : m_batches)
		{
			gx.BindTexture(batch.first, ShaderType::PixelShader, 0);

			// Setup geometry
			m_hwBuffer->Set(0);

			// Draw vertex buffer data
			gx.Draw(batch.second, pos);

			// Increase offset
			pos += batch.second;
		}
	}

	void GeometryBuffer::SetClippingRegion(const Rect & region)
	{
		// TODO
	}

	void GeometryBuffer::AppendVertex(const Vertex & vertex)
	{
		AppendGeometry(&vertex, 1);
	}

	void GeometryBuffer::AppendGeometry(const Vertex * const buffer, const uint32 count)
	{
		ASSERT(m_activeTexture);

		// See if we need to start a new render batch
		if (m_batches.empty() || m_batches.back().first != m_activeTexture)
			m_batches.emplace_back(m_activeTexture, 0);

		// Update size of current batch
		m_batches.back().second += count;

		// Buffer these vertices
		m_vertices.reserve(m_vertices.size() + count);
		for (uint32 i = 0; i < count; ++i)
		{
			const Vertex& vs = buffer[i];

			// TODO: If needed, modify vertex data in here

			m_vertices.push_back(vs);
		}

		// Buffer is out of sync now
		m_sync = false;
	}

	void GeometryBuffer::SetActiveTexture(const TexturePtr& texture)
	{
		m_activeTexture = texture;
	}

	void GeometryBuffer::Reset()
	{
		m_vertices.clear();
		m_batches.clear();
		m_activeTexture = nullptr;
		m_sync = false;
	}

	TexturePtr GeometryBuffer::GetActiveTexture() const
	{
		return m_activeTexture;
	}

	uint32 GeometryBuffer::GetVertexCount() const
	{
		return static_cast<uint32>(m_vertices.size());
	}

	uint32 GeometryBuffer::GetBatchCount() const
	{
		return static_cast<uint32>(m_batches.size());
	}

	void GeometryBuffer::SyncHardwareBuffer()
	{
		ASSERT(!m_sync);

		// First, we request the current hw buffer capacity
		size_t size = m_hwBuffer->GetVertexCount();

		// Then we check if we need to resize the buffer
		const size_t required = m_vertices.size();
		if (size < required)
		{
			// Resize is required. We will double the existing size as long as it fits, so that
			// we won't have to do hw buffer reallocation too often as it is a somewhat heavy
			// operation
			while (size < required)
			{
				size *= 2;
			}

			// Reallocate the buffer
			m_hwBuffer = GraphicsDevice::Get().CreateVertexBuffer(size, sizeof(Vertex), BufferUsage::DynamicWriteOnly, nullptr);
		}

		// Copy vertex data into hw buffer
		if (required > 0)
		{
			std::memcpy(m_hwBuffer->Map(LockOptions::Discard), m_vertices.data(), sizeof(Vertex) * m_vertices.size());
			m_hwBuffer->Unmap();
		}

		// The buffers are now synchronized
		m_sync = true;
	}
}
