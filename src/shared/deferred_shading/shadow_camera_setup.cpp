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
		
		// Set reasonable near clip distance to avoid artifacts
		shadowCamera.SetNearClipDistance(0.1f);
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
		float shadowOffset = shadowDist * (scene.GetShadowDirLightTextureOffset() * 0.5f);  // Increased from 0.3 to 0.5 to put more shadow detail near camera

		// Directional lights 
		if (light.GetType() == LightType::Directional)
		{
			// Set ortho projection for directional light
			shadowCamera.SetProjectionType(ProjectionType::Orthographic);
			
			// Significantly tighter ortho window for better texel density
			shadowCamera.SetOrthoWindow(shadowDist * 1.2f, shadowDist * 1.2f);  // Reduced from 1.5f
			
			// Calculate look at position - target a spot in front of camera
			Vector3 target = camera.GetDerivedPosition() + (camera.GetDerivedDirection() * shadowOffset);
			
			// Calculate light direction and normalize
			dir = -light.GetDerivedDirection();
			dir.Normalize();
			
			// Calculate the shadow camera position
			float extrusionDistance = scene.GetShadowDirectionalLightExtrusionDistance();
			pos = target + dir * extrusionDistance;
			
			// Use the actual high-resolution shadow map size
			float shadowMapResolution = 8192.0f; // Match the increased resolution
			float worldTexelSize = (shadowCamera.GetOrthoWindowWidth() / shadowMapResolution);
			
			// Setup camera orientation vectors
			Vector3 up = Vector3::UnitY;
			
			// Make sure up vector isn't aligned with light direction
			if (::fabsf(up.Dot(dir)) >= 0.99f)
			{
				// Use Z axis as up vector instead
				up = Vector3::UnitZ;
			}
			
			// Build an orthonormal basis for the shadow camera
			Vector3 right = up.Cross(dir);
			right.Normalize();
			up = dir.Cross(right);
			up.Normalize();
			
			// Create orientation quaternion
			Quaternion q;
			q.FromAxes(right, up, dir);
			
			// Transform position to light space
			Vector3 lightSpacePos = q.Inverse() * pos;
			
			// More precise texel snapping with higher resolution
			lightSpacePos.x = std::floor(lightSpacePos.x / worldTexelSize) * worldTexelSize;
			lightSpacePos.y = std::floor(lightSpacePos.y / worldTexelSize) * worldTexelSize;
			
			// Transform back to world space
			pos = q * lightSpacePos;
			
			// Set camera position and orientation
			shadowCamera.GetParentNode()->SetPosition(pos);
			shadowCamera.GetParentSceneNode()->LookAt(target, TransformSpace::World, Vector3::UnitZ);
		}

		shadowCamera.InvalidateView();
		shadowCamera.InvalidateFrustum();
	}
}