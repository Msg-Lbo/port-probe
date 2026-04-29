#include "app/QrCodeDialog.h"

#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace pp {

QrCodeDialog::QrCodeDialog(const QString& payload, QWidget* parent)
    : QDialog(parent), _payload(payload)
{
    setWindowTitle(QStringLiteral("二维码"));
    resize(420, 460);

    _imageLabel = new QLabel(this);
    _imageLabel->setMinimumSize(360, 360);
    _imageLabel->setAlignment(Qt::AlignCenter);
    _imageLabel->setText(QStringLiteral(""));

    _statusLabel = new QLabel(QStringLiteral("二维码加载中，请稍候..."), this);
    _statusLabel->setAlignment(Qt::AlignCenter);

    _loading = new QProgressBar(this);
    _loading->setRange(0, 0);
    _loading->setTextVisible(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_imageLabel, 1);
    layout->addWidget(_statusLabel);
    layout->addWidget(_loading);
    layout->addWidget(buttons);
    setLayout(layout);

    _manager = new QNetworkAccessManager(this);
    QTimer::singleShot(0, this, &QrCodeDialog::startRequest);
}

void QrCodeDialog::startRequest()
{
    // Use HTTP endpoint to avoid TLS backend dependency issues on older systems.
    QUrl url(QStringLiteral("http://api.qrserver.com/v1/create-qr-code/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("size"), QStringLiteral("360x360"));
    query.addQueryItem(QStringLiteral("data"), _payload);
    url.setQuery(query);
    QNetworkRequest req(url);
    _reply = _manager->get(req);
    connect(_reply, &QNetworkReply::finished, this, &QrCodeDialog::onReplyFinished);
}

void QrCodeDialog::onReplyFinished()
{
    if (_reply == nullptr) {
        return;
    }

    if (_reply->error() != QNetworkReply::NoError) {
        _statusLabel->setText(QStringLiteral("二维码生成失败：%1").arg(_reply->errorString()));
        _loading->hide();
        _reply->deleteLater();
        _reply = nullptr;
        return;
    }

    QImage image;
    const auto bytes = _reply->readAll();
    _reply->deleteLater();
    _reply = nullptr;
    if (!image.loadFromData(bytes)) {
        _statusLabel->setText(QStringLiteral("二维码图片解析失败"));
        _loading->hide();
        return;
    }

    _statusLabel->setText(QStringLiteral("已生成"));
    _loading->hide();
    _imageLabel->setPixmap(QPixmap::fromImage(image).scaled(
        _imageLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

} // namespace pp

