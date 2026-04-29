#include "app/ConfigDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace pp {

ConfigDialog::ConfigDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("探测配置"));

    _sourcePort = new QSpinBox(this);
    _sourcePort->setRange(1, 65535);
    _destPort = new QSpinBox(this);
    _destPort->setRange(1, 65535);
    _destIp = new QLineEdit(this);
    _timeout = new QSpinBox(this);
    _timeout->setRange(1, 60);
    _broadcastHex = new QTextEdit(this);
    _broadcastHex->setAcceptRichText(false);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("源端口"), _sourcePort);
    form->addRow(QStringLiteral("目标端口"), _destPort);
    form->addRow(QStringLiteral("目标 IP"), _destIp);
    form->addRow(QStringLiteral("超时（秒）"), _timeout);
    form->addRow(QStringLiteral("广播数据(Hex)"), _broadcastHex);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    setLayout(layout);
    resize(420, 360);
}

void ConfigDialog::setConfig(const SearchConfig& cfg)
{
    _sourcePort->setValue(cfg.sourcePort);
    _destPort->setValue(cfg.destPort);
    _destIp->setText(cfg.destIp);
    _timeout->setValue(cfg.timeoutSeconds);
    _broadcastHex->setPlainText(cfg.broadcastDataHex);
}

SearchConfig ConfigDialog::config() const
{
    SearchConfig cfg;
    cfg.sourcePort = _sourcePort->value();
    cfg.destPort = _destPort->value();
    cfg.destIp = _destIp->text().trimmed();
    cfg.timeoutSeconds = _timeout->value();
    cfg.broadcastDataHex = _broadcastHex->toPlainText().trimmed().remove(' ').remove('\n').remove('\r').remove('\t');
    return cfg;
}

} // namespace pp

