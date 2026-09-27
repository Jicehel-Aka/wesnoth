// text_markup.h — retire les balises de mise en forme Pango/WML (<b>, </i>,
// <span ...>, etc.) du texte affiché.
//
// Le WML d'origine de Wesnoth utilise ce genre de balises pour la mise en
// forme (gras/italique/couleur) dans son moteur PC, qui a un vrai rendu de
// texte riche. Notre police 8x8 (aka_font) ne sait afficher que du texte
// brut : sans ce nettoyage, les balises apparaissent telles quelles à
// l'écran (ex. "Oh, <i><b>now</b></i> you want my help?").
//
// Utilisé partout où du texte extrait du WML est affiché : les répliques de
// récit (campaign_loader.cpp) et les messages d'événements de bataille
// (events.cpp). Un seul point de nettoyage pour les deux, plutôt que de
// dupliquer la logique.
#pragma once
#include <string>

namespace wesnoth_sg {

inline std::string strip_markup(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_tag = false;
    for (char c : in) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out += c;
    }
    // Entites XML minimales que Pango peut produire dans ce texte.
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all(out, "&amp;", "&");
    replace_all(out, "&lt;", "<");
    replace_all(out, "&gt;", ">");
    replace_all(out, "&quot;", "\"");
    replace_all(out, "&apos;", "'");
    return out;
}

}  // namespace wesnoth_sg
