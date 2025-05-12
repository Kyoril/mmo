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

		// Calculate shadow offset - controls where we center the shadow texture
		// Using a smaller value for shadowOffset to focus more on close objects
		float shadowOffset = shadowDist * 0.1f;

		// Directional lights 
		if (light.GetType() == LightType::Directional)
		{
			// Set orthographic projection for directional light
			shadowCamera.SetProjectionType(ProjectionType::Orthographic);

			// Calculate direction from light
			dir = -light.GetDerivedDirection(); // backwards since point down -z
			dir.Normalize();

			// Calculate target position - we'll focus on an area near the camera
			Vector3 cameraPos = camera.GetDerivedPosition();
			Vector3 cameraDir = camera.GetDerivedDirection();
			Vector3 target = cameraPos + (cameraDir * shadowOffset);

			// Calculate shadow camera position
			float shadowDistance = scene.GetShadowDirectionalLightExtrusionDistance();
			pos = target + dir * shadowDistance;

			// Calculate an appropriate orthographic window size
			// For small/close objects, we want a smaller window size
			float orthoSize;
			
			// Use smaller ortho window for close-up shadows
			if (m_focusOnSmallObjects)
			{
				// Use a much smaller window size for focusing on small objects
				orthoSize = m_smallObjectFocusSize;
			}
			else
			{
				// Traditional size based on shadow distance - suitable for larger areas
				orthoSize = shadowDist;
			}
			
			shadowCamera.SetOrthoWindow(orthoSize * 2, orthoSize * 2);

			// Apply texel snapping to reduce shadow shimmer/jitter
			int32 vw;
			GraphicsDevice::Get().GetViewport(nullptr, nullptr, &vw);
			float worldTexelSize = (orthoSize * 2) / static_cast<float>(vw);

			// Build the light orientation basis
			Vector3 up = Vector3::UnitY;
			if (::fabsf(up.Dot(dir)) >= 0.99f)
			{
				up = Vector3::UnitZ;
			}

			// Create orthonormal basis
			Vector3 right = up.Cross(dir);
			right.Normalize();
			up = dir.Cross(right);
			up.Normalize();

			// Build quaternion from basis
			Quaternion q;
			q.FromAxes(right, up, dir);

			// Convert position to light space
			Vector3 lightSpacePos = q.Inverse() * pos;

			// Snap to nearest texel to reduce jittering
			lightSpacePos.x = std::floor(lightSpacePos.x / worldTexelSize) * worldTexelSize;
			lightSpacePos.y = std::floor(lightSpacePos.y / worldTexelSize) * worldTexelSize;

			// Convert back to world space
			pos = q * lightSpacePos;

			// Set the shadow camera position and orientation
			shadowCamera.GetParentNode()->SetPosition(pos);
			shadowCamera.GetParentSceneNode()->LookAt(target, TransformSpace::World, Vector3::UnitZ);
		}

		shadowCamera.InvalidateView();
		shadowCamera.InvalidateFrustum();
	}
}
