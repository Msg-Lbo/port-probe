#include "app/MainWindow.h"

#include "app/ConfigDialog.h"
#include "app/DeviceTableModel.h"
#include "app/QrCodeDialog.h"
#include "core/Admin.h"
#include "core/DeviceParser.h"
#include "core/NicService.h"
#include "core/SettingsStore.h"
#include "core/UdpScanner.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QPushButton>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QHBoxLayout>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyleOptionButton>
#include <functional>

namespace pp {

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

static const char* kReleaseApiUrl = "https://api.github.com/repos/Msg-Lbo/port-probe/releases/latest";
static const char* kChangelogUrl = "https://raw.githubusercontent.com/Msg-Lbo/port-probe/main/CHANGELOG.md";

static QString currentVersion()
{
    return QStringLiteral(APP_VERSION);
}

static QString withVersionPrefix(const QString& version)
{
    const auto v = version.trimmed();
    return v.startsWith('v', Qt::CaseInsensitive) ? v : QStringLiteral("v") + v;
}

static QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith('v', Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    const auto dash = version.indexOf('-');
    if (dash >= 0) {
        version = version.left(dash);
    }
    return version;
}

static QVector<int> versionParts(const QString& version)
{
    QVector<int> out;
    const auto parts = normalizedVersion(version).split('.', Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        bool ok = false;
        out.push_back(part.toInt(&ok));
        if (!ok) {
            out.last() = 0;
        }
    }
    while (out.size() < 3) {
        out.push_back(0);
    }
    return out;
}

static int compareVersions(const QString& a, const QString& b)
{
    const auto av = versionParts(a);
    const auto bv = versionParts(b);
    const int count = qMax(av.size(), bv.size());
    for (int i = 0; i < count; ++i) {
        const int ai = i < av.size() ? av.at(i) : 0;
        const int bi = i < bv.size() ? bv.at(i) : 0;
        if (ai != bi) {
            return ai < bi ? -1 : 1;
        }
    }
    return 0;
}

static QString readBundledChangelog()
{
    QFile file(QStringLiteral(":/docs/CHANGELOG.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

static QString changelogForVersion(const QString& changelog, const QString& version)
{
    const QString wanted = normalizedVersion(version);
    const auto lines = changelog.split('\n');
    const QRegularExpression heading(QStringLiteral("^##\\s+v?([^\\s(]+).*"));
    QStringList out;
    bool collecting = false;

    for (const auto& rawLine : lines) {
        const auto line = rawLine.trimmed();
        const auto match = heading.match(line);
        if (match.hasMatch()) {
            if (collecting) {
                break;
            }
            collecting = (normalizedVersion(match.captured(1)) == wanted);
            continue;
        }
        if (collecting) {
            out << rawLine;
        }
    }

    return out.join('\n').trimmed();
}

static QString bestDownloadUrl(const QJsonObject& release)
{
    const auto assets = release.value(QStringLiteral("assets")).toArray();
    QString fallback;
    for (const auto& item : assets) {
        const auto asset = item.toObject();
        const auto name = asset.value(QStringLiteral("name")).toString();
        const auto url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = url;
        }
        if (name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive) && name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            return url;
        }
        if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            fallback = url;
        }
    }
    return fallback;
}

static QString buildQrPayload(const QVector<DeviceInfo>& devices)
{
    QStringList sns;
    for (const auto& d : devices) {
        const auto sn = d.sn.trimmed();
        if (!sn.isEmpty()) {
            sns << sn;
        }
    }

    if (sns.size() <= 1) {
        return sns.isEmpty() ? QString{} : sns.first();
    }

    QString payload = QStringLiteral("deviceSns");
    for (const auto& sn : sns) {
        payload += sn + QStringLiteral(",");
    }
    return payload;
}

class ClickableLabel : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> onClicked;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QLabel::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && onClicked) {
            onClicked();
        }
    }
};

class CenteredCheckDelegate : public QStyledItemDelegate {
public:
    explicit CenteredCheckDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.column() != DeviceTableModel::Selected) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, option.palette.highlight());
        } else {
            painter->fillRect(option.rect, option.palette.base());
        }
        QStyleOptionButton check;
        check.state = QStyle::State_Enabled;
        const Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
        if (state == Qt::Checked) {
            check.state |= QStyle::State_On;
        } else {
            check.state |= QStyle::State_Off;
        }
        const int indicatorW = option.widget->style()->pixelMetric(QStyle::PM_IndicatorWidth);
        const int indicatorH = option.widget->style()->pixelMetric(QStyle::PM_IndicatorHeight);
        check.rect = QRect(
            option.rect.x() + (option.rect.width() - indicatorW) / 2,
            option.rect.y() + (option.rect.height() - indicatorH) / 2,
            indicatorW,
            indicatorH);
        option.widget->style()->drawControl(QStyle::CE_CheckBox, &check, painter, option.widget);
        painter->restore();
    }
};

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("探测工具"));
    resize(1180, 780);

    auto* central = new QWidget(this);
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(10);

    auto* topBar = new QWidget(central);
    topBar->setObjectName("topBarCard");
    auto* top = new QHBoxLayout(topBar);
    top->setContentsMargins(10, 10, 10, 10);
    top->setSpacing(8);

    _ifaceCombo = new QComboBox(topBar);
    _ifaceCombo->setMinimumWidth(360);
    _refreshBtn = new QPushButton(QStringLiteral("刷新接口"), topBar);
    _detectBtn = new QPushButton(QStringLiteral("探测"), topBar);
    _configBtn = new QPushButton(QStringLiteral("探测配置"), topBar);
    _qrBtn = new QPushButton(QStringLiteral("生成二维码"), topBar);
    _refreshBtn->setMinimumWidth(88);
    _detectBtn->setMinimumWidth(72);
    _qrBtn->setMinimumWidth(108);
    _configBtn->setMinimumWidth(108);

    top->addWidget(_ifaceCombo);
    top->addWidget(_refreshBtn);
    top->addWidget(_detectBtn);
    top->addWidget(_qrBtn);
    top->addStretch(1);
    top->addWidget(_configBtn);
    topBar->setLayout(top);

    _status = new QLabel(QStringLiteral(""), central);
    _status->setObjectName("statusCard");
    _status->setMinimumHeight(22);

    _model = new DeviceTableModel(this);
    _detectWatcher = new QFutureWatcher<SearchResult>(this);
    _updateManager = new QNetworkAccessManager(this);
    _versionBlinkTimer = new QTimer(this);
    _versionBlinkTimer->setInterval(560);
    connect(_versionBlinkTimer, &QTimer::timeout, this, [this]() {
        _versionBlinkOn = !_versionBlinkOn;
        applyUpdateState();
    });

    _table = new QTableView(central);
    _table->setModel(_model);
    _table->setSelectionBehavior(QAbstractItemView::SelectItems);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setItemDelegateForColumn(DeviceTableModel::Selected, new CenteredCheckDelegate(_table));
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setDefaultSectionSize(160);
    _table->setColumnWidth(DeviceTableModel::Selected, 56);
    _table->verticalHeader()->setVisible(false);
    _table->verticalHeader()->setDefaultSectionSize(32);
    _table->setContextMenuPolicy(Qt::CustomContextMenu);
    _headerSelectAll = new QCheckBox(_table->horizontalHeader());
    _headerSelectAll->setTristate(true);
    _headerSelectAll->setToolTip(QStringLiteral("全选/取消全选"));
    _headerSelectAll->setCheckState(Qt::Unchecked);
    _headerSelectAll->setFocusPolicy(Qt::NoFocus);
    _headerSelectAll->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto positionHeaderCheckbox = [this]() {
        auto* header = _table->horizontalHeader();
        const int section = DeviceTableModel::Selected;
        const int x = header->sectionViewportPosition(section);
        const int w = _table->columnWidth(section);
        const int h = header->height();
        const int box = 18;
        _headerSelectAll->setGeometry(x + (w - box) / 2, (h - box) / 2, box, box);
        _headerSelectAll->raise();
    };
    positionHeaderCheckbox();
    connect(_table->horizontalHeader(), &QHeaderView::sectionResized, this, [positionHeaderCheckbox](int, int, int) { positionHeaderCheckbox(); });
    connect(_table->horizontalHeader(), &QHeaderView::geometriesChanged, this, [positionHeaderCheckbox]() { positionHeaderCheckbox(); });
    connect(_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [positionHeaderCheckbox](int, int, int) { positionHeaderCheckbox(); });
    connect(_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section != DeviceTableModel::Selected) {
            return;
        }
        _model->setAllSelected(!_model->allSelected());
    });

    connect(_table, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("复制当前单元格"), this, &MainWindow::copyCell);
        menu.addAction(QStringLiteral("复制当前行"), this, &MainWindow::copyRow);
        menu.exec(_table->viewport()->mapToGlobal(pos));
    });
    connect(_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || index.column() != DeviceTableModel::Selected) {
            return;
        }
        const bool checked = (_model->data(index, Qt::CheckStateRole).toInt() == Qt::Checked);
        _model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    });

    v->addWidget(topBar);
    v->addWidget(_status);
    v->addWidget(_table, 1);

    auto* bottomBar = new QWidget(central);
    auto* bottom = new QHBoxLayout(bottomBar);
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(0);
    auto* version = new ClickableLabel(withVersionPrefix(currentVersion()), bottomBar);
    version->setObjectName("versionLabel");
    version->setCursor(Qt::PointingHandCursor);
    version->setToolTip(QStringLiteral("查看版本与更新"));
    version->setMinimumHeight(18);
    version->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    version->onClicked = [this]() { openUpdateDialog(); };
    _versionLabel = version;
    bottom->addWidget(_versionLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    bottom->addStretch(1);
    bottomBar->setLayout(bottom);
    v->addWidget(bottomBar);

    central->setLayout(v);
    setCentralWidget(central);

    connect(_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshInterfaces);
    connect(_detectBtn, &QPushButton::clicked, this, &MainWindow::detect);
    connect(_configBtn, &QPushButton::clicked, this, &MainWindow::openConfig);
    connect(_qrBtn, &QPushButton::clicked, this, &MainWindow::generateQr);
    connect(_model, &QAbstractItemModel::dataChanged, this, [this]() { syncCornerSelectState(); });
    connect(_model, &QAbstractItemModel::modelReset, this, [this]() { syncCornerSelectState(); });
    connect(_detectWatcher, &QFutureWatcher<SearchResult>::finished, this, [this]() {
        const auto result = _detectWatcher->result();
        if (!result.success) {
            setBusy(false, QString{});
            QMessageBox::critical(this, QStringLiteral("探测失败"), result.error);
            return;
        }

        DeviceParser parser;
        const auto devices = parser.parseDevices(result.responses);
        _model->setDevices(devices);
        syncCornerSelectState();
        const int respCount = result.responses.size();
        const int devCount = devices.size();
        setBusy(false, QStringLiteral("设备数量：%1 台（回包 %2，解析 %3）").arg(devCount).arg(respCount).arg(devCount));

        if (respCount == 0) {
            QMessageBox::information(
                this,
                QStringLiteral("未收到回包"),
                QStringLiteral("未收到任何设备回包。\n\n请确认：\n1) 以管理员身份运行\n2) 网卡选择正确\n3) 端口配置与旧版一致（源36368/目标36369）\n4) 防火墙未拦截"));
        } else if (devCount == 0) {
            QString previewText;
            if (!result.responses.isEmpty()) {
                const auto& first = result.responses.first();
                previewText = QStringLiteral("\n\n首条回包预览(text):\n%1\n\n首条回包预览(hex):\n%2")
                    .arg(first.dataText.left(220))
                    .arg(first.dataHex.left(220));
            }
            QMessageBox::information(
                this,
                QStringLiteral("回包已收到但未解析"),
                QStringLiteral("已收到设备回包，但未解析出设备字段。") + previewText);
        }
    });

    if (!isRunningAsAdmin()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("探测需要管理员权限（Raw Socket 抓包）。请以管理员身份运行。"));
    }

    // load config.ini next to exe
    const auto iniPath = QApplication::applicationDirPath() + "/config.ini";
    SettingsStore store(iniPath);
    _cfg = store.load();

    refreshInterfaces();
    applyUpdateState();
    QTimer::singleShot(1200, this, [this]() { checkForUpdates(false); });
}

void MainWindow::setBusy(bool busy, const QString& text)
{
    _refreshBtn->setEnabled(!busy);
    _detectBtn->setEnabled(!busy);
    _ifaceCombo->setEnabled(!busy);
    _configBtn->setEnabled(!busy);
    _qrBtn->setEnabled(!busy);
    _detectBtn->setText(busy ? QStringLiteral("探测中...") : QStringLiteral("探测"));
    _status->setText(text);
}

void MainWindow::syncCornerSelectState()
{
    if (!_headerSelectAll) {
        return;
    }

    const int total = _model->devices().size();
    const int selected = _model->selectedDevices().size();

    _headerSelectAll->blockSignals(true);
    if (total <= 0) {
        _headerSelectAll->setEnabled(false);
        _headerSelectAll->setCheckState(Qt::Unchecked);
    } else {
        _headerSelectAll->setEnabled(true);
        if (selected <= 0) {
            _headerSelectAll->setCheckState(Qt::Unchecked);
        } else if (selected >= total) {
            _headerSelectAll->setCheckState(Qt::Checked);
        } else {
            _headerSelectAll->setCheckState(Qt::PartiallyChecked);
        }
    }
    _headerSelectAll->blockSignals(false);
}

void MainWindow::rebuildInterfaceOptions(const QVector<InterfaceOption>& list)
{
    _ifaceCombo->clear();
    for (const auto& it : list) {
        _ifaceCombo->addItem(it.name, it.ip);
    }
}

QString MainWindow::currentInterfaceIp() const
{
    return _ifaceCombo->currentData().toString();
}

void MainWindow::refreshInterfaces()
{
    NicService nic;
    const auto list = nic.listIPv4();
    rebuildInterfaceOptions(list);
    if (_ifaceCombo->count() > 0) {
        _ifaceCombo->setCurrentIndex(0);
    }
}

void MainWindow::detect()
{
    if (_detectWatcher->isRunning()) {
        return;
    }

    const auto ip = currentInterfaceIp();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择网卡"));
        return;
    }

    setBusy(true, QStringLiteral("正在探测设备，请稍候..."));
    const auto cfg = _cfg;
    _detectWatcher->setFuture(QtConcurrent::run([ip, cfg]() {
        NicService nic;
        UdpScanner scanner(nic);
        return scanner.search(ip, cfg);
    }));
}

void MainWindow::openConfig()
{
    ConfigDialog dlg(this);
    dlg.setConfig(_cfg);
    if (dlg.exec() == QDialog::Accepted) {
        _cfg = dlg.config();
        const auto iniPath = QApplication::applicationDirPath() + "/config.ini";
        SettingsStore store(iniPath);
        store.save(_cfg);
    }
}

void MainWindow::generateQr()
{
    const auto selected = _model->selectedDevices();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先勾选至少一台设备"));
        return;
    }

    const QString payload = buildQrPayload(selected);
    if (payload.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("勾选设备缺少有效 SN，无法生成二维码"));
        return;
    }

    QrCodeDialog dlg(payload, this);
    dlg.exec();
}

void MainWindow::copyCell()
{
    const auto idx = _table->currentIndex();
    if (!idx.isValid()) return;
    const auto text = _model->data(idx, Qt::DisplayRole).toString();
    if (text.isEmpty()) return;
    QApplication::clipboard()->setText(text);
}

void MainWindow::copyRow()
{
    const auto idx = _table->currentIndex();
    if (!idx.isValid()) return;
    const int row = idx.row();
    QStringList cols;
    for (int c = 0; c < DeviceTableModel::ColumnCount; ++c) {
        cols << _model->data(_model->index(row, c), Qt::DisplayRole).toString();
    }
    QApplication::clipboard()->setText(cols.join("\t"));
}

void MainWindow::openUpdateDialog()
{
    if (_updateDialog) {
        _updateDialog->raise();
        _updateDialog->activateWindow();
        return;
    }

    _updateDialog = new QDialog(this);
    _updateDialog->setWindowTitle(QStringLiteral("版本更新"));
    _updateDialog->setWindowModality(Qt::ApplicationModal);
    _updateDialog->setAttribute(Qt::WA_DeleteOnClose);
    _updateDialog->resize(520, 420);

    auto* layout = new QVBoxLayout(_updateDialog);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(10);

    _updateInfoLabel = new QLabel(_updateDialog);
    _updateInfoLabel->setWordWrap(true);
    _updateInfoLabel->setMinimumHeight(42);

    _updateLog = new QTextEdit(_updateDialog);
    _updateLog->setReadOnly(true);
    _updateLog->setMinimumHeight(250);
    _updateLog->setAcceptRichText(false);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    _checkUpdateButton = new QPushButton(QStringLiteral("检测更新"), _updateDialog);
    _updateButton = new QPushButton(QStringLiteral("更新"), _updateDialog);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), _updateDialog);
    buttons->addWidget(_checkUpdateButton);
    buttons->addStretch(1);
    buttons->addWidget(_updateButton);
    buttons->addWidget(closeButton);

    layout->addWidget(_updateInfoLabel);
    layout->addWidget(_updateLog, 1);
    layout->addLayout(buttons);

    connect(_checkUpdateButton, &QPushButton::clicked, this, [this]() { checkForUpdates(true); });
    connect(_updateButton, &QPushButton::clicked, this, &MainWindow::openLatestRelease);
    connect(closeButton, &QPushButton::clicked, _updateDialog, &QDialog::close);
    connect(_updateDialog, &QObject::destroyed, this, [this]() {
        _updateDialog = nullptr;
        _updateInfoLabel = nullptr;
        _updateLog = nullptr;
        _updateButton = nullptr;
        _checkUpdateButton = nullptr;
    });

    refreshUpdateDialog();
    _updateDialog->show();
}

void MainWindow::checkForUpdates()
{
    checkForUpdates(true);
}

void MainWindow::checkForUpdates(bool userInitiated)
{
    if (_checkingUpdate || !_updateManager) {
        refreshUpdateDialog();
        return;
    }

    _checkingUpdate = true;
    _updateError.clear();
    _latestChangelog.clear();
    applyUpdateState();
    refreshUpdateDialog();

    QNetworkRequest req(QUrl(QString::fromLatin1(kReleaseApiUrl)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "ProbeTool");
    auto* reply = _updateManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            _checkingUpdate = false;
            _updateError = QStringLiteral("检测更新失败：%1").arg(reply->errorString());
            if (_latestVersion.isEmpty()) {
                _hasUpdate = false;
                setVersionFlash(false);
            }
            applyUpdateState();
            refreshUpdateDialog();
            if (userInitiated && !_updateDialog) {
                QMessageBox::warning(this, QStringLiteral("检测更新"), _updateError);
            }
            return;
        }

        QJsonParseError parseError {};
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            _checkingUpdate = false;
            _updateError = QStringLiteral("检测更新失败：版本信息解析失败");
            _hasUpdate = false;
            setVersionFlash(false);
            applyUpdateState();
            refreshUpdateDialog();
            return;
        }

        const auto obj = doc.object();
        _latestVersion = obj.value(QStringLiteral("tag_name")).toString();
        _latestReleaseUrl = obj.value(QStringLiteral("html_url")).toString();
        _latestReleaseBody = obj.value(QStringLiteral("body")).toString().trimmed();
        _latestDownloadUrl = bestDownloadUrl(obj);
        _hasUpdate = compareVersions(_latestVersion, currentVersion()) > 0;

        if (!_hasUpdate) {
            _checkingUpdate = false;
            _latestChangelog = changelogForVersion(readBundledChangelog(), currentVersion());
            _updateError.clear();
            setVersionFlash(false);
            applyUpdateState();
            refreshUpdateDialog();
            return;
        }

        setVersionFlash(true);
        applyUpdateState();
        refreshUpdateDialog();

        QNetworkRequest changelogReq(QUrl(QString::fromLatin1(kChangelogUrl)));
        changelogReq.setRawHeader("User-Agent", "ProbeTool");
        auto* changelogReply = _updateManager->get(changelogReq);
        connect(changelogReply, &QNetworkReply::finished, this, [this, changelogReply]() {
            changelogReply->deleteLater();
            if (changelogReply->error() == QNetworkReply::NoError) {
                const auto changelogText = QString::fromUtf8(changelogReply->readAll());
                _latestChangelog = changelogForVersion(changelogText, _latestVersion);
            }
            if (_latestChangelog.trimmed().isEmpty()) {
                _latestChangelog = _latestReleaseBody.trimmed();
            }
            if (_latestChangelog.trimmed().isEmpty()) {
                _latestChangelog = QStringLiteral("暂无更新日志。");
            }
            _checkingUpdate = false;
            _updateError.clear();
            applyUpdateState();
            refreshUpdateDialog();
        });
    });
}

void MainWindow::applyUpdateState()
{
    if (!_versionLabel) {
        return;
    }

    QString text = withVersionPrefix(currentVersion());
    if (_checkingUpdate) {
        text += QStringLiteral("  检测中");
    } else if (_hasUpdate) {
        text += QStringLiteral("  有新版本");
    }
    _versionLabel->setText(text);

    if (_hasUpdate) {
        const QString bg = _versionBlinkOn ? QStringLiteral("#ffcf42") : QStringLiteral("#fff0a8");
        _versionLabel->setStyleSheet(QStringLiteral(
            "QLabel#versionLabel {"
            "background:%1;"
            "color:#5a3b00;"
            "border:1px solid #e1a600;"
            "border-radius:3px;"
            "padding:1px 7px;"
            "font-weight:600;"
            "}"
        ).arg(bg));
        return;
    }

    _versionLabel->setStyleSheet(QStringLiteral(
        "QLabel#versionLabel {"
        "background:transparent;"
        "color:#53657d;"
        "border:1px solid transparent;"
        "border-radius:3px;"
        "padding:1px 7px;"
        "}"
        "QLabel#versionLabel:hover {"
        "background:#eaf1ff;"
        "color:#1d4ed8;"
        "border-color:#c8d8f3;"
        "}"
    ));
}

void MainWindow::refreshUpdateDialog()
{
    if (!_updateDialog) {
        return;
    }

    QString info = QStringLiteral("当前版本：%1").arg(withVersionPrefix(currentVersion()));
    if (_checkingUpdate) {
        info += QStringLiteral("\n正在检测更新...");
    } else if (!_updateError.isEmpty()) {
        info += QStringLiteral("\n%1").arg(_updateError);
    } else if (!_latestVersion.isEmpty()) {
        info += QStringLiteral("\n最新版本：%1").arg(withVersionPrefix(_latestVersion));
        info += _hasUpdate ? QStringLiteral("，发现可用更新。") : QStringLiteral("，当前已是最新版本。");
    } else {
        info += QStringLiteral("\n尚未检测更新。");
    }

    if (_updateInfoLabel) {
        _updateInfoLabel->setText(info);
    }

    QString logText;
    if (_checkingUpdate && _latestChangelog.isEmpty()) {
        logText = QStringLiteral("正在获取版本信息...");
    } else if (_hasUpdate) {
        logText = _latestChangelog.trimmed().isEmpty() ? QStringLiteral("暂无更新日志。") : _latestChangelog;
    } else {
        logText = changelogForVersion(readBundledChangelog(), currentVersion());
        if (logText.trimmed().isEmpty()) {
            logText = QStringLiteral("暂无更新日志。");
        }
    }

    if (_updateLog) {
        _updateLog->setPlainText(logText);
    }
    if (_checkUpdateButton) {
        _checkUpdateButton->setEnabled(!_checkingUpdate);
        _checkUpdateButton->setText(_checkingUpdate ? QStringLiteral("检测中...") : QStringLiteral("检测更新"));
    }
    if (_updateButton) {
        _updateButton->setEnabled(_hasUpdate && (!_latestDownloadUrl.isEmpty() || !_latestReleaseUrl.isEmpty()));
    }
}

void MainWindow::setVersionFlash(bool enabled)
{
    if (!_versionBlinkTimer) {
        return;
    }
    _versionBlinkOn = false;
    if (enabled) {
        if (!_versionBlinkTimer->isActive()) {
            _versionBlinkTimer->start();
        }
    } else {
        _versionBlinkTimer->stop();
    }
}

void MainWindow::openLatestRelease()
{
    const auto url = !_latestDownloadUrl.isEmpty() ? _latestDownloadUrl : _latestReleaseUrl;
    if (url.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("更新"), QStringLiteral("暂无可用下载地址，请先检测更新。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace pp
