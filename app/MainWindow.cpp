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
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QHBoxLayout>
#include <QStringList>

namespace pp {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PortProbeQt"));
    resize(1180, 780);

    auto* central = new QWidget(this);
    auto* v = new QVBoxLayout(central);

    auto* topBar = new QWidget(central);
    auto* top = new QHBoxLayout(topBar);
    top->setContentsMargins(0, 0, 0, 0);

    _ifaceCombo = new QComboBox(topBar);
    _ifaceCombo->setMinimumWidth(360);
    _refreshBtn = new QPushButton(QStringLiteral("刷新接口"), topBar);
    _detectBtn = new QPushButton(QStringLiteral("探测"), topBar);
    _configBtn = new QPushButton(QStringLiteral("探测配置"), topBar);
    _qrBtn = new QPushButton(QStringLiteral("生成二维码"), topBar);

    top->addWidget(_ifaceCombo);
    top->addWidget(_refreshBtn);
    top->addWidget(_detectBtn);
    top->addWidget(_qrBtn);
    top->addStretch(1);
    top->addWidget(_configBtn);
    topBar->setLayout(top);

    _status = new QLabel(QStringLiteral(""), central);
    _status->setMinimumHeight(22);

    _model = new DeviceTableModel(this);
    _detectWatcher = new QFutureWatcher<SearchResult>(this);
    _table = new QTableView(central);
    _table->setModel(_model);
    _table->setSelectionBehavior(QAbstractItemView::SelectItems);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
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

    QStringList sns;
    for (const auto& d : selected) {
        if (!d.sn.trimmed().isEmpty()) {
            sns << d.sn.trimmed();
        }
    }
    if (sns.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("勾选设备缺少有效 SN，无法生成二维码"));
        return;
    }

    const QString payload = sns.join(",");
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

} // namespace pp

