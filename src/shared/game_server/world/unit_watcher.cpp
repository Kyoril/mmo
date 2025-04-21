// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_watcher.h"

namespace mmo
{
	UnitWatcher::UnitWatcher(const Circle& shape, VisibilityChange visibilityChanged)
		: m_shape(shape)
		, m_visibilityChanged(std::move(visibilityChanged))
	{
	}

	UnitWatcher::~UnitWatcher()
	{
	}

	void UnitWatcher::SetShape(const Circle& shape)
	{
		m_shape = shape;
		OnShapeUpdated();
	}
}