#pragma once

#include <QString>
#include <QVector>

namespace pp {

struct InterfaceOption {
    QString name;
    QString ip;
};

struct SearchConfig {
    int sourcePort = 36368;
    int destPort = 36369;
    QString destIp = QStringLiteral("255.255.255.255");
    int timeoutSeconds = 3;
    QString broadcastDataHex = QStringLiteral("00037b226d6574686f64223a22474e542e736561726368222c22766572223a2231227d00");
};

struct DeviceInfo {
    QString name;
    QString model;
    QString ip;
    QString sn;
    QString mac;
};

struct DeviceResponse {
    QString dataText;
    QString dataHex;
    QString sourceIp;
    int sourcePort = 0;
};

struct SearchResult {
    bool success = false;
    QVector<DeviceResponse> responses;
    QString error;
};

} // namespace pp

