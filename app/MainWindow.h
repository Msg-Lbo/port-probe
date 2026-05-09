#pragma once

#include "core/Models.h"

#include <QMainWindow>

class QComboBox;
class QPushButton;
class QLabel;
class QTableView;
class QCheckBox;
class QDialog;
class QNetworkAccessManager;
class QTextEdit;
class QTimer;
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
    void openUpdateDialog();
    void checkForUpdates();

private:
    void setBusy(bool busy, const QString& text = {});
    void rebuildInterfaceOptions(const QVector<InterfaceOption>& list);
    QString currentInterfaceIp() const;
    void syncCornerSelectState();
    void checkForUpdates(bool userInitiated);
    void applyUpdateState();
    void refreshUpdateDialog();
    void setVersionFlash(bool enabled);
    void openLatestRelease();

    QComboBox* _ifaceCombo = nullptr;
    QPushButton* _refreshBtn = nullptr;
    QPushButton* _detectBtn = nullptr;
    QPushButton* _configBtn = nullptr;
    QPushButton* _qrBtn = nullptr;
    QLabel* _status = nullptr;
    QLabel* _versionLabel = nullptr;
    QTableView* _table = nullptr;
    QCheckBox* _headerSelectAll = nullptr;
    DeviceTableModel* _model = nullptr;
    QFutureWatcher<SearchResult>* _detectWatcher = nullptr;
    QNetworkAccessManager* _updateManager = nullptr;
    QTimer* _versionBlinkTimer = nullptr;
    QDialog* _updateDialog = nullptr;
    QLabel* _updateInfoLabel = nullptr;
    QTextEdit* _updateLog = nullptr;
    QPushButton* _updateButton = nullptr;
    QPushButton* _checkUpdateButton = nullptr;

    SearchConfig _cfg;
    QString _latestVersion;
    QString _latestReleaseUrl;
    QString _latestDownloadUrl;
    QString _latestReleaseBody;
    QString _latestChangelog;
    QString _updateError;
    bool _hasUpdate = false;
    bool _checkingUpdate = false;
    bool _versionBlinkOn = false;
};

} // namespace pp
