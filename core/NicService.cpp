#include "core/NicService.h"

#include <QtNetwork/QNetworkInterface>

namespace pp {

QVector<InterfaceOption> NicService::listIPv4() const
{
    QVector<InterfaceOption> out;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto& entry : iface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            const auto ipStr = ip.toString();
            if (ipStr.startsWith("127.")) {
                continue;
            }
            InterfaceOption opt;
            opt.ip = ipStr;
            opt.name = QString("%1(%2)").arg(iface.humanReadableName(), ipStr);
            out.push_back(opt);
        }
    }
    return out;
}

bool NicService::isLocalIp(const QString& ip) const
{
    const auto list = listIPv4();
    for (const auto& it : list) {
        if (it.ip == ip) return true;
    }
    return false;
}

} // namespace pp

