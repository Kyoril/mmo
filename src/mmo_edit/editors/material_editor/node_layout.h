#pragma once

#include <imgui.h>

namespace mmo
{
	class Grid
	{
	public:
	    void Begin(const char* id, int columns, float width = -1.0f);
	    void Begin(ImU32 id, int columns, float width = -1.0f);
	    void NextColumn();
	    void NextRow();
	    void SetColumnAlignment(float alignment);
	    void End();

	private:
	    [[nodiscard]] int Seed(const int column, const int row) const { return column + row * m_columns; }
	    [[nodiscard]] int Seed() const { return Seed(m_column, m_row); }
	    [[nodiscard]] int ColumnSeed(const int column) const { return Seed(column, -1); }
	    [[nodiscard]] int ColumnSeed() const { return ColumnSeed(m_column); }
	    void EnterCell(int column, int row);
	    void LeaveCell();

	private:
	    int m_columns = 1;
	    int m_row = 0;
	    int m_column = 0;
	    float m_minimumWidth = -1.0f;
	    ImVec2 m_cursorPos;
	    ImGuiStorage* m_storage = nullptr;
	    float m_columnAlignment = 0.0f;
	    float m_maximumColumnWidthAcc = -1.0f;
	};
	}
