#include "ui/theme.h"

namespace ui {

namespace {

std::vector<Theme> allThemes() {
    return {
        { // Cyberpunk Neon
            "#070913", "#0d1124", "#090c1a", "#00f0ff", "rgba(0, 240, 255, 0.25)",
            "#ff0055", "#94a3b8", "#f8fafc", "#1e293b", "#131b35", "#1e294d",
            "rgba(0, 240, 255, 0.3)", "#00ffcc", "#ffb700", "#00f0ff", "#ff2a55"},
        { // Matrix Emerald
            "#050c07", "#0b1a0e", "#071209", "#00ff66", "rgba(0, 255, 102, 0.25)",
            "#00cc44", "#86efac", "#f0fdf4", "#14532d", "#0e2715", "#163a20",
            "rgba(0, 255, 102, 0.3)", "#00ff66", "#facc15", "#00ffaa", "#f87171"},
        { // Obsidian OLED
            "#000000", "#0a0a0c", "#050507", "#38bdf8", "rgba(56, 189, 248, 0.25)",
            "#818cf8", "#94a3b8", "#f8fafc", "#1e293b", "#111827", "#1f2937",
            "rgba(56, 189, 248, 0.3)", "#38bdf8", "#fbbf24", "#00f0ff", "#f87171"},
        { // Deep Space Purple
            "#090514", "#120b29", "#0c071b", "#c084fc", "rgba(192, 132, 252, 0.25)",
            "#f0abfc", "#cbd5e1", "#f8fafc", "#2e1065", "#1e1140", "#2e1a66",
            "rgba(192, 132, 252, 0.3)", "#c084fc", "#fb923c", "#e879f9", "#f43f5e"},
        { // Stealth Red Alert
            "#0f0506", "#1a080b", "#130608", "#f43f5e", "rgba(244, 63, 94, 0.25)",
            "#fb923c", "#fda4af", "#fff1f2", "#4c0519", "#2b0b10", "#421018",
            "rgba(244, 63, 94, 0.3)", "#f43f5e", "#fb923c", "#ff6b00", "#9f1239"},
    };
}

}  // namespace

const std::vector<QString>& themeNames() {
    static const std::vector<QString> names = {
        "Cyberpunk Neon", "Matrix Emerald", "Obsidian OLED",
        "Deep Space Purple", "Stealth Red Alert"};
    return names;
}

Theme themeByName(const QString& name) {
    const auto themes = allThemes();
    const auto& names = themeNames();
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return themes[i];
    }
    return themes.empty() ? Theme{} : themes[0];
}

QString buildStylesheet(const QString& theme_name) {
    const Theme t = themeByName(theme_name);

    const auto& bg = t.bg;
    const auto& panel = t.panel_bg;
    const auto& header = t.header_bg;
    const auto& accent = t.accent;
    const auto& glow = t.accent_glow;
    const auto& text = t.text;
    const auto& bright = t.text_bright;
    const auto& border = t.border;
    const auto& btn_bg = t.btn_bg;
    const auto& btn_hover = t.btn_hover;

    return QString(R"(
QMainWindow, QWidget {
    background-color: %1;
    color: %6;
    font-family: 'Inter', 'Segoe UI', Roboto, sans-serif;
}
QMainWindow { background-color: %1; }
QDialog { background-color: %1; }

QMenuBar {
    background-color: %3;
    color: %6;
    border-bottom: 1px solid %7;
    padding: 4px 8px;
}
QMenuBar::item:selected {
    background-color: %9;
    border-radius: 6px;
    color: %8;
}
QMenu {
    background-color: %2;
    border: 1px solid %7;
    border-radius: 10px;
    padding: 6px;
}
QMenu::item { padding: 6px 14px; border-radius: 6px; }
QMenu::item:selected { background-color: %9; color: %8; }

QPushButton {
    background-color: %10;
    color: %8;
    border-top: 1px solid rgba(255, 255, 255, 0.15);
    border-left: 1px solid rgba(255, 255, 255, 0.1);
    border-right: 1px solid rgba(0, 0, 0, 0.5);
    border-bottom: 3px solid rgba(0, 0, 0, 0.7);
    border-radius: 8px;
    padding: 6px 16px;
    margin-bottom: 3px;
    font-size: 12px;
    font-weight: 700;
    letter-spacing: 0.3px;
}
QPushButton:hover {
    background-color: %9;
    border-bottom-color: %4;
    border-top-color: %4;
    color: %4;
}
QPushButton:pressed {
    background-color: %1;
    border-top: 2px solid rgba(0, 0, 0, 0.9);
    border-left: 2px solid rgba(0, 0, 0, 0.9);
    border-bottom: 1px solid %4;
    color: %8;
    padding-top: 7px;
    padding-left: 17px;
}
QPushButton:checked {
    background-color: %5;
    border-bottom-color: %4;
    border-top-color: %4;
    color: %8;
}
QPushButton:disabled { color: %7; background-color: %1; border-color: %7; }

QPushButton#dangerBtn {
    background-color: rgba(244, 63, 94, 0.15);
    border-top: 1px solid rgba(244, 63, 94, 0.4);
    border-left: 1px solid rgba(244, 63, 94, 0.3);
    border-bottom: 3px solid rgba(159, 18, 57, 0.8);
    color: #f43f5e;
}
QPushButton#dangerBtn:hover { background-color: rgba(244, 63, 94, 0.3); border-bottom-color: #f43f5e; color: #ffffff; }
QPushButton#dangerBtn:pressed { border-bottom: 1px solid #f43f5e; padding-top: 7px; padding-left: 17px; }

QPushButton#addBtn {
    background-color: rgba(34, 197, 94, 0.15);
    border-top: 1px solid rgba(34, 197, 94, 0.4);
    border-left: 1px solid rgba(34, 197, 94, 0.3);
    border-bottom: 3px solid rgba(21, 128, 61, 0.8);
    color: #4ade80;
}
QPushButton#addBtn:hover { background-color: rgba(34, 197, 94, 0.3); border-bottom-color: #4ade80; color: #ffffff; }
QPushButton#addBtn:pressed { border-bottom: 1px solid #4ade80; padding-top: 7px; padding-left: 17px; }

QPushButton#primaryBtn {
    background-color: rgba(56, 189, 248, 0.15);
    border-top: 1px solid rgba(56, 189, 248, 0.4);
    border-left: 1px solid rgba(56, 189, 248, 0.3);
    border-bottom: 3px solid rgba(3, 105, 161, 0.8);
    color: #38bdf8;
}
QPushButton#primaryBtn:hover { background-color: rgba(56, 189, 248, 0.3); border-bottom-color: #38bdf8; color: #ffffff; }
QPushButton#primaryBtn:pressed { border-bottom: 1px solid #38bdf8; padding-top: 7px; padding-left: 17px; }

QLabel { color: %6; background: transparent; }
QLabel#headerTitle {
    font-size: 16px;
    font-weight: 800;
    color: %4;
    letter-spacing: 2px;
}

QGroupBox {
    background-color: %2;
    border: 1px solid %7;
    border-radius: 12px;
    margin-top: 16px;
    padding: 16px 12px 12px 12px;
    font-weight: 700;
    font-size: 11px;
    color: %6;
    text-transform: uppercase;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 3px 12px;
    background-color: %10;
    border-radius: 6px;
    color: %4;
    border: 1px solid %7;
}

QScrollBar:vertical { background: %1; width: 6px; margin: 0; border-radius: 3px; }
QScrollBar::handle:vertical { background: %7; min-height: 24px; border-radius: 3px; }
QScrollBar::handle:vertical:hover { background: %4; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

QListWidget, QTreeWidget, QTableWidget {
    background-color: %2;
    border: 1px solid %7;
    border-radius: 10px;
    padding: 6px;
    outline: none;
}
QListWidget::item, QTableWidget::item {
    background-color: %10;
    border: 1px solid %7;
    border-radius: 8px;
    padding: 8px 12px;
    margin: 3px 1px;
    color: %6;
}
QListWidget::item:selected, QTableWidget::item:selected {
    background-color: %9;
    border-color: %4;
    color: %8;
}

QComboBox {
    background-color: %10;
    border: 1px solid %7;
    border-radius: 8px;
    padding: 6px 12px;
    color: %8;
    font-size: 12px;
    font-weight: 600;
}
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow {
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid %4;
    margin-right: 6px;
}
QComboBox:hover { border-color: %4; }
QComboBox QAbstractItemView {
    background-color: %2;
    border: 1px solid %7;
    selection-background-color: %9;
    selection-color: %8;
}

QSpinBox, QDoubleSpinBox, QLineEdit {
    background-color: %10;
    border: 1px solid %7;
    border-radius: 8px;
    padding: 7px 12px;
    color: %8;
    font-size: 12px;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %4; }

QCheckBox { color: %6; spacing: 8px; font-size: 12px; }
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 4px;
    border: 1px solid %7;
    background: %1;
}
QCheckBox::indicator:checked {
    background: %4;
    border-color: %4;
}

QSlider::groove:horizontal {
    height: 4px;
    background: %7;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: %4;
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: %8; }

QSplitter::handle { background-color: %7; width: 1px; }

QStatusBar {
    background-color: %3;
    border-top: 1px solid %7;
    color: %6;
    font-size: 11px;
}
QStatusBar QLabel { color: %6; }

QScrollArea { border: none; background: transparent; }
)").arg(bg).arg(panel).arg(header).arg(accent).arg(glow).arg(text)
    .arg(border).arg(bright).arg(btn_hover).arg(btn_bg);
}

}  // namespace ui
