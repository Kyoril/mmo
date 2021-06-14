
#include "loading_screen.h"
#include "screen.h"
#include "graphics/graphics_device.h"


namespace mmo
{
	static ScreenLayerIt s_paintLoadingScreenLayer;
	
	void LoadingScreen::Init()
	{
		s_paintLoadingScreenLayer = Screen::AddLayer(
			&LoadingScreen::Paint, 
			1000.0f, 
			ScreenLayerFlags::Disabled | ScreenLayerFlags::IdentityProjection | ScreenLayerFlags::IdentityTransform);
	}

	void LoadingScreen::Destroy()
	{
		Screen::RemoveLayer(s_paintLoadingScreenLayer);
	}

	void LoadingScreen::Paint()
	{
		// Get the current graphics device
		auto& gx = GraphicsDevice::Get();

		
	}

	void LoadingScreen::Show()
	{
		s_paintLoadingScreenLayer->flags &= ~ScreenLayerFlags::Disabled;
	}

	void LoadingScreen::Hide()
	{
		s_paintLoadingScreenLayer->flags |= ScreenLayerFlags::Disabled;
	}
}
