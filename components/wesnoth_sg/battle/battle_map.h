// battle/battle_map.h
//
// Parseur du format de carte Wesnoth réel : une ligne par rangée, cases
// séparées par des virgules, code de terrain WML par case (ex: "Gg", "Hh",
// "Gg^Fms"). C'est le format exact des fichiers .map officiels -- un vrai
// fichier de campagne se charge sans conversion, seulement en le collant
// tel quel dans un .map_data.
#pragma once
#include <string>
#include <sstream>
#include <vector>
#include "hex.h"
#include "terrain.h"

class BattleMap {
public:
    int width = 0, height = 0;

    bool load_from_string(const std::string& map_data) {
        rows_.clear();
        std::istringstream lines(map_data);
        std::string line;
        while (std::getline(lines, line)) {
            // ignore lignes vides (souvent en fin de fichier)
            if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            std::vector<TerrainClass> row;
            std::istringstream cells(line);
            std::string cell;
            while (std::getline(cells, cell, ',')) {
                // Wesnoth admet des espaces autour des virgules
                size_t a = cell.find_first_not_of(" \t");
                size_t b = cell.find_last_not_of(" \t");
                if (a == std::string::npos) continue;
                row.push_back(parse_terrain_code(cell.substr(a, b - a + 1)));
            }
            if (!row.empty()) rows_.push_back(std::move(row));
        }
        height = (int)rows_.size();
        width = height > 0 ? (int)rows_[0].size() : 0;
        return width > 0 && height > 0;
    }

    bool in_bounds(HexCoord h) const {
        return h.x >= 0 && h.x < width && h.y >= 0 && h.y < height;
    }

    TerrainClass terrain_at(HexCoord h) const {
        if (!in_bounds(h)) return TerrainClass::Unwalkable;
        return rows_[h.y][h.x];
    }

private:
    std::vector<std::vector<TerrainClass>> rows_;
};
