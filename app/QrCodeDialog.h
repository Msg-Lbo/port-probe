#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;

namespace pp {

class QrCodeDialog : public QDialog {
    Q_OBJECT
public:
    explicit QrCodeDialog(const QString& payload, QWidget* parent = nullptr);

private slots:
    void onReplyFinished();

private:
    void startRequest();

    QString _payload;
    QLabel* _imageLabel = nullptr;
    QLabel* _statusLabel = nullptr;
    QProgressBar* _loading = nullptr;
    QNetworkAccessManager* _manager = nullptr;
    QNetworkReply* _reply = nullptr;
};

} // namespace pp

