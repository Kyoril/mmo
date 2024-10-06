// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "editor_window_base.h"
#include "proto_data/proto_template.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "proto_data/project.h"

#include "imgui_listbox.h"

namespace mmo
{
	/// @brief Base class of a dockable editor ui window.
	template<class T1, class T2>
	class EditorEntryWindowBase : public EditorWindowBase
	{
	public:
		typedef T1 ArrayType;
		typedef T2 EntryType;

	public:
		explicit EditorEntryWindowBase(proto::Project& project, proto::TemplateManager<T1, T2>& manager, const String& name);
		virtual ~EditorEntryWindowBase() = default;

	public:
		/// @brief Called when it's time to draw the window.
		virtual bool Draw() override;

		virtual void DrawDetailsImpl(T2& currentEntry) = 0;

	protected:
		virtual void OnNewEntry(proto::TemplateManager<T1, T2>::EntryType& entry) {}

		virtual const String& EntryDisplayName(const T2& entry) { return entry.name(); }

	protected:
		proto::Project& m_project;
		proto::TemplateManager<T1, T2>& m_manager;
	};

	template <class T1, class T2>
	EditorEntryWindowBase<T1, T2>::EditorEntryWindowBase(proto::Project& project, proto::TemplateManager<T1, T2>& manager, const String& name)
		: EditorWindowBase(name)
		, m_project(project)
		, m_manager(manager)
	{
	}

	template <class T1, class T2>
	bool EditorEntryWindowBase<T1, T2>::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			ImGui::Columns(2, nullptr, true);
			static bool widthSet = false;
			if (!widthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				widthSet = true;
			}

			static int currentItem = -1;

			if (ImGui::Button("Add New", ImVec2(-1, 0)))
			{
				auto* entry = m_manager.add();
				OnNewEntry(*entry);
			}

			ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.spells.count());
			if (ImGui::Button("Remove", ImVec2(-1, 0)))
			{
				m_manager.remove(m_manager.getTemplates().entry().at(currentItem).id());
			}
			ImGui::EndDisabled();

			ImGui::BeginChild("entryListScrollable", ImVec2(-1, 0));
			ListBox("##entryList", &currentItem, [this](void* data, int idx, const char** out_text)
				{
					const T1* entries = static_cast<T1*>(data);
					const auto& entry = entries->entry().at(idx);
					*out_text = this->EntryDisplayName(entry).c_str();
					return true;
				}, &m_manager.getTemplates(), m_manager.count(), 20);
			ImGui::EndChild();

			ImGui::NextColumn();

			T2* currentEntry = nullptr;
			if (currentItem != -1 && currentItem < m_manager.count())
			{
				currentEntry = &m_manager.getTemplates().mutable_entry()->at(currentItem);
			}

			if (ImGui::BeginChild("entryDetails", ImVec2(-1, -1)))
			{
				if (currentEntry)
				{
					DrawDetailsImpl(*currentEntry);
				}
				ImGui::EndChild();
			}

			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}
}
