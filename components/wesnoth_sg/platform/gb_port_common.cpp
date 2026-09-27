// platform/gb_port_common.cpp
// Partagé device + host + SDL : ne dépend que de gb::text()/gb::text_width(),
// donc aucune divergence possible entre plateformes sur la logique de
// découpage en lignes elle-même. Le rendu UTF-8 proprement dit (accents
// FR/DE/ES) vit dans gb_text_render.h, utilisé directement par chaque
// implémentation de text()/text_width() (gb_port_aka.cpp, gb_port_host.cpp,
// gb_port_sdl.cpp) -- pas ici, qui ne fait que le retour à la ligne.
#include "platform/gb_port.h"

#include <sstream>

namespace gb {

int text_wrapped(int x, int y, int max_width_px, int line_height_px,
                  const std::string& s, Color c) {
    std::istringstream words(s);
    std::string word, line;
    int lines_drawn = 0;
    int cur_y = y;

    auto flush_line = [&]() {
        if (!line.empty()) {
            text(x, cur_y, line.c_str(), c);
            cur_y += line_height_px;
            lines_drawn++;
            line.clear();
        }
    };

    while (words >> word) {
        std::string candidate = line.empty() ? word : (line + " " + word);
        if (text_width(candidate.c_str()) > max_width_px && !line.empty()) {
            flush_line();
            line = word;
        } else {
            line = candidate;
        }
    }
    flush_line();
    return lines_drawn;
}

std::vector<std::string> wrap_text_lines(int max_width_px, const std::string& s) {
    std::vector<std::string> out;
    std::istringstream words(s);
    std::string word, line;
    auto flush_line = [&]() {
        if (!line.empty()) { out.push_back(line); line.clear(); }
    };
    while (words >> word) {
        std::string candidate = line.empty() ? word : (line + " " + word);
        if (text_width(candidate.c_str()) > max_width_px && !line.empty()) {
            flush_line();
            line = word;
        } else {
            line = candidate;
        }
    }
    flush_line();
    return out;
}

int draw_text_lines(int x, int y, int line_height_px,
                     const std::vector<std::string>& lines, Color c,
                     int first_line, int max_lines) {
    int drawn = 0;
    int cur_y = y;
    for (int i = first_line; i < (int)lines.size() && drawn < max_lines; ++i, ++drawn) {
        text(x, cur_y, lines[i].c_str(), c);
        cur_y += line_height_px;
    }
    return drawn;
}

}  // namespace gb
