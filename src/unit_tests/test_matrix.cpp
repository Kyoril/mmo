// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#if 0

#include "catch.hpp"

#include "math/vector3.h"
#include "math/matrix4.h"
#include "math/angle.h"
#include "math/radian.h"
#include "math/degree.h"

#ifdef _WIN32
#include <DirectXMath.h>
using namespace DirectX;
#endif

#include "scene_graph/scene.h"

using namespace mmo;

static void dumpVec(const char* name, const Vector4& v)
{
    printf("%s = (%.4f, %.4f, %.4f, %.4f)\n",
        name, v.x, v.y, v.z, v.w);
}

#ifdef _WIN32
static void dumpXVec(const char* name, FXMVECTOR v)
{
    XMFLOAT4 f; XMStoreFloat4(&f, v);
    printf("%s = (%.4f, %.4f, %.4f, %.4f)\n", name, f.x, f.y, f.z, f.w);
}
#endif

static Matrix4 MakeOrthographicMatrix(const float left, const float top, const float right, const float bottom, const float nearPlane, const float farPlane)
{
    const float invW = 1.0f / (right - left);
    const float invH = 1.0f / (top - bottom);
    const float invD = 1.0f / (farPlane - nearPlane);

    Matrix4 result = Matrix4::Zero;
    result[0][0] = 2.0f * invW;
    result[0][3] = -(right + left) * invW;
    result[1][1] = 2.0f * invH;
    result[1][3] = -(top + bottom) * invH;
    result[2][2] = invD;
    result[2][3] = -nearPlane * invD;
    result[3][3] = 1.0f;

    return result;
}

#ifdef _WIN32
TEST_CASE("XMATHWorks", "[matrix]")
{
    //------------------------------------------------------------------
    // 1. Choose a vertex in world space – something obviously inside the
    //    volume we want the orthographic camera to see.
    //------------------------------------------------------------------
    XMVECTOR vWorld = XMVectorSet(5.0f,   // x
        2.0f,   // y
        0.5f,   // z (in front of the camera)
        1.0f); // w

    //------------------------------------------------------------------
    // 2. Define the light (directional) camera.
    //    We place the camera  20 units back along +Z looking towards the
    //    origin with +Y as up.  This mimics a sunlight coming from –Z.
    //------------------------------------------------------------------
    XMVECTOR eye = XMVectorSet(0.0f, 0.0f, 20.0f, 1.0f);
    XMVECTOR target = XMVectorZero();              // look at world origin
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    //------------------------------------------------------------------
    // 3. Orthographic projection that encloses a 20×20×(near..far) box.
    //------------------------------------------------------------------
    const float orthoWidth = 20.0f;   // covers x ∈ [‑10 … +10]
    const float orthoHeight = 20.0f;   // covers y ∈ [‑10 … +10]

    const float nearZ = 0.0f;          // start at camera plane
    const float farZ = 50.0f;         // 50 units deep into the scene

    XMMATRIX proj = XMMatrixOrthographicLH(orthoWidth, orthoHeight, nearZ, farZ);

    //------------------------------------------------------------------
    // 4. Combine to View‑Projection *column major* (GPU expects this).
    //------------------------------------------------------------------
    XMMATRIX vp = XMMatrixMultiply(view, proj);    // equivalent to view * proj

    //------------------------------------------------------------------
    // 5. Transform the vertex.  The result is clip‑space (x,y,z,w).
    //------------------------------------------------------------------
    XMVECTOR vClip = XMVector4Transform(vWorld, vp);

    //------------------------------------------------------------------
    // 6. Homogeneous divide to get NDC (x/w, y/w, z/w).
    //------------------------------------------------------------------
    XMVECTOR vNdc = vClip / XMVectorSplatW(vClip);

    dumpXVec("World vertex", vWorld);
    dumpXVec("Clip (before /w)", vClip);
    dumpXVec("NDC (after /w)", vNdc);

    //------------------------------------------------------------------
    // 7. Expectations
    //------------------------------------------------------------------
    //   • orthoWidth and orthoHeight are ±10, so our x=+5  → NDC.x = +0.5
    //   • y=+2                          → NDC.y = +0.2
    //   • z = 0.5 is 0.5‑near / (far‑near) = 0.5 / 50 → 0.01  ≈ 0.01
    //   All three should lie inside [‑1 … +1].

    XMFLOAT4 n; XMStoreFloat4(&n, vNdc);
    CHECK(n.x > -1.0f);
	CHECK(n.x < 1.0f);
    CHECK(n.y > -1.0f);
	CHECK(n.y < 1.0f);
    CHECK(n.z >= 0.0f);
	CHECK(n.z <= 1.0f);
}
#endif

TEST_CASE("ProjectionWorks", "[matrix]")
{
	//--------------------------------------------------------------------
    // 1. A known world‑space vertex (inside our planned ortho box)
    //--------------------------------------------------------------------
    Vector4 vWorld(5.0f, 2.0f, 0.5f, 1.0f);

    //--------------------------------------------------------------------
    // 2. Build the light's View matrix
    //--------------------------------------------------------------------
    Vector3 eye(0.0f, 0.0f, 20.0f);
    Vector3 target(0.0f, 0.0f, 0.0f);
    Vector3 up(0.0f, 1.0f, 0.0f);

    // Your MakeViewMatrix probably takes (pos, orientation).  Build a
    // quaternion that looks toward -Z with +Y up:
    Vector3 dir = (target - eye).NormalizedCopy();
    Vector3 left = up.Cross(dir).NormalizedCopy();
    up = dir.Cross(left).NormalizedCopy();
    Quaternion q; q.FromAxes(left, up, dir);

    Matrix4 view = MakeViewMatrix(eye, q);      // LH, looks towards -Z

    //--------------------------------------------------------------------
    // 3. Orthographic projection  (width = height = 20, near = 0, far = 50)
    //--------------------------------------------------------------------
    float orthoHalf = 10.0f;                  // ±10 in X and Y
    float nearZ = 0.0f;
    float farZ = 50.0f;

    Matrix4 proj = MakeOrthographicMatrix(
        -orthoHalf,        // left
        orthoHalf,        // top
        orthoHalf,        // right
        -orthoHalf,        // bottom
        nearZ, farZ);

    //--------------------------------------------------------------------
    // 4. View‑Projection  (your math is row‑major – multiply that way)
    //--------------------------------------------------------------------
    Matrix4 vp = proj * view;                   // world→clip

    //--------------------------------------------------------------------
    // 5. Transform the vertex and divide by w
    //--------------------------------------------------------------------
    Vector4 vClip = vp * vWorld;                // clip‑space
    Vector4 vNdc = vClip / vClip.w;            // homogeneous divide

    dumpVec("World vertex       ", vWorld);
    dumpVec("Clip (before /w)   ", vClip);
    dumpVec("NDC  (after  /w)   ", vNdc);

    //--------------------------------------------------------------------
    // 6. Assertions – should match DirectXMath run
    //--------------------------------------------------------------------
    CHECK(vNdc.x > -1.0f);
    CHECK(vNdc.x < 1.0f);    // inside frustum in X
    CHECK(vNdc.y > -1.0f);
	CHECK(vNdc.y < 1.0f);    // inside frustum in Y
    CHECK(vNdc.z >= 0.0f);
    CHECK(vNdc.z <= 1.0f);   // 0..1 depth
}

TEST_CASE("CameraViewMatrixCorrect", "[matrix]")
{
    //--------------------------------------------------------------------
    // 1. A known world‑space vertex (inside our planned ortho box)
    //--------------------------------------------------------------------
    Vector4 vWorld(5.0f, 2.0f, 0.5f, 1.0f);

    //--------------------------------------------------------------------
    // 2. Build the light's View matrix
    //--------------------------------------------------------------------
    Vector3 eye(0.0f, 0.0f, 20.0f);
    Vector3 target(0.0f, 0.0f, 0.0f);
    Vector3 up(0.0f, 1.0f, 0.0f);

    // Your MakeViewMatrix probably takes (pos, orientation).  Build a
    // quaternion that looks toward -Z with +Y up:
    Vector3 dir = (target - eye).NormalizedCopy();
    Vector3 left = up.Cross(dir).NormalizedCopy();
    up = dir.Cross(left).NormalizedCopy();
    Quaternion q; q.FromAxes(left, up, dir);

    const Matrix4 view = MakeViewMatrix(eye, q);      // LH, looks towards -Z

    GraphicsDevice::CreateNull(GraphicsDeviceDesc());

    Scene scene;

	Camera* camera = scene.CreateCamera("TestCamera");
    REQUIRE(camera);

	SceneNode* cameraNode = scene.GetRootSceneNode().CreateChildSceneNode("TestCameraNode");
	REQUIRE(cameraNode);

	cameraNode->AttachObject(*camera);
	cameraNode->SetPosition(Vector3(0.0f, 0.0f, 20.0f));
	cameraNode->LookAt(Vector3::Zero, TransformSpace::World, Vector3::UnitZ);
    const Matrix4 cameraView = camera->GetViewMatrix();
	CHECK(cameraView.IsNearlyEqual(view));
}

#endif
