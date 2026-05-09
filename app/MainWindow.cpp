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
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QSslError>
#include <QStandardPaths>
#include <QTableView>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
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
#define APP_VERSION "1.0.4"
#endif

static const char* kReleaseApiUrl = "https://api.github.com/repos/Msg-Lbo/port-probe/releases/latest";
static const char* kDefaultManifestUrl = "https://gitee.com/msglbo/port-probe/raw/main/update/latest.json";
static const char* kChangelogUrl = "https://gitee.com/msglbo/port-probe/raw/main/CHANGELOG.md";
static constexpr int kUpdateCheckTimeoutMs = 12000;
static constexpr int kChangelogTimeoutMs = 8000;

struct UpdateInfo {
    QString version;
    QString releaseUrl;
    QString downloadUrl;
    QString changelog;
    QString changelogUrl;
};

static QString currentVersion()
{
    return QStringLiteral(APP_VERSION);
}

static QString withVersionPrefix(const QString& version)
{
    const auto v = version.trimmed();
    return v.startsWith('v', Qt::CaseInsensitive) ? v : QStringLiteral("v") + v;
}

static QString jsonString(const QJsonObject& obj, const QStringList& keys)
{
    for (const auto& key : keys) {
        const auto value = obj.value(key);
        if (value.isString()) {
            const auto text = value.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

static QString jsonChangelog(const QJsonObject& obj)
{
    const auto changelog = obj.value(QStringLiteral("changelog"));
    if (changelog.isString()) {
        return changelog.toString().trimmed();
    }
    if (changelog.isArray()) {
        QStringList lines;
        const auto items = changelog.toArray();
        for (const auto& item : items) {
            if (item.isString() && !item.toString().trimmed().isEmpty()) {
                lines << QStringLiteral("- %1").arg(item.toString().trimmed());
            }
        }
        return lines.join('\n').trimmed();
    }
    return jsonString(obj, { QStringLiteral("body"), QStringLiteral("notes"), QStringLiteral("release_notes") });
}

static QString resolveUpdateUrl(const QString& baseUrl, const QString& value)
{
    const auto text = value.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    QUrl url(text);
    if (url.isRelative() && !baseUrl.isEmpty()) {
        url = QUrl(baseUrl).resolved(url);
    }
    return url.toString();
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

static bool isInstallerUrl(const QString& urlText)
{
    const QUrl url(urlText);
    return url.isValid() && url.path().endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
}

static UpdateInfo githubReleaseInfo(const QJsonObject& obj)
{
    UpdateInfo info;
    info.version = obj.value(QStringLiteral("tag_name")).toString().trimmed();
    info.releaseUrl = obj.value(QStringLiteral("html_url")).toString().trimmed();
    info.downloadUrl = bestDownloadUrl(obj);
    info.changelog = obj.value(QStringLiteral("body")).toString().trimmed();
    info.changelogUrl = QString::fromLatin1(kChangelogUrl);
    return info;
}

static UpdateInfo manifestInfo(const QJsonObject& obj, const QString& manifestUrl)
{
    UpdateInfo info;
    info.version = jsonString(obj, { QStringLiteral("version"), QStringLiteral("tag_name") });
    info.releaseUrl = resolveUpdateUrl(manifestUrl, jsonString(obj, { QStringLiteral("release_url"), QStringLiteral("html_url") }));
    info.downloadUrl = resolveUpdateUrl(manifestUrl, jsonString(obj, {
        QStringLiteral("download_url"),
        QStringLiteral("installer_url"),
        QStringLiteral("url")
    }));
    info.changelog = jsonChangelog(obj);
    info.changelogUrl = resolveUpdateUrl(manifestUrl, jsonString(obj, { QStringLiteral("changelog_url") }));
    return info;
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

QString MainWindow::configuredUpdateManifestUrl() const
{
    const auto envUrl = qEnvironmentVariable("PROBE_TOOL_UPDATE_MANIFEST_URL").trimmed();
    const auto defaultUrl = envUrl.isEmpty() ? QString::fromLatin1(kDefaultManifestUrl) : envUrl;
    QSettings settings(QApplication::applicationDirPath() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("update"));
    const auto url = settings.value(QStringLiteral("manifest_url"), defaultUrl).toString().trimmed();
    settings.endGroup();
    return url;
}

bool MainWindow::ignoreUpdateSslErrors() const
{
    const auto envValue = qEnvironmentVariable("PROBE_TOOL_IGNORE_SSL_ERRORS").trimmed().toLower();
    const bool envEnabled = envValue == QStringLiteral("1")
        || envValue == QStringLiteral("true")
        || envValue == QStringLiteral("yes");

    QSettings settings(QApplication::applicationDirPath() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("update"));
    const bool enabled = settings.value(QStringLiteral("ignore_ssl_errors"), envEnabled).toBool();
    settings.endGroup();
    return enabled;
}

QNetworkReply* MainWindow::sendUpdateRequest(QNetworkRequest request, int timeoutMs)
{
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!request.hasRawHeader("User-Agent")) {
        request.setRawHeader("User-Agent", "ProbeTool");
    }
    request.setRawHeader("Cache-Control", "no-cache");

    auto* reply = _updateManager->get(request);
    if (ignoreUpdateSslErrors()) {
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>&) {
            reply->ignoreSslErrors();
        });
    }
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, reply, [reply]() {
            if (reply->isRunning()) {
                reply->abort();
            }
        });
    }
    return reply;
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
    _updateButton = new QPushButton(QStringLiteral("下载并更新"), _updateDialog);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), _updateDialog);
    buttons->addWidget(_checkUpdateButton);
    buttons->addStretch(1);
    buttons->addWidget(_updateButton);
    buttons->addWidget(closeButton);

    layout->addWidget(_updateInfoLabel);
    layout->addWidget(_updateLog, 1);
    layout->addLayout(buttons);

    connect(_checkUpdateButton, &QPushButton::clicked, this, [this]() { checkForUpdates(true); });
    connect(_updateButton, &QPushButton::clicked, this, &MainWindow::startUpdateDownload);
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

    const QString manifestUrl = configuredUpdateManifestUrl();
    const bool useManifest = !manifestUrl.isEmpty();
    QNetworkRequest req{ QUrl(useManifest ? manifestUrl : QString::fromLatin1(kReleaseApiUrl)) };
    req.setRawHeader("Accept", useManifest ? "application/json" : "application/vnd.github+json");
    auto* reply = sendUpdateRequest(req, kUpdateCheckTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated]() {
        const QString requestUrl = reply->request().url().toString();
        const bool useManifest = requestUrl != QString::fromLatin1(kReleaseApiUrl);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            _checkingUpdate = false;
            _updateError = QStringLiteral("检测更新失败：%1").arg(reply->errorString());
            if (!useManifest) {
                _updateError += QStringLiteral("\n默认更新源使用 Gitee。若现场网络仍不可达，请在 config.ini 的 [update] 里配置 manifest_url，指向内网更新清单。");
            }
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
        const auto payload = reply->readAll();
        const auto doc = QJsonDocument::fromJson(payload, &parseError);
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
        const UpdateInfo info = useManifest ? manifestInfo(obj, requestUrl) : githubReleaseInfo(obj);
        if (info.version.isEmpty()) {
            _checkingUpdate = false;
            _updateError = QStringLiteral("检测更新失败：更新清单缺少版本号");
            _hasUpdate = false;
            setVersionFlash(false);
            applyUpdateState();
            refreshUpdateDialog();
            return;
        }

        _latestVersion = info.version;
        _latestReleaseUrl = info.releaseUrl;
        _latestReleaseBody = info.changelog.trimmed();
        _latestDownloadUrl = info.downloadUrl;
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

        _checkingUpdate = false;
        _latestChangelog = _latestReleaseBody.trimmed();
        if (_latestChangelog.isEmpty()) {
            _latestChangelog = QStringLiteral("正在获取更新日志...");
        }
        _updateError.clear();
        setVersionFlash(true);
        applyUpdateState();
        refreshUpdateDialog();

        const QString changelogUrl = info.changelogUrl;
        if (changelogUrl.isEmpty()) {
            if (_latestChangelog == QStringLiteral("正在获取更新日志...")) {
                _latestChangelog = QStringLiteral("暂无更新日志。");
                refreshUpdateDialog();
            }
            return;
        }

        QNetworkRequest changelogReq{ QUrl(changelogUrl) };
        auto* changelogReply = sendUpdateRequest(changelogReq, kChangelogTimeoutMs);
        connect(changelogReply, &QNetworkReply::finished, this, [this, changelogReply]() {
            changelogReply->deleteLater();
            if (changelogReply->error() == QNetworkReply::NoError) {
                const auto changelogText = QString::fromUtf8(changelogReply->readAll());
                _latestChangelog = changelogForVersion(changelogText, _latestVersion);
            }
            if (_latestChangelog.trimmed().isEmpty()) {
                _latestChangelog = _latestReleaseBody.trimmed();
            }
            if (_latestChangelog == QStringLiteral("正在获取更新日志...")) {
                _latestChangelog.clear();
            }
            if (_latestChangelog.trimmed().isEmpty()) {
                _latestChangelog = QStringLiteral("暂无更新日志。");
            }
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
        _versionLabel->setToolTip(QStringLiteral("正在检测更新"));
    } else if (_hasUpdate) {
        text += QStringLiteral("  有新版本");
        _versionLabel->setToolTip(_latestVersion.isEmpty()
            ? QStringLiteral("发现新版本，点击查看更新")
            : QStringLiteral("发现新版本 %1，点击查看更新").arg(withVersionPrefix(_latestVersion)));
    } else {
        _versionLabel->setToolTip(QStringLiteral("查看版本与更新"));
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
        _checkUpdateButton->setEnabled(!_checkingUpdate && !_updateDownloadReply);
        _checkUpdateButton->setText(_checkingUpdate ? QStringLiteral("检测中...") : QStringLiteral("检测更新"));
    }
    if (_updateButton) {
        const bool installerReady = isInstallerUrl(_latestDownloadUrl);
        _updateButton->setText(installerReady ? QStringLiteral("下载并更新") : QStringLiteral("打开下载页"));
        if (_updateDownloadReply) {
            _updateButton->setText(QStringLiteral("下载中..."));
        }
        _updateButton->setEnabled(_hasUpdate
            && !_checkingUpdate
            && !_updateDownloadReply
            && (!_latestDownloadUrl.isEmpty() || !_latestReleaseUrl.isEmpty()));
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

void MainWindow::startUpdateDownload()
{
    if (_updateDownloadReply) {
        return;
    }

    if (!isInstallerUrl(_latestDownloadUrl)) {
        openLatestRelease();
        return;
    }

    const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("更新"), QStringLiteral("无法找到临时目录，不能下载更新。"));
        return;
    }

    QDir().mkpath(tempDir);
    const QString fileName = QStringLiteral("ProbeTool_Setup_x64_%1.exe").arg(withVersionPrefix(_latestVersion));
    _updateDownloadPath = QDir(tempDir).filePath(fileName);
    if (QFile::exists(_updateDownloadPath)) {
        QFile::remove(_updateDownloadPath);
    }

    _updateDownloadFile = new QFile(_updateDownloadPath, this);
    if (!_updateDownloadFile->open(QIODevice::WriteOnly)) {
        const auto errorText = _updateDownloadFile->errorString();
        _updateDownloadFile->deleteLater();
        _updateDownloadFile = nullptr;
        QMessageBox::warning(this, QStringLiteral("更新"), QStringLiteral("无法写入更新文件：%1").arg(errorText));
        return;
    }

    _updateProgressDialog = new QProgressDialog(QStringLiteral("正在下载更新..."), QStringLiteral("取消"), 0, 100, this);
    _updateProgressDialog->setWindowTitle(QStringLiteral("在线更新"));
    _updateProgressDialog->setWindowModality(Qt::ApplicationModal);
    _updateProgressDialog->setMinimumDuration(0);
    _updateProgressDialog->setValue(0);

    QNetworkRequest req{ QUrl(_latestDownloadUrl) };
    _updateDownloadReply = sendUpdateRequest(req);
    connect(_updateProgressDialog, &QProgressDialog::canceled, this, [this]() {
        if (_updateDownloadReply) {
            _updateDownloadReply->abort();
        }
    });
    connect(_updateDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (_updateDownloadReply && _updateDownloadFile) {
            _updateDownloadFile->write(_updateDownloadReply->readAll());
        }
    });
    connect(_updateDownloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!_updateProgressDialog) {
            return;
        }
        if (total > 0) {
            _updateProgressDialog->setRange(0, 100);
            _updateProgressDialog->setValue(static_cast<int>((received * 100) / total));
            _updateProgressDialog->setLabelText(QStringLiteral("正在下载更新：%1 / %2 MB")
                .arg(QString::number(received / 1024.0 / 1024.0, 'f', 1))
                .arg(QString::number(total / 1024.0 / 1024.0, 'f', 1)));
        } else {
            _updateProgressDialog->setRange(0, 0);
            _updateProgressDialog->setLabelText(QStringLiteral("正在下载更新..."));
        }
    });
    connect(_updateDownloadReply, &QNetworkReply::finished, this, [this]() {
        auto* reply = _updateDownloadReply;
        _updateDownloadReply = nullptr;
        if (reply && _updateDownloadFile) {
            _updateDownloadFile->write(reply->readAll());
        }
        if (_updateDownloadFile) {
            _updateDownloadFile->close();
            _updateDownloadFile->deleteLater();
            _updateDownloadFile = nullptr;
        }

        const bool ok = reply && reply->error() == QNetworkReply::NoError;
        const auto errorText = reply ? reply->errorString() : QStringLiteral("未知错误");
        if (reply) {
            reply->deleteLater();
        }
        if (_updateProgressDialog) {
            _updateProgressDialog->close();
            _updateProgressDialog->deleteLater();
            _updateProgressDialog = nullptr;
        }
        refreshUpdateDialog();

        if (!ok) {
            QFile::remove(_updateDownloadPath);
            QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("下载更新失败：%1").arg(errorText));
            return;
        }

        if (QFileInfo(_updateDownloadPath).size() <= 0) {
            QFile::remove(_updateDownloadPath);
            QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("下载的更新文件为空。"));
            return;
        }

        launchInstallerAndRestart(_updateDownloadPath);
    });

    refreshUpdateDialog();
}

void MainWindow::launchInstallerAndRestart(const QString& installerPath)
{
    const QString appPath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const QString nativeInstaller = QDir::toNativeSeparators(installerPath);
    const QString scriptPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("ProbeTool_Update_%1.cmd").arg(QCoreApplication::applicationPid()));

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("无法创建更新脚本：%1").arg(script.errorString()));
        return;
    }

    QTextStream out(&script);
    out.setCodec("GBK");
    out << "@echo off\r\n";
    out << "setlocal\r\n";
    out << "set \"SETUP=" << nativeInstaller << "\"\r\n";
    out << "set \"APP=" << appPath << "\"\r\n";
    out << "set \"PID=" << QCoreApplication::applicationPid() << "\"\r\n";
    out << ":wait_app\r\n";
    out << "tasklist /FI \"PID eq %PID%\" | find \"%PID%\" >nul\r\n";
    out << "if not errorlevel 1 (\r\n";
    out << "  ping 127.0.0.1 -n 2 >nul\r\n";
    out << "  goto wait_app\r\n";
    out << ")\r\n";
    out << "\"%SETUP%\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-\r\n";
    out << "start \"\" \"%APP%\"\r\n";
    out << "del \"%SETUP%\" >nul 2>nul\r\n";
    out << "del \"%~f0\" >nul 2>nul\r\n";
    script.close();

    if (!QProcess::startDetached(QStringLiteral("cmd.exe"), { QStringLiteral("/C"), QDir::toNativeSeparators(scriptPath) })) {
        QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("无法启动更新安装程序。"));
        return;
    }

    QApplication::quit();
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
