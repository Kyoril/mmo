#pragma once

namespace mmo
{
	class Scene;
	class Camera;
	class Light;

	/// This is a basic interface for setting up a shadow camera.
	class ShadowCameraSetup
	{
	public:
		ShadowCameraSetup() = default;
		virtual ~ShadowCameraSetup() = default;

	public:
		virtual void SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera) = 0;
	};

	class DefaultShadowCameraSetup : public ShadowCameraSetup
	{
	public:
		DefaultShadowCameraSetup() = default;
		~DefaultShadowCameraSetup() override = default;

	public:
		virtual void SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera) override;
	};
}