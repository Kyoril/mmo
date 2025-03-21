// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "condition_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/gossip.h"
#include "game/quest.h"
#include "log/default_log_levels.h"
#include "math/clamp.h"

namespace mmo
{
	ConditionEditorWindow::ConditionEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.conditions, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Conditions";
	}

    namespace
	{
        static const char* s_questParamValues[] = {
            "REWARDED",
            "COMPLETED",
            "UNAVAILABLE",
            "IN PROGRESS",
            "AVAILABLE",
            "FAILED"
        };

        constexpr int32 GetQuestParam2Count()
        {
            return std::size(s_questParamValues);
        }

        static_assert(std::size(s_questParamValues) == GetQuestParam2Count(), "QuestParam2 values do not match the expected count");

		const char* GetQuestParam2(int param2)
		{
			static const char* s_invalidValue = "<INVALID>";

            if (param2 >= 0 && param2 < GetQuestParam2Count())
            {
                return s_questParamValues[param2];
            }

            return s_invalidValue;
		}

        bool RenderQuestParam2(proto::Condition& condition, int32 param2)
		{
			if (ImGui::Combo("##quest_param2", &param2, s_questParamValues, std::size(s_questParamValues)))
			{
				// Set the new value
				condition.set_param2(param2);
                return true;
			}

            return false;
		}

        int32 GetConditionParamCount(const int type)
        {
            switch (type)
            {
            case proto::Condition_ConditionType_CLASS_CHECK:
                return 1;
            case proto::Condition_ConditionType_LEVEL_CHECK:
                return 2;
            case proto::Condition_ConditionType_QUEST_CHECK:
                return 2;
            case proto::Condition_ConditionType_NONE_TYPE:
                return 0;
            }

            return 0;
        }

        const char* GetConditionTypeName(int type)
        {
            static const char* s_unknown = "Unknown";
            static const char* conditionTypes[] = { "NONE", "CLASS_CHECK", "LEVEL_CHECK", "QUEST_CHECK" };

            if (type < 0 || type >= std::size(conditionTypes))
                return s_unknown;

            return conditionTypes[type];
        }

        const char* GetLogicOperatorName(int op)
        {
            static const char* s_unknown = "Unknown";
            static const char* operators[] = { "NONE", "AND", "OR" };

            if (op < 0 || op >= std::size(operators))
                return s_unknown;

            return operators[op];
        }

        void RenderConditionDescription(const proto::ConditionManager& conditions, const proto::Condition& condition)
        {
            const auto valueColor = ImVec4(0.2f, 0.4f, 1.0f, 1.0f);

            switch (condition.conditiontype())
            {
            case proto::Condition_ConditionType_CLASS_CHECK:
                ImGui::Text("PlayerClass is");
                ImGui::SameLine();
                ImGui::TextColored(valueColor, "%d", condition.param1());
                break;
            case proto::Condition_ConditionType_LEVEL_CHECK:
                ImGui::Text("PlayerLevel is");
                ImGui::SameLine();
                if (condition.param2() <= 0)
                {
                    ImGui::TextColored(valueColor, ">= %d", condition.param1());
                }
                else
                {
                    ImGui::TextColored(valueColor, ">= %d && <= %d", condition.param1(), condition.param2());
                }
                break;
            case proto::Condition_ConditionType_QUEST_CHECK:
                ImGui::Text("Quest %d", condition.param1());
                ImGui::SameLine();
                ImGui::TextColored(valueColor, GetQuestParam2(condition.param2()));
                break;
            case proto::Condition_ConditionType_NONE_TYPE:
                if (condition.logicoperator() == proto::Condition_LogicOperator_AND || condition.logicoperator() == proto::Condition_LogicOperator_OR)
                {
                    for (int i = 0; i < condition.subconditionids_size(); i++)
                    {
	                    if (const auto* subCond = conditions.getById(condition.subconditionids(i)))
                        {
                            RenderConditionDescription(conditions, *subCond);
                        }

                        if (i < condition.subconditionids_size() - 1)
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.1f, 1.0f, 0.1f, 1.0f), "%s", condition.logicoperator() == proto::Condition_LogicOperator_AND ? "AND" : "OR");
                            ImGui::SameLine();
                        }
                    }
                }
                break;
            default:
                ImGui::Text("Quest %d", condition.param1());
                break;
            }
        }
	}
    
	void ConditionEditorWindow::DrawDetailsImpl(proto::Condition& currentEntry)
	{
		if (ImGui::Button("Duplicate Condition"))
		{
			proto::Condition* copied = m_project.conditions.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 3, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}
		}

        bool conditionChanged = false;

        // Condition Type Combo
        {
            // We'll build a small array of known ConditionType values
            static const int conditionTypes[] = { 0, 1, 2, 3 };

            // and display them in a combo
            const char* preview = GetConditionTypeName(currentEntry.conditiontype());
            if (ImGui::BeginCombo("Condition Type", preview))
            {
                for (int t : conditionTypes)
                {
                    bool isSelected = (currentEntry.conditiontype() == t);
                    if (ImGui::Selectable(GetConditionTypeName(t), isSelected))
                    {
                        currentEntry.set_conditiontype(static_cast<proto::Condition_ConditionType>(t));
                        conditionChanged = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Logic Operator Combo
        {
            // We'll build a small array of known operators
            static const int logicOps[] = { 0, 1, 2 };

            if (ImGui::BeginCombo("Logic Operator", GetLogicOperatorName(currentEntry.logicoperator())))
            {
                for (int op : logicOps)
                {
                    bool isSelected = (currentEntry.logicoperator() == op);
                    if (ImGui::Selectable(GetLogicOperatorName(op), isSelected))
                    {
                        currentEntry.set_logicoperator(static_cast<mmo::proto::Condition_LogicOperator>(op));
                        conditionChanged = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

		const int32 paramCount = GetConditionParamCount(currentEntry.conditiontype());
        if (paramCount >= 1)
        {
            uint32 param = currentEntry.param1();
            if (ImGui::InputScalar("Param1", ImGuiDataType_U32, &param))
            {
                currentEntry.set_param1(param);
                conditionChanged = true;
            }

            if (paramCount >= 2)
            {
                param = currentEntry.param2();
				if (currentEntry.conditiontype() == proto::Condition_ConditionType_QUEST_CHECK)
				{
                    conditionChanged = conditionChanged || RenderQuestParam2(currentEntry, param);
				}
				else
				{
                    if (ImGui::InputScalar("Param2", ImGuiDataType_U32, &param))
                    {
                        currentEntry.set_param2(param);
                        conditionChanged = true;
                    }
				}

                if (paramCount >= 3)
                {
                    param = currentEntry.param3();
                    if (ImGui::InputScalar("Param3", ImGuiDataType_U32, &param))
                    {
                        currentEntry.set_param3(param);
                        conditionChanged = true;
                    }
                }
            }
        }
        
		if (currentEntry.logicoperator() != proto::Condition_LogicOperator_NONE_OPERATOR)
		{
            ImGui::Separator();

            ImGui::TextUnformatted("Sub-Conditions");

            // A child region to list subConditionIds with potential scrolling
            // We'll do drag & drop reordering + add/remove. 
            static int selectedSubIndex = -1; // which sub-condition is selected
            static bool openAddSubPopup = false;
            static uint32_t pendingAddSubId = 0;

            // Table with 2 columns: "Handle" and "Name"
            const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
                | ImGuiTableFlags_RowBg
                | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_Resizable;
            ImGui::BeginChild("SubConditionTableChild", ImVec2(0, 150), true);
            if (ImGui::BeginTable("SubConditionTable", 2, tableFlags))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("SubCondition Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (int i = 0; i < (int)currentEntry.subconditionids_size(); i++)
                {
                    uint32_t scId = currentEntry.subconditionids(i);
                    ImGui::TableNextRow();
                    for (int col = 0; col < 2; col++)
                    {
                        ImGui::TableSetColumnIndex(col);

                        // Unique label for drag-drop
                        char rowLabel[32];
                        sprintf(rowLabel, "##subCondRow_%d", i);

                        if (col == 0)
                        {
                            // Reorder handle
                            bool isSelected = (i == selectedSubIndex);

                            ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns
                                | ImGuiSelectableFlags_AllowItemOverlap;
                            if (ImGui::Selectable(rowLabel, isSelected, selFlags, ImVec2(0, 0)))
                            {
                                selectedSubIndex = i;
                            }

                            // Drag Source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("SUBCOND_REORDER", &i, sizeof(int));
                                ImGui::Text("Move SubCondition %u", scId);
                                ImGui::EndDragDropSource();
                            }

                            // Drag Target
                            if (ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SUBCOND_REORDER"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(int));
                                    int srcIndex = *(const int*)payload->Data;
                                    if (srcIndex != i)
                                    {
                                        auto* repeatedField = currentEntry.mutable_subconditionids();

                                        // Copy out
                                        std::vector<uint32_t> tmp;
                                        tmp.reserve(repeatedField->size());
                                        for (int idx = 0; idx < repeatedField->size(); idx++)
                                            tmp.push_back(repeatedField->Get(idx));

                                        // Reorder in `tmp` any way you like...
                                        uint32_t movedId = tmp[srcIndex];
                                        tmp.erase(tmp.begin() + srcIndex);
                                        int insertIdx = i;
                                        if (srcIndex < i)
                                            insertIdx = i - 1;
                                        tmp.insert(tmp.begin() + insertIdx, movedId);

                                        // Copy back
                                        repeatedField->Clear();
                                        for (auto val : tmp)
                                            repeatedField->Add(val);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            // Optional highlight if item hovered by some drag
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) && ImGui::GetDragDropPayload() != nullptr)
                            {
                                if (ImGui::GetDragDropPayload()->IsDataType("SUBCOND_REORDER"))
                                {
                                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(100, 255, 100, 80));
                                }
                            }
                        }
                        else
                        {
                            // Show name
                            const auto* cond = m_project.conditions.getById(scId);
                            ImGui::TextUnformatted(cond ? cond->name().c_str() : "(None)");
                        }
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndChild();

            // Add / Remove
            if (ImGui::Button("Add SubCondition"))
            {
                openAddSubPopup = true;
                pendingAddSubId = 0;
                ImGui::OpenPopup("AddSubConditionPopup");
            }

            ImGui::SameLine();

            bool canRemove = (selectedSubIndex >= 0 && selectedSubIndex < (int)currentEntry.subconditionids_size());
            if (!canRemove) ImGui::BeginDisabled();
            if (ImGui::Button("Remove SubCondition") && canRemove)
            {
                currentEntry.mutable_subconditionids()->erase(currentEntry.mutable_subconditionids()->begin() + selectedSubIndex);
                if (selectedSubIndex >= (int)currentEntry.subconditionids_size())
                    selectedSubIndex = (int)currentEntry.subconditionids_size() - 1;
            }
            if (!canRemove) ImGui::EndDisabled();

            // Popup for picking which sub-condition ID to add
            if (ImGui::BeginPopupModal("AddSubConditionPopup", &openAddSubPopup, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Select or enter a SubCondition ID to add:");

                static ImGuiTextFilter filter;
                filter.Draw("Filter###FilterSubCond", 200);

                // We'll list known IDs for convenience:
                ImGui::BeginChild("SubCondList", ImVec2(300, 200), true);
                for (const auto& condition : m_project.conditions.getTemplates().entry())
                {
                    // build a label
                    if (!filter.PassFilter(condition.name().c_str()))
                        continue;

                    bool isSelected = (pendingAddSubId == condition.id());
                    if (ImGui::Selectable(condition.name().c_str(), isSelected))
                    {
                        pendingAddSubId = condition.id();
                    }
                }
                ImGui::EndChild();

                ImGui::Separator();
                ImGui::TextUnformatted("Or type an ID manually:");
                ImGui::PushItemWidth(120);
                static char idBuf[32] = "";
                if (ImGui::IsWindowAppearing())
                    sprintf(idBuf, "%u", pendingAddSubId);

                if (ImGui::InputText("##ManualSubId", idBuf, IM_ARRAYSIZE(idBuf), ImGuiInputTextFlags_CharsDecimal))
                {
                    // parse
                    pendingAddSubId = (uint32_t)atoi(idBuf);
                }
                ImGui::PopItemWidth();

                ImGui::Separator();

                if (ImGui::Button("OK", ImVec2(100, 0)))
                {
                    currentEntry.mutable_subconditionids()->Add(pendingAddSubId);
                    selectedSubIndex = (int)currentEntry.subconditionids_size() - 1;
                    ImGui::CloseCurrentPopup();
                    openAddSubPopup = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)))
                {
                    ImGui::CloseCurrentPopup();
                    openAddSubPopup = false;
                }
                ImGui::EndPopup();
            }
		}

        // Render a preview of the condition
        ImGui::Separator();
        ImGui::TextUnformatted("Preview");
        ImGui::Separator();

        RenderConditionDescription(m_project.conditions, currentEntry);
	}

	void ConditionEditorWindow::OnNewEntry(proto::TemplateManager<proto::Conditions, proto::Condition>::EntryType& entry)
	{
		EditorEntryWindowBase<proto::Conditions, proto::Condition>::OnNewEntry(entry);

        entry.set_conditiontype(proto::Condition_ConditionType_NONE_TYPE);
        entry.set_logicoperator(proto::Condition_LogicOperator_NONE_OPERATOR);
	}
}
