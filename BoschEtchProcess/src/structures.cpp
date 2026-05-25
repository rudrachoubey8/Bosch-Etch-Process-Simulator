#include "structures.h"

Grid::Grid(int X_, int Y_, int Z_)
    : X(X_), Y(Y_), Z(Z_), voxels(X_* Y_* Z_) {}

int Grid::index(int x, int y, int z) {
    return x + X * (y + Y * z);
}

Voxel& Grid::at(int x, int y, int z) {
    return voxels[index(x, y, z)];
}

bool Grid::inBounds(int x, int y, int z) {
    return (x < X && y < Y && z < Z && x >= 0 && y >= 0 && z >= 0);
}

void RenderDynamicInputGrid(int& numCols, int& numRows, std::vector<float>& gridData, int offset)
{
    ImGui::Text("Grid Dimensions:");
    bool resized = false;

    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Voxels", &numRows)) {
        if (numRows < 1) numRows = 1; 
        resized = true;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Particles", &numCols)) {
        if (numCols < 1) numCols = 1; 
        resized = true;
    }

    if (resized) {
        size_t oldSize = gridData.size();
        size_t newSize = static_cast<size_t>(numRows * numCols) * 3;

        gridData.resize(newSize);

        if (newSize > oldSize) {
            std::fill(gridData.begin() + oldSize, gridData.end(), 0.0f);
        }
    }

    if (gridData.empty()) {
        gridData.resize(numRows * numCols, 0.0f);
    }

    ImGui::Separator();
    ImGui::Spacing();

    int totalTableColumns = numCols + 1;

    if (ImGui::BeginTable("dynamic_grid", totalTableColumns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("##Corner", ImGuiTableColumnFlags_WidthFixed, 50.0f);

        for (int c = 0; c < numCols; c++) {
            std::string colLabel = "Particle " + std::to_string(c);
            ImGui::TableSetupColumn(colLabel.c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        }
        ImGui::TableHeadersRow();

        for (int r = 0; r < numRows; r++)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Voxel %d", r);

            for (int c = 0; c < numCols; c++)
            {
                ImGui::TableSetColumnIndex(c + 1);

                ImGui::PushID(r * 1000 + c + offset * numCols * numRows);

                ImGui::PushItemWidth(-FLT_MIN); 

                ImGui::InputFloat("##cell_input", &gridData[r * numCols + c + offset * numCols * numRows], 0, 0);

                ImGui::PopItemWidth();
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}