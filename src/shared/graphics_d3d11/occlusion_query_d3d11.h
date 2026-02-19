// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/occlusion_query.h"

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace mmo
{
	/// @brief D3D11 implementation of GPU occlusion queries using ID3D11Query.
	///
	/// Uses D3D11_QUERY_OCCLUSION to count how many pixels passed the depth test
	/// for geometry rendered between Begin() and End(). Results are retrieved
	/// asynchronously via TryGetResult() to avoid GPU stalls.
	class OcclusionQueryD3D11 final : public OcclusionQuery
	{
	public:
		/// @brief Constructs a D3D11 occlusion query.
		/// @param device The D3D11 device used to create the query object.
		/// @param context The D3D11 device context used to issue query commands.
		explicit OcclusionQueryD3D11(ID3D11Device& device, ID3D11DeviceContext& context);

		/// @brief Destructor.
		~OcclusionQueryD3D11() override = default;

	public:
		/// @brief Begins the occlusion query on the GPU.
		void Begin() override;

		/// @brief Ends the occlusion query on the GPU.
		void End() override;

		/// @brief Attempts to retrieve the query result without blocking the CPU.
		/// @param pixelCount Receives the number of pixels that passed the depth test.
		/// @return true if the result is available, false if still pending on the GPU.
		bool TryGetResult(uint64& pixelCount) override;

		/// @brief Returns true if a query has been issued and may have pending results.
		bool HasPendingResult() const override;

	private:
		ComPtr<ID3D11Query> m_query;
		ID3D11DeviceContext* m_context;
		bool m_hasPendingResult = false;
	};
}
