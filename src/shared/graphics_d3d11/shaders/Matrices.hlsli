// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Shared matrix constant buffers.
//
// The world matrix lives in its own small per-object buffer at b0 (re-uploaded per object), while
// the view/projection matrices and their inverses live in a per-view buffer at b12 that is only
// re-uploaded when the camera changes. Keep the register assignments in sync with
// GraphicsDeviceD3D11 (kPerViewMatrixBufferSlot) and the material compiler.

cbuffer ObjectMatrices : register(b0)
{
	column_major matrix matWorld;
};

cbuffer ViewMatrices : register(b12)
{
	column_major matrix matView;
	column_major matrix matProj;
	column_major matrix matInvView;
	column_major matrix matInvProj;
};
