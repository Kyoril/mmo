// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "occlusion_query_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	OcclusionQueryD3D11::OcclusionQueryD3D11(ID3D11Device& device, ID3D11DeviceContext& context)
		: m_context(&context)
	{
		D3D11_QUERY_DESC desc = {};
		desc.Query = D3D11_QUERY_OCCLUSION;
		desc.MiscFlags = 0;

		const HRESULT hr = device.CreateQuery(&desc, m_query.GetAddressOf());
		ASSERT(SUCCEEDED(hr));
	}

	void OcclusionQueryD3D11::Begin()
	{
		ASSERT(m_query);
		m_context->Begin(m_query.Get());
	}

	void OcclusionQueryD3D11::End()
	{
		ASSERT(m_query);
		m_context->End(m_query.Get());
		m_hasPendingResult = true;
	}

	bool OcclusionQueryD3D11::TryGetResult(uint64& pixelCount)
	{
		if (!m_hasPendingResult)
		{
			return false;
		}

		UINT64 result = 0;
		const HRESULT hr = m_context->GetData(m_query.Get(), &result, sizeof(result), D3D11_ASYNC_GETDATA_DONOTFLUSH);

		if (hr == S_OK)
		{
			pixelCount = static_cast<uint64>(result);
			m_hasPendingResult = false;
			return true;
		}

		// S_FALSE means the query is still pending
		return false;
	}

	bool OcclusionQueryD3D11::HasPendingResult() const
	{
		return m_hasPendingResult;
	}
}
