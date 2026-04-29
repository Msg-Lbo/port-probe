#include "core/UdpScanner.h"

#include "core/Hex.h"

#include <QElapsedTimer>
#include <QSet>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace pp {

static QString decodePayload(const QByteArray& data)
{
    const QString utf8 = QString::fromUtf8(data);
    if (!utf8.isEmpty()) {
        return utf8;
    }

    const QString local = QString::fromLocal8Bit(data);
    if (!local.isEmpty()) {
        return local;
    }

    return QString::fromLatin1(data.toHex());
}

#pragma pack(push, 1)
struct IpHeader {
    unsigned char ver_ihl;
    unsigned char tos;
    unsigned short tot_len;
    unsigned short id;
    unsigned short frag_off;
    unsigned char ttl;
    unsigned char protocol;
    unsigned short check;
    unsigned int saddr;
    unsigned int daddr;
};
struct UdpHeader {
    unsigned short source;
    unsigned short dest;
    unsigned short len;
    unsigned short check;
};
#pragma pack(pop)

static int readIhlBytes(unsigned char ver_ihl) { return (ver_ihl & 0x0F) * 4; }

static bool parseIpv4(const QByteArray& ip, in_addr* outAddr)
{
    if (!outAddr) {
        return false;
    }
    const unsigned long v = inet_addr(ip.constData());
    if (v == INADDR_NONE && ip != "255.255.255.255") {
        return false;
    }
    outAddr->s_addr = v;
    return true;
}

static QString ipv4ToString(const in_addr& addr)
{
    in_addr copy = addr;
    const char* s = inet_ntoa(copy);
    return s ? QString::fromLatin1(s) : QString();
}

UdpScanner::UdpScanner(const NicService& nicService) : _nicService(nicService) {}

SearchResult UdpScanner::search(const QString& interfaceIp, const SearchConfig& cfg) const
{
    if (!_nicService.isLocalIp(interfaceIp)) {
        SearchResult r;
        r.success = false;
        r.error = QStringLiteral("IP %1 不是本机有效地址，请刷新网卡列表后重试").arg(interfaceIp);
        return r;
    }

    const auto payload = hexToBytes(cfg.broadcastDataHex);
    if (payload.isEmpty()) {
        SearchResult r;
        r.success = false;
        r.error = QStringLiteral("broadcast_data 十六进制解码失败: 请输入有效的十六进制字符串");
        return r;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SearchResult r;
        r.success = false;
        r.error = QStringLiteral("网络错误: WSAStartup 失败");
        return r;
    }

    SOCKET sendSock = INVALID_SOCKET;
    SOCKET recvSock = INVALID_SOCKET;
    SOCKET rawSock = INVALID_SOCKET;
    QVector<DeviceResponse> responses;
    QSet<QString> seen;

    auto cleanup = [&]() {
        if (sendSock != INVALID_SOCKET) {
            closesocket(sendSock);
            sendSock = INVALID_SOCKET;
        }
        if (recvSock != INVALID_SOCKET) {
            closesocket(recvSock);
            recvSock = INVALID_SOCKET;
        }
        if (rawSock != INVALID_SOCKET) {
            closesocket(rawSock);
            rawSock = INVALID_SOCKET;
        }
        WSACleanup();
    };

    try {
        // UDP send (WinSock) - keep behavior close to previous C# implementation.
        sendSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sendSock == INVALID_SOCKET) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 创建 UDP 发送套接字失败");
            cleanup();
            return r;
        }

        BOOL reuse = TRUE;
        setsockopt(sendSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        BOOL broadcast = TRUE;
        if (setsockopt(sendSock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) == SOCKET_ERROR) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 启用 UDP 广播失败");
            cleanup();
            return r;
        }

        sockaddr_in sendBindAddr {};
        sendBindAddr.sin_family = AF_INET;
        sendBindAddr.sin_port = htons(static_cast<u_short>(cfg.sourcePort));
        if (!parseIpv4(interfaceIp.toUtf8(), &sendBindAddr.sin_addr)) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 无效的本机网卡 IP 地址");
            cleanup();
            return r;
        }
        if (bind(sendSock, reinterpret_cast<sockaddr*>(&sendBindAddr), sizeof(sendBindAddr)) == SOCKET_ERROR) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 发送套接字绑定失败（端口可能被占用）");
            cleanup();
            return r;
        }

        // UDP receive channel (more stable on some Qt5/Win7 environments)
        recvSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (recvSock == INVALID_SOCKET) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 创建 UDP 接收套接字失败");
            cleanup();
            return r;
        }
        setsockopt(recvSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(recvSock, reinterpret_cast<sockaddr*>(&sendBindAddr), sizeof(sendBindAddr)) == SOCKET_ERROR) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 接收套接字绑定失败（端口可能被占用）");
            cleanup();
            return r;
        }
        DWORD recvTimeoutMs = 200;
        setsockopt(recvSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs), sizeof(recvTimeoutMs));

        // Raw receive (WinSock): requires admin for SIO_RCVALL.
        rawSock = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
        if (rawSock == INVALID_SOCKET) {
            const int err = WSAGetLastError();
            SearchResult r;
            r.success = false;
            r.error = (err == WSAEACCES)
                ? QStringLiteral("需要管理员权限才能使用原始套接字模式，请以管理员身份运行")
                : QStringLiteral("网络错误: 创建 Raw Socket 失败");
            cleanup();
            return r;
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        if (!parseIpv4(interfaceIp.toUtf8(), &addr.sin_addr)) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: Raw Socket 绑定 IP 无效");
            cleanup();
            return r;
        }
        addr.sin_port = 0;
        if (bind(rawSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: Raw Socket 绑定失败");
            cleanup();
            return r;
        }

        // enable promiscuous receive
        DWORD inBuf = 1;
        DWORD outBuf = 0;
        if (WSAIoctl(rawSock, SIO_RCVALL, &inBuf, sizeof(inBuf), &outBuf, sizeof(outBuf), &outBuf, nullptr, nullptr) == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            SearchResult r;
            r.success = false;
            r.error = (err == WSAEACCES)
                ? QStringLiteral("需要管理员权限才能使用原始套接字模式，请以管理员身份运行")
                : QStringLiteral("网络错误: 启用抓包模式失败");
            cleanup();
            return r;
        }

        // set recv timeout
        DWORD timeoutMs = 500;
        setsockopt(rawSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        // send broadcast
        sockaddr_in destAddr {};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(static_cast<u_short>(cfg.destPort));
        if (!parseIpv4(cfg.destIp.toUtf8(), &destAddr.sin_addr)) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 目标 IP 地址无效");
            cleanup();
            return r;
        }
        const int sent = sendto(
            sendSock,
            payload.constData(),
            payload.size(),
            0,
            reinterpret_cast<sockaddr*>(&destAddr),
            sizeof(destAddr));
        if (sent == SOCKET_ERROR) {
            SearchResult r;
            r.success = false;
            r.error = QStringLiteral("网络错误: 广播发送失败");
            cleanup();
            return r;
        }

        QElapsedTimer t;
        t.start();
        const qint64 totalMs = static_cast<qint64>(cfg.timeoutSeconds) * 1000;

        QByteArray buffer;
        buffer.resize(65535);

        while (t.elapsed() < totalMs) {
            // 1) UDP direct receive
            sockaddr_in fromAddr {};
            int fromLen = sizeof(fromAddr);
            const int udpReceived = recvfrom(
                recvSock,
                buffer.data(),
                buffer.size(),
                0,
                reinterpret_cast<sockaddr*>(&fromAddr),
                &fromLen);
            if (udpReceived > 0) {
                const QString srcIp = ipv4ToString(fromAddr.sin_addr);
                const int srcPort = ntohs(fromAddr.sin_port);
                if (srcPort == cfg.destPort) {
                    const QByteArray data = buffer.left(udpReceived);
                    DeviceResponse resp;
                    resp.sourceIp = srcIp;
                    resp.sourcePort = srcPort;
                    resp.dataText = decodePayload(data);
                    resp.dataHex = QString::fromLatin1(data.toHex());
                    const QString key = resp.sourceIp + ":" + QString::number(resp.sourcePort) + ":" + resp.dataText.left(96);
                    if (!seen.contains(key)) {
                        seen.insert(key);
                        responses.push_back(resp);
                    }
                }
            }

            // 2) Raw receive fallback
            int received = recv(rawSock, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                continue;
            }
            if (received < static_cast<int>(sizeof(IpHeader))) {
                continue;
            }

            const auto* ip = reinterpret_cast<const IpHeader*>(buffer.constData());
            const int ihlBytes = readIhlBytes(ip->ver_ihl);
            if (ihlBytes < 20 || received < ihlBytes + static_cast<int>(sizeof(UdpHeader))) {
                continue;
            }
            if (ip->protocol != 17) {
                continue;
            }

            const auto* udp = reinterpret_cast<const UdpHeader*>(buffer.constData() + ihlBytes);
            const int srcPort = ntohs(udp->source);
            const int dstPort = ntohs(udp->dest);
            if (srcPort != cfg.destPort || dstPort != cfg.sourcePort) {
                continue;
            }

            const int udpLen = ntohs(udp->len);
            const int dataLen = qMax(0, udpLen - 8);
            if (received < ihlBytes + 8 + dataLen) {
                continue;
            }

            const QByteArray data = buffer.mid(ihlBytes + 8, dataLen);

            in_addr srcAddr {};
            srcAddr.s_addr = ip->saddr;
            DeviceResponse resp;
            resp.sourceIp = ipv4ToString(srcAddr);
            resp.sourcePort = srcPort;
            resp.dataText = decodePayload(data);
            resp.dataHex = QString::fromLatin1(data.toHex());
            const QString key = resp.sourceIp + ":" + QString::number(resp.sourcePort) + ":" + resp.dataText.left(96);
            if (!seen.contains(key)) {
                seen.insert(key);
                responses.push_back(resp);
            }
        }

        // disable promiscuous receive
        DWORD disable = 0;
        DWORD bytes = 0;
        WSAIoctl(rawSock, SIO_RCVALL, &disable, sizeof(disable), &outBuf, sizeof(outBuf), &bytes, nullptr, nullptr);

        SearchResult r;
        r.success = true;
        r.responses = responses;
        cleanup();
        return r;
    } catch (...) {
        SearchResult r;
        r.success = false;
        r.error = QStringLiteral("网络错误: 未知异常");
        cleanup();
        return r;
    }
#else
    SearchResult r;
    r.success = false;
    r.error = QStringLiteral("当前版本仅支持 Windows");
    return r;
#endif
}

} // namespace pp

