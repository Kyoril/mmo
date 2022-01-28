#include "node_layout.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

namespace mmo
{
	void Grid::Begin(const char* id, const int columns, const float width)
	{
	    Begin(ImGui::GetID(id), columns, width);
	}

	void Grid::Begin(const ImU32 id, const int columns, const float width)
	{
	    m_cursorPos = ImGui::GetCursorScreenPos();

	    ImGui::PushID(id);
	    m_columns = ImMax(1, columns);
	    m_storage = ImGui::GetStateStorage();

	    for (int i = 0; i < columns; ++i)
	    {
	        ImGui::PushID(ColumnSeed());
	        m_storage->SetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
	        ImGui::PopID();
	    }

	    m_columnAlignment = 0.0f;
	    m_minimumWidth = width;

	    ImGui::BeginGroup();

	    EnterCell(0, 0);
	}

	void Grid::NextColumn()
	{
	    LeaveCell();

	    int nextColumn = m_column + 1;
	    int nextRow    = 0;
	    if (nextColumn > m_columns)
	    {
	        nextColumn -= m_columns;
	        nextRow    += 1;
	    }

	    auto cursorPos = m_cursorPos;
	    for (int i = 0; i < nextColumn; ++i)
	    {
	        ImGui::PushID(ColumnSeed(i));
	        const auto maximumColumnWidth = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
	        ImGui::PopID();

	        if (maximumColumnWidth > 0.0f)
	        {
				cursorPos.x += maximumColumnWidth + ImGui::GetStyle().ItemSpacing.x;   
	        }
	    }

	    ImGui::SetCursorScreenPos(cursorPos);

	    EnterCell(nextColumn, nextRow);
	}

	void Grid::NextRow()
	{
	    LeaveCell();

	    auto cursorPos = ImGui::GetCursorScreenPos();
	    cursorPos.x = m_cursorPos.x;
	    for (int i = 0; i < m_column; ++i)
	    {
	        ImGui::PushID(ColumnSeed(i));
	        const auto maximumColumnWidth = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
	        ImGui::PopID();

	        if (maximumColumnWidth > 0.0f)
	        {
		        cursorPos.x += maximumColumnWidth + ImGui::GetStyle().ItemSpacing.x;
	        }
	    }

	    ImGui::SetCursorScreenPos(cursorPos);

	    EnterCell(m_column, m_row + 1);
	}

	void Grid::EnterCell(const int column, const int row)
	{
	    m_column = column;
	    m_row    = row;

	    ImGui::PushID(ColumnSeed());
	    m_maximumColumnWidthAcc = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
	    const auto maximumColumnWidth = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
	    ImGui::PopID();

	    ImGui::PushID(Seed());
	    const auto lastCellWidth = m_storage->GetFloat(ImGui::GetID("LastCellWidth"), -1.0f);

	    if (maximumColumnWidth >= 0.0f && lastCellWidth >= 0.0f)
	    {
		    const auto freeSpace = maximumColumnWidth - lastCellWidth;
		    const auto offset = ImFloor(m_columnAlignment * freeSpace);

	        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	        ImGui::Dummy(ImVec2(offset, 0.0f));
	        ImGui::SameLine(0.0f, 0.0f);
	        ImGui::PopStyleVar();
	    }

	    ImGui::BeginGroup();
	}

	void Grid::LeaveCell()
	{
	    ImGui::EndGroup();

	    const auto itemSize = ImGui::GetItemRectSize();
	    m_storage->SetFloat(ImGui::GetID("LastCellWidth"), itemSize.x);
	    ImGui::PopID();

	    m_maximumColumnWidthAcc = ImMax(m_maximumColumnWidthAcc, itemSize.x);
	    ImGui::PushID(ColumnSeed());
	    m_storage->SetFloat(ImGui::GetID("MaximumColumnWidthAcc"), m_maximumColumnWidthAcc);
	    ImGui::PopID();
	}

	void Grid::SetColumnAlignment(float alignment)
	{
	    alignment = ImClamp(alignment, 0.0f, 1.0f);
	    m_columnAlignment = alignment;
	}

	void Grid::End()
	{
	    LeaveCell();

	    ImGui::EndGroup();

	    float totalWidth = 0.0f;
	    for (int i = 0; i < m_columns; ++i)
	    {
	        ImGui::PushID(ColumnSeed(i));
	        const auto currentMaxCellWidth = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
	        totalWidth += currentMaxCellWidth;
	        m_storage->SetFloat(ImGui::GetID("MaximumColumnWidth"), currentMaxCellWidth);
	        ImGui::PopID();
	    }

	    if (totalWidth < m_minimumWidth)
	    {
	        auto spaceToDivide = m_minimumWidth - totalWidth;
	        auto spacePerColumn = ImCeil(spaceToDivide / m_columns);

	        for (int i = 0; i < m_columns; ++i)
	        {
	            ImGui::PushID(ColumnSeed(i));
	            auto columnWidth = m_storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
	            columnWidth += spacePerColumn;
	            m_storage->SetFloat(ImGui::GetID("MaximumColumnWidth"), columnWidth);
	            ImGui::PopID();

	            spaceToDivide -= spacePerColumn;
	            if (spaceToDivide < 0)
	            {
		            spacePerColumn += spaceToDivide;
	            }
	        }
	    }

	    ImGui::PopID();
	}
}
