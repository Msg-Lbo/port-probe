#pragma once

#include "core/Models.h"

#include <QMainWindow>

class QComboBox;
class QPushButton;
class QLabel;
class QTableView;
class QCheckBox;
template<typename T>
class QFutureWatcher;

namespace pp {

class DeviceTableModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshInterfaces();
    void detect();
    void openConfig();
    void generateQr();
    void copyCell();
    void copyRow();

private:
    void setBusy(bool busy, const QString& text = {});
    void rebuildInterfaceOptions(const QVector<InterfaceOption>& list);
    QString currentInterfaceIp() const;
    void syncCornerSelectState();

    QComboBox* _ifaceCombo = nullptr;
    QPushButton* _refreshBtn = nullptr;
    QPushButton* _detectBtn = nullptr;
    QPushButton* _configBtn = nullptr;
    QPushButton* _qrBtn = nullptr;
    QLabel* _status = nullptr;
    QTableView* _table = nullptr;
    QCheckBox* _headerSelectAll = nullptr;
    DeviceTableModel* _model = nullptr;
    QFutureWatcher<SearchResult>* _detectWatcher = nullptr;

    SearchConfig _cfg;
};

} // namespace pp

