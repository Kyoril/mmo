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

	protected:
		float m_progress;
		Color m_progressColor = Color::White;
	};
}
