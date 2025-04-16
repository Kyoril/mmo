
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
		shadowCamera.SetNearClipDistance(light.DeriveShadowNearClipDistance(camera));
		shadowCamera.SetFarClipDistance(light.DeriveShadowFarClipDistance(camera));

		// get the shadow frustum's far distance
		float shadowDist = light.GetShadowFarDistance();
		if (!shadowDist)
		{
			// need a shadow distance, make one up
			shadowDist = camera.GetNearClipDistance() * 300;
		}

		float shadowOffset = shadowDist * (scene.GetShadowDirLightTextureOffset());

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
			if (::fabsf(up.Dot(dir)) >= 1.0f)
			{
				// Use camera up
				up = Vector3::UnitZ;
			}

			// cross twice to re-derive, only direction is unaltered
			Vector3 left = dir.Cross(up);
			left.Normalize();
			up = dir.Cross(left);
			up.Normalize();

			// Derive quaternion from axes
			Quaternion q;
			q.FromAxes(left, up, dir);

			//convert world space camera position into light space
			Vector3 lightSpacePos = q.Inverse() * pos;

			//snap to nearest texel
			lightSpacePos.x -= fmod(lightSpacePos.x, worldTexelSize);
			lightSpacePos.y -= fmod(lightSpacePos.y, worldTexelSize);

			//convert back to world space
			pos = q * lightSpacePos;

		}
		// TODO: Other light types

		// Finally set position
		shadowCamera.GetParentNode()->SetPosition(pos);

		// Calculate orientation based on direction calculated above
		/*
		// Next section (camera oriented shadow map) abandoned
		// Always point in the same direction, if we don't do this then
		// we get 'shadow swimming' as camera rotates
		// As it is, we get swimming on moving but this is less noticeable

		// calculate up vector, we want it aligned with cam direction
		Vector3 up = cam->getDerivedDirection();
		// Check it's not coincident with dir
		if (up.dotProduct(dir) >= 1.0f)
		{
		// Use camera up
		up = cam->getUp();
		}
		*/
		Vector3 up = Vector3::UnitY;
		// Check it's not coincident with dir
		if (::fabsf(up.Dot(dir)) >= 1.0f)
		{
			// Use camera up
			up = Vector3::UnitZ;
		}
		// cross twice to rederive, only direction is unaltered
		Vector3 left = dir.Cross(up);
		left.Normalize();
		up = dir.Cross(left);
		up.Normalize();
		// Derive quaternion from axes
		Quaternion q;
		q.FromAxes(left, up, dir);
		shadowCamera.GetParentNode()->SetOrientation(q);
	}
}
