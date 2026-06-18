// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/structured_buffer.h"

namespace mmo
{
	/// @brief D3D11 implementation of a structured buffer.
	/// 
	/// This class wraps a D3D11 buffer with a shader resource view for use as
	/// a StructuredBuffer in HLSL shaders. It supports dynamic updates for
	/// efficient per-frame data changes.
	class StructuredBufferD3D11 final : public StructuredBuffer
	{
	public:
		/// @brief Constructs a D3D11 structured buffer.
		/// @param device The D3D11 device.
		/// @param context The D3D11 device context.
		/// @param elementSize The size of a single element in bytes.
		/// @param elementCount The maximum number of elements the buffer can hold.
		/// @param initialData Optional initial data to populate the buffer with.
		explicit StructuredBufferD3D11(
			ID3D11Device& device, 
			ID3D11DeviceContext& context, 
			size_t elementSize, 
			size_t elementCount, 
			const void* initialData);

		/// @brief Destructor.
		~StructuredBufferD3D11() override = default;

	public:
		/// @copydoc StructuredBuffer::BindToStage
		void BindToStage(ShaderType type, uint32 slot) override;

		/// @copydoc StructuredBuffer::Update
		void Update(const void* data, size_t elementCount) override;

		/// @brief Gets the underlying D3D11 buffer.
		/// @return Pointer to the D3D11 buffer.
		[[nodiscard]] ID3D11Buffer* GetBuffer() const
		{
			return m_buffer.Get();
		}

		/// @brief Gets the shader resource view for this buffer.
		/// @return Pointer to the shader resource view.
		[[nodiscard]] ID3D11ShaderResourceView* GetSRV() const
		{
			return m_srv.Get();
		}

	protected:
		/// @brief Reference to the D3D11 device.
		ID3D11Device& m_device;

		/// @brief Reference to the D3D11 device context.
		ID3D11DeviceContext& m_context;

		/// @brief The underlying D3D11 buffer.
		ComPtr<ID3D11Buffer> m_buffer;

		/// @brief Shader resource view for reading the buffer in shaders.
		ComPtr<ID3D11ShaderResourceView> m_srv;
	};
}
