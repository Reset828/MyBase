#ifndef THEMESTYLESHEET_H
#define THEMESTYLESHEET_H

#include <QString>

inline QString getThemeStyleSheet() {
    return R"(
        * {
            font-family: "Microsoft YaHei", "Segoe UI", "Helvetica Neue", sans-serif;
            font-size: 13px;
        }

        QMainWindow {
            background-color: #f5f9f5;
        }

        QMenuBar {
            background-color: #eaf4ea;
            border-bottom: 1px solid #c8e6c8;
            padding: 2px;
        }

        QMenuBar::item {
            padding: 6px 14px;
            color: #2e5c2e;
            border-radius: 4px;
        }

        QMenuBar::item:selected {
            background-color: #c8e6c8;
        }

        QMenu {
            background-color: #ffffff;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 4px;
        }

        QMenu::item {
            padding: 7px 28px;
            color: #2e5c2e;
            border-radius: 4px;
        }

        QMenu::item:selected {
            background-color: #d4edda;
        }

        QMenu::separator {
            height: 1px;
            background: #c8e6c8;
            margin: 4px 8px;
        }

        QToolBar {
            background-color: #eaf4ea;
            border-bottom: 1px solid #c8e6c8;
            padding: 4px 8px;
            spacing: 6px;
        }

        QToolBar QToolButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 5px 12px;
            color: #2e5c2e;
            font-size: 13px;
        }

        QToolBar QToolButton:hover {
            background-color: #d4edda;
            border: 1px solid #b8dab8;
        }

        QToolBar QToolButton:pressed {
            background-color: #c8e6c8;
        }

        QStatusBar {
            background-color: #eaf4ea;
            border-top: 1px solid #c8e6c8;
            color: #5a8a5a;
            font-size: 12px;
            padding: 2px 8px;
        }

        QSplitter::handle {
            background-color: #c8e6c8;
            width: 1px;
        }

        QSplitter::handle:horizontal {
            width: 1px;
        }

        QSplitter::handle:vertical {
            height: 1px;
        }

        QLineEdit {
            border: 1px solid #c8e6c8;
            border-radius: 8px;
            padding: 6px 12px;
            background-color: #ffffff;
            color: #2e5c2e;
            selection-background-color: #a8d8a8;
        }

        QLineEdit:focus {
            border: 1px solid #7bc47b;
        }

        QLineEdit::placeholder {
            color: #a0c0a0;
        }

        QListView {
            background-color: #ffffff;
            border: none;
            outline: none;
            padding: 4px;
        }

        QListView::item {
            padding: 8px 10px;
            border-bottom: 1px solid #eaf4ea;
            border-radius: 4px;
            color: #2e5c2e;
        }

        QListView::item:selected {
            background-color: #d4edda;
            color: #1a4a1a;
        }

        QListView::item:hover {
            background-color: #e8f5e8;
        }

        QTreeView {
            background-color: #ffffff;
            border: none;
            outline: none;
            padding: 4px;
        }

        QTreeView::item {
            padding: 5px 8px;
            border-radius: 3px;
            color: #2e5c2e;
        }

        QTreeView::item:selected {
            background-color: #d4edda;
            color: #1a4a1a;
        }

        QTreeView::item:hover {
            background-color: #e8f5e8;
        }

        QTreeView::branch {
            background-color: transparent;
        }

        QTabWidget::pane {
            border: none;
            background-color: #ffffff;
        }

        QTabBar::tab {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-bottom: none;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            padding: 8px 20px;
            margin-right: 2px;
            color: #5a8a5a;
            font-size: 13px;
        }

        QTabBar::tab:selected {
            background-color: #ffffff;
            color: #2e5c2e;
            font-weight: bold;
            border-bottom: 2px solid #7bc47b;
        }

        QTabBar::tab:hover:!selected {
            background-color: #d4edda;
        }

        QTabBar::close-button {
            image: none;
            subcontrol-position: right;
        }

        QTextEdit {
            background-color: #ffffff;
            border: none;
            color: #2e3e2e;
            selection-background-color: #c8e6c8;
            padding: 12px 16px;
            font-size: 14px;
            line-height: 1.6;
        }

        QTextBrowser {
            background-color: #fafefa;
            border: none;
            color: #2e3e2e;
            padding: 16px 20px;
            font-size: 14px;
        }

        QPushButton {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 7px 18px;
            color: #2e5c2e;
            font-size: 13px;
        }

        QPushButton:hover {
            background-color: #d4edda;
            border: 1px solid #a8d8a8;
        }

        QPushButton:pressed {
            background-color: #c8e6c8;
        }

        QPushButton:disabled {
            background-color: #f0f5f0;
            color: #a0c0a0;
            border: 1px solid #d8e8d8;
        }

        QTableView {
            background-color: #ffffff;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            gridline-color: #e8f5e8;
            selection-background-color: #d4edda;
            selection-color: #1a4a1a;
            alternate-background-color: #f5faf5;
        }

        QTableView::item {
            padding: 6px 10px;
        }

        QHeaderView::section {
            background-color: #eaf4ea;
            color: #2e5c2e;
            border: none;
            border-bottom: 1px solid #c8e6c8;
            border-right: 1px solid #d8e8d8;
            padding: 8px 10px;
            font-weight: bold;
            font-size: 12px;
        }

        QScrollBar:vertical {
            background-color: transparent;
            width: 8px;
            margin: 0;
        }

        QScrollBar::handle:vertical {
            background-color: #c8e6c8;
            border-radius: 4px;
            min-height: 30px;
        }

        QScrollBar::handle:vertical:hover {
            background-color: #a8d8a8;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }

        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }

        QScrollBar:horizontal {
            background-color: transparent;
            height: 8px;
            margin: 0;
        }

        QScrollBar::handle:horizontal {
            background-color: #c8e6c8;
            border-radius: 4px;
            min-width: 30px;
        }

        QScrollBar::handle:horizontal:hover {
            background-color: #a8d8a8;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }

        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }

        QMessageBox {
            background-color: #f5f9f5;
        }

        QMessageBox QLabel {
            color: #2e5c2e;
        }

        QInputDialog {
            background-color: #f5f9f5;
        }

        QDialog {
            background-color: #f5f9f5;
        }
    )";
}

inline QString getPanelLabelStyle() {
    return "color: #3a7a3a; font-weight: bold; font-size: 13px; padding: 8px 4px 4px 4px; letter-spacing: 1px;";
}

inline QString getPanelStyle() {
    return "background-color: #f5f9f5; border-right: 1px solid #c8e6c8;";
}

inline QString getEditorToolBarStyle() {
    return R"(
        QToolBar {
            background-color: #f5f9f5;
            border-bottom: 1px solid #c8e6c8;
            border-top: none;
            border-left: none;
            border-right: none;
            padding: 3px 6px;
            spacing: 4px;
        }

        QToolBar QToolButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 4px 10px;
            color: #5a8a5a;
            font-size: 12px;
        }

        QToolBar QToolButton:hover {
            background-color: #d4edda;
            border: 1px solid #b8dab8;
        }

        QToolBar QToolButton:pressed {
            background-color: #c8e6c8;
        }

        QToolBar QToolButton:checked {
            background-color: #c8e6c8;
            color: #2e5c2e;
            font-weight: bold;
            border: 1px solid #a8d8a8;
        }

        QToolBar::separator {
            width: 1px;
            background: #c8e6c8;
            margin: 4px 6px;
        }
    )";
}

inline QString getTrashDialogStyle() {
    return R"(
        QDialog {
            background-color: #f5f9f5;
        }

        QTableView {
            background-color: #ffffff;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            gridline-color: #e8f5e8;
            selection-background-color: #d4edda;
            selection-color: #1a4a1a;
            alternate-background-color: #f5faf5;
        }

        QHeaderView::section {
            background-color: #eaf4ea;
            color: #2e5c2e;
            border: none;
            border-bottom: 1px solid #c8e6c8;
            padding: 8px 10px;
            font-weight: bold;
        }

        QPushButton {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 7px 18px;
            color: #2e5c2e;
        }

        QPushButton:hover {
            background-color: #d4edda;
            border: 1px solid #a8d8a8;
        }

        QPushButton:pressed {
            background-color: #c8e6c8;
        }

        QPushButton:disabled {
            background-color: #f0f5f0;
            color: #a0c0a0;
            border: 1px solid #d8e8d8;
        }
    )";
}

#endif // THEMESTYLESHEET_H
