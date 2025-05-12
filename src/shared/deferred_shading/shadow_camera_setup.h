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
		DefaultShadowCameraSetup() 
			: m_focusOnSmallObjects(false)
			, m_smallObjectFocusSize(50.0f)
		{
		}
		
		~DefaultShadowCameraSetup() override = default;

	public:
		virtual void SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera) override;
		
		// Set these parameters to improve shadow quality for small objects
		void SetFocusOnSmallObjects(bool focus) { m_focusOnSmallObjects = focus; }
		void SetSmallObjectFocusSize(float size) { m_smallObjectFocusSize = size; }
		
		bool GetFocusOnSmallObjects() const { return m_focusOnSmallObjects; }
		float GetSmallObjectFocusSize() const { return m_smallObjectFocusSize; }

	private:
		bool m_focusOnSmallObjects;
		float m_smallObjectFocusSize;
	};
}