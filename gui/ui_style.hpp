#pragma once

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStyle>

namespace microspice::gui {

inline QIcon actionIcon(const QString& name) {
    return QIcon(":/ui/icons/" + name + ".png");
}

inline QIcon actionIcon(QStyle::StandardPixmap icon) {
    switch (icon) {
        case QStyle::SP_BrowserReload:
            return actionIcon("rotate-cw");
        case QStyle::SP_TrashIcon:
            return actionIcon("trash-2");
        case QStyle::SP_ArrowUp:
            return actionIcon("zoom-in");
        case QStyle::SP_ArrowDown:
            return actionIcon("zoom-out");
        case QStyle::SP_DesktopIcon:
            return actionIcon("maximize");
        case QStyle::SP_MediaPlay:
            return actionIcon("play");
        case QStyle::SP_MediaStop:
            return actionIcon("square");
        case QStyle::SP_FileIcon:
            return actionIcon("file-plus-2");
        case QStyle::SP_DialogOpenButton:
            return actionIcon("folder-open");
        case QStyle::SP_DialogSaveButton:
            return actionIcon("save");
        case QStyle::SP_ArrowBack:
            return actionIcon("undo-2");
        case QStyle::SP_ArrowForward:
            return actionIcon("redo-2");
        default:
            return QApplication::style()->standardIcon(icon);
    }
}

inline void applyDesktopStyle(QApplication& application) {
    application.setStyle("Fusion");
    auto palette = application.palette();
    palette.setColor(QPalette::Window, QColor("#f3f6f4"));
    palette.setColor(QPalette::WindowText, QColor("#263b34"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f3f6f4"));
    palette.setColor(QPalette::Text, QColor("#263b34"));
    palette.setColor(QPalette::Button, QColor("#f3f6f4"));
    palette.setColor(QPalette::ButtonText, QColor("#263b34"));
    palette.setColor(QPalette::Highlight, QColor("#007f78"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#88958e"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor("#88958e"));
    application.setPalette(palette);
    if (QFontDatabase::families().contains("Avenir Next")) {
        auto font = application.font();
        font.setFamily("Avenir Next");
        font.setPointSize(11);
        application.setFont(font);
    }
    application.setStyleSheet(R"(
        QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #dce4df; spacing: 3px; padding: 4px; }
        QToolBar::separator { background: #dce4df; width: 1px; margin: 6px; }
        QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 5px; }
        QToolButton:hover { background: #edf3f0; border-color: #d5e1da; }
        QToolButton:checked { background: #d9ede7; border-color: #7cb3a1; }
        QToolButton:pressed { background: #c6ded4; }
        QToolButton:focus { border-color: #007f78; }
        QToolButton:disabled { background: transparent; border-color: transparent; }
        QDockWidget { font-weight: 600; }
        QDockWidget::title { background: #e8eeea; padding: 7px 10px; }
        QLineEdit, QDoubleSpinBox, QComboBox { background: #ffffff; border: 1px solid #cbd7cf; border-radius: 4px; padding: 5px 7px; min-height: 18px; }
        QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #007f78; }
        QLineEdit[invalid="true"] { border-color: #ba3c3c; background: #fff4f3; }
        QLineEdit:disabled, QDoubleSpinBox:disabled { background: #edf1ee; color: #88958e; }
        QTabWidget::pane { border: 1px solid #dce4df; background: #ffffff; }
        QTabBar::tab { padding: 7px 12px; background: #e8eeea; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { background: #ffffff; border-bottom-color: #007f78; }
        QHeaderView::section { background: #edf2ee; border: 0; border-bottom: 1px solid #dce4df; padding: 6px; }
        QTableView { background: #ffffff; alternate-background-color: #f3f6f4; gridline-color: #e3e9e5; border: 1px solid #dce4df; }
        QPlainTextEdit { background: #ffffff; border: 0; padding: 8px; }
        QStatusBar { background: #e8eeea; border-top: 1px solid #dce4df; }
        QSplitter::handle { background: #e3eae5; }
        QProgressBar { border: 1px solid #cbd7cf; border-radius: 3px; text-align: center; max-height: 18px; }
        QProgressBar::chunk { background: #72b59b; }
        QToolTip { background: #263b34; color: #ffffff; border: 0; padding: 6px; }
    )");
}

}  // namespace microspice::gui