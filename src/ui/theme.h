#pragma once

#include <QString>

#include <vector>

namespace ui {

// Theme engine — mirrors ui/styles.py (5 glassmorphic themes + stylesheet
// builder). Palette values map 1:1 to the Python THEMES dict.
struct Theme {
    QString bg;
    QString panel_bg;
    QString header_bg;
    QString accent;
    QString accent_glow;
    QString accent_secondary;
    QString text;
    QString text_bright;
    QString border;
    QString btn_bg;
    QString btn_hover;
    QString btn_border;
    QString status_online;
    QString status_motion;
    QString status_human;
    QString status_offline;
};

// Ordered list of theme names (for the theme combo). Default first.
const std::vector<QString>& themeNames();

Theme themeByName(const QString& name);

// Full application stylesheet for a theme (fallback: Cyberpunk Neon).
QString buildStylesheet(const QString& theme_name);

}  // namespace ui
