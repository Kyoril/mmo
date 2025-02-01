
#include "material_frame.h"

namespace mmo
{
	MaterialFrame::MaterialFrame(const std::string& type, const std::string& name)
		: Frame(type, name)
	{
	}

	MaterialFrame::~MaterialFrame()
	{
	}

	void MaterialFrame::Copy(Frame& other)
	{
		Frame::Copy(other);
	}

	void MaterialFrame::Update(const float elapsed)
	{
		Frame::Update(elapsed);


	}

	void MaterialFrame::DrawSelf()
	{
		Frame::DrawSelf();


	}
}
