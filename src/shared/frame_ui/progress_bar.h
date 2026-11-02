// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame.h"

namespace mmo
{
	class ProgressBar : public Frame
	{
	public:
		explicit ProgressBar(const String& type, const String& name);
		~ProgressBar() override;

		void Copy(Frame& frame) override;

	public:
		void SetProgress(float progress);
		float GetProgress() const { return m_progress; }

	protected:
		void PopulateGeometryBuffer() override;

	private:
		void OnProgressChanged(const Property& property);
		void OnProgressColorChanged(const Property& property);
		void OnProgressInsetChanged(const Property& property);

	protected:
		float m_progress = 0.0f;
		/// Native UI inset applied before the fill width is scaled by progress.
		float m_progressInset = 0.0f;
		Color m_progressColor = Color::White;
	};
}
