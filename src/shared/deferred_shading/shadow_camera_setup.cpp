
#include "shadow_camera_setup.h"

#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"

namespace mmo
{
	void DefaultShadowCameraSetup::SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera)
	{
		Vector3 pos, dir;

		// reset custom view / projection matrix in case already set
		shadowCamera.SetCustomViewMatrix(false);
		shadowCamera.SetCustomProjMatrix(false);
		shadowCamera.SetNearClipDistance(0.01f);
		shadowCamera.SetFarClipDistance(light.GetShadowFarDistance());

		// get the shadow frustum's far distance
		float shadowDist = light.GetShadowFarDistance();
		if (!shadowDist)
		{
			// need a shadow distance, make one up
			shadowDist = camera.GetNearClipDistance() * 300;
		}

		// Calculate shadow offset - this controls where we center the shadow texture
		// Smaller value brings shadow focus closer to camera for better quality on nearby objects
		float shadowOffset = shadowDist * (scene.GetShadowDirLightTextureOffset() * 0.3f);

		// Directional lights 
		if (light.GetType() == LightType::Directional)
		{
			// set up the shadow texture
			// Set ortho projection
			shadowCamera.SetProjectionType(ProjectionType::Orthographic);
			// set ortho window so that texture covers far dist
			shadowCamera.SetOrthoWindow(shadowDist * 2, shadowDist * 2);

			// Calculate look at position
			// We want to look at a spot shadowOffset away from near plane
			// 0.5 is a little too close for angles
			Vector3 target = camera.GetDerivedPosition() + (camera.GetDerivedDirection() * shadowOffset);

			// Calculate direction, which same as directional light direction
			dir = -light.GetDerivedDirection(); // backwards since point down -z
			dir.Normalize();

			// Calculate position
			// We want to be in the -ve direction of the light direction
			// far enough to project for the dir light extrusion distance
			pos = target + dir * scene.GetShadowDirectionalLightExtrusionDistance();

			// Round local x/y position based on a world-space texel; this helps to reduce
			// jittering caused by the projection moving with the camera
			// Viewport is 2 * near clip distance across (90 degree fov)
			//~ Real worldTexelSize = (texCam->getNearClipDistance() * 20) / vp->getActualWidth();
			//~ pos.x -= fmod(pos.x, worldTexelSize);
			//~ pos.y -= fmod(pos.y, worldTexelSize);
			//~ pos.z -= fmod(pos.z, worldTexelSize);
			int32 vw;
			GraphicsDevice::Get().GetViewport(nullptr, nullptr, &vw);
			float worldTexelSize = (shadowDist * 2) / static_cast<float>(vw);

			//get texCam orientation
			Vector3 up = Vector3::UnitY;

			// Check it's not coincident with dir
			if (::fabsf(up.Dot(dir)) >= 0.99f)
			{
				// Use camera up
				up = Vector3::UnitZ;
			}

			// 1. build an orthonormal basis that matches DirectX LH look‑at
			Vector3 right = up.Cross(dir);   //  X  axis  (points right)
			right.Normalize();
			up = dir.Cross(right);        //  Y  axis  (re‑derived, keeps it orthogonal)
			up.Normalize();

			// 2. build the quaternion from X,Y,Z
			Quaternion q;
			q.FromAxes(right, up, dir);      // q rotates local +Z onto 'dir'

			//convert world space camera position into light space
			Vector3 lightSpacePos = q.Inverse() * pos;

			//snap to nearest texel
			lightSpacePos.x = std::floor(lightSpacePos.x / worldTexelSize) * worldTexelSize;
			lightSpacePos.y = std::floor(lightSpacePos.y / worldTexelSize) * worldTexelSize;

			//convert back to world space
			pos = q * lightSpacePos;

			// TODO: Other light types
			shadowCamera.GetParentNode()->SetPosition(pos);
			shadowCamera.GetParentSceneNode()->LookAt(target, TransformSpace::World, Vector3::UnitZ);
		}

		shadowCamera.InvalidateView();
		shadowCamera.InvalidateFrustum();
	}
}