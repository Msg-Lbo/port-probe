#include "core/DeviceParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace pp {

static QString pickFromObject(const QJsonObject& o, std::initializer_list<const char*> keys)
{
    for (const auto* k : keys) {
        const auto v = o.value(k);
        if (v.isString()) {
            const auto s = v.toString().trimmed();
            if (!s.isEmpty()) {
                return s;
            }
        } else if (v.isDouble()) {
            return QString::number(v.toDouble());
        }
    }
    return {};
}

static DeviceInfo fromJsonObject(const QJsonObject& o, const QString& ipFallback)
{
    DeviceInfo d;
    d.name = pickFromObject(o, {"Name", "name", "DeviceName", "deviceName", "DevName", "devName"});
    d.model = pickFromObject(o, {"Model", "model", "DeviceType", "deviceType", "type"});
    d.ip = pickFromObject(o, {"Ip", "IP", "ip", "HostIP", "hostIp", "ipv4"});
    d.sn = pickFromObject(o, {"Sn", "SN", "sn", "SerialNo", "serialNo", "serial", "DeviceID", "deviceId"});
    d.mac = pickFromObject(o, {"Mac", "MAC", "mac", "EthAddr", "ethAddr", "hwaddr"});
    if (d.ip.isEmpty()) {
        d.ip = ipFallback;
    }
    return d;
}

static bool looksLikeDeviceObject(const QJsonObject& o)
{
    const auto sn = pickFromObject(o, {"Sn", "SN", "sn", "SerialNo", "serialNo", "serial", "DeviceID", "deviceId"});
    const auto mac = pickFromObject(o, {"Mac", "MAC", "mac", "EthAddr", "ethAddr", "hwaddr"});
    const auto ip = pickFromObject(o, {"Ip", "IP", "ip", "HostIP", "hostIp", "ipv4"});
    return !sn.isEmpty() || !mac.isEmpty() || !ip.isEmpty();
}

static void collectCandidateObjects(const QJsonValue& v, QVector<QJsonObject>& out)
{
    if (v.isObject()) {
        const auto obj = v.toObject();
        if (looksLikeDeviceObject(obj)) {
            out.push_back(obj);
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            collectCandidateObjects(it.value(), out);
        }
        return;
    }
    if (v.isArray()) {
        const auto arr = v.toArray();
        for (const auto& item : arr) {
            collectCandidateObjects(item, out);
        }
    }
}

static DeviceInfo fallbackFromRawText(const QString& text, const QString& ipFallback)
{
    DeviceInfo d;
    d.ip = ipFallback;
    auto extract = [&](const QRegularExpression& re) -> QString {
        const auto m = re.match(text);
        return m.hasMatch() ? m.captured(1).trimmed() : QString{};
    };

    d.sn = extract(QRegularExpression("\"(?:Sn|SN|sn|SerialNo|serialNo|serial)\"\\s*:\\s*\"([^\"]+)\""));
    d.mac = extract(QRegularExpression("\"(?:Mac|MAC|mac|EthAddr|ethAddr)\"\\s*:\\s*\"([^\"]+)\""));
    d.name = extract(QRegularExpression("\"(?:Name|name|DeviceName|deviceName)\"\\s*:\\s*\"([^\"]+)\""));
    d.model = extract(QRegularExpression("\"(?:Model|model|DeviceType|deviceType)\"\\s*:\\s*\"([^\"]+)\""));
    if (d.ip.isEmpty()) {
        d.ip = extract(QRegularExpression("\"(?:Ip|IP|ip|HostIP|hostIp)\"\\s*:\\s*\"([^\"]+)\""));
    }
    return d;
}

static bool tryParseJsonDocument(const QString& dataText, QJsonDocument& outDoc)
{
    const int begin = dataText.indexOf('{');
    const int end = dataText.lastIndexOf('}');
    if (begin < 0 || end <= begin) {
        return false;
    }
    const auto jsonText = dataText.mid(begin, end - begin + 1).toUtf8();
    QJsonParseError err {};
    const auto doc = QJsonDocument::fromJson(jsonText, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    outDoc = doc;
    return true;
}

static bool tryParseJsonDocumentFromHex(const QString& dataHex, QJsonDocument& outDoc)
{
    if (dataHex.trimmed().isEmpty()) {
        return false;
    }
    const QByteArray raw = QByteArray::fromHex(dataHex.toLatin1());
    if (raw.isEmpty()) {
        return false;
    }
    const int begin = raw.indexOf('{');
    const int end = raw.lastIndexOf('}');
    if (begin < 0 || end <= begin) {
        return false;
    }
    const QByteArray jsonBytes = raw.mid(begin, end - begin + 1);
    QJsonParseError err {};
    const auto doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    outDoc = doc;
    return true;
}

QVector<DeviceInfo> DeviceParser::parseDevices(const QVector<DeviceResponse>& responses) const
{
    QVector<DeviceInfo> out;
    QSet<QString> seen;

    for (const auto& resp : responses) {
        QJsonDocument doc;
        if (!tryParseJsonDocument(resp.dataText, doc) && !tryParseJsonDocumentFromHex(resp.dataHex, doc)) {
            continue;
        }
        QVector<QJsonObject> candidates;
        collectCandidateObjects(doc.object(), candidates);
        if (candidates.isEmpty()) {
            // Fallback: try regex extraction directly from raw text.
            const auto fallback = fallbackFromRawText(resp.dataText, resp.sourceIp);
            const QString dedupeKey = !fallback.sn.trimmed().isEmpty()
                ? QStringLiteral("sn:%1").arg(fallback.sn.trimmed())
                : (!fallback.mac.trimmed().isEmpty()
                    ? QStringLiteral("mac:%1").arg(fallback.mac.trimmed())
                    : QStringLiteral("ip:%1").arg(fallback.ip.trimmed()));
            if (!dedupeKey.endsWith(':') && !seen.contains(dedupeKey)) {
                seen.insert(dedupeKey);
                out.push_back(fallback);
            }
            continue;
        }
        for (const auto& obj : candidates) {
            const auto device = fromJsonObject(obj, resp.sourceIp);
            const QString dedupeKey = !device.sn.trimmed().isEmpty()
                ? QStringLiteral("sn:%1").arg(device.sn.trimmed())
                : (!device.mac.trimmed().isEmpty()
                    ? QStringLiteral("mac:%1").arg(device.mac.trimmed())
                    : QStringLiteral("ip:%1").arg(device.ip.trimmed()));
            if (dedupeKey.endsWith(':') || seen.contains(dedupeKey)) {
                continue;
            }
            seen.insert(dedupeKey);
            out.push_back(device);
        }
    }

    std::sort(out.begin(), out.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.ip < b.ip;
    });
    return out;
}

} // namespace pp

