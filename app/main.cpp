#include "app/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    app.setStyleSheet(R"(
QWidget {
    background: #f3f6fb;
    color: #172033;
}
QMainWindow {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #f8fbff, stop:1 #eef3fb);
}
QMenu {
    background: #ffffff;
    border: 1px solid #d8e2f1;
    border-radius: 4px;
    padding: 6px;
}
QMenu::item {
    padding: 7px 18px;
    border-radius: 6px;
}
QMenu::item:selected {
    background: #e8f0ff;
    color: #0d1b33;
}
QPushButton {
    background: #2f6fed;
    color: #ffffff;
    border: 1px solid #255fd4;
    border-radius: 4px;
    padding: 6px 14px;
    min-height: 32px;
    font-weight: 600;
}
QPushButton:hover {
    background: #3f7cf2;
}
QPushButton:pressed {
    background: #265dcc;
}
QPushButton:disabled {
    background: #bdc8db;
    border-color: #bdc8db;
    color: #edf1f7;
}
QComboBox, QLineEdit, QSpinBox, QTextEdit {
    background: #ffffff;
    border: 1px solid #c9d4e6;
    border-radius: 4px;
    padding: 4px 8px;
    min-height: 32px;
    selection-background-color: #2f6fed;
}
QComboBox {
    padding-right: 32px;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 28px;
    border-left: 1px solid #d7e1f0;
    background: #f4f8ff;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}
QComboBox::down-arrow {
    image: url(:/icons/arrow-down.png);
    width: 10px;
    height: 6px;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QTextEdit:focus {
    border: 1px solid #6a9dff;
}
QComboBox QAbstractItemView {
    background: #ffffff;
    border: 1px solid #d2deee;
    border-radius: 4px;
    padding: 4px;
    selection-background-color: #dbe9ff;
    selection-color: #11213d;
    outline: 0;
}
QTableView {
    background: #ffffff;
    border: 1px solid #d7e1f0;
    border-radius: 4px;
    gridline-color: #edf2fa;
    selection-background-color: #e5efff;
    selection-color: #0e1a31;
}
QHeaderView::section {
    background: #eef4ff;
    color: #2e3f5f;
    border: none;
    border-right: 1px solid #dee7f6;
    border-bottom: 1px solid #dee7f6;
    padding: 8px 6px;
    font-weight: 600;
}
QLabel {
    background: transparent;
}
QProgressBar {
    border: 1px solid #d1ddef;
    border-radius: 4px;
    background: #ffffff;
}
QProgressBar::chunk {
    background: #2f6fed;
    border-radius: 3px;
}
QDialog {
    background: #f4f7fc;
}
#topBarCard {
    background: #ffffff;
    border: 1px solid #d9e3f2;
    border-radius: 6px;
}
#statusCard {
    padding: 7px 10px;
    background: #ffffff;
    border: 1px solid #d9e3f2;
    border-radius: 6px;
    color: #425673;
}
)");
    QIcon appIcon(":/icons/app.ico");
    if (appIcon.isNull()) {
        appIcon = QIcon(":/icons/app.png");
    }
    app.setWindowIcon(appIcon);
    pp::MainWindow w;
    w.setWindowIcon(app.windowIcon());
    w.show();
    return app.exec();
}
