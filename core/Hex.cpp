#include "core/Hex.h"

#include <QChar>

namespace pp {

static int hexNibble(const QChar c) {
    if (c.isDigit()) return c.unicode() - '0';
    const auto u = c.toUpper().unicode();
    if (u >= 'A' && u <= 'F') return 10 + (u - 'A');
    return -1;
}

QByteArray hexToBytes(const QString& hex)
{
    const auto s = hex.trimmed();
    if (s.isEmpty() || (s.size() % 2) != 0) {
        return {};
    }
    QByteArray out;
    out.resize(s.size() / 2);
    for (int i = 0; i < out.size(); ++i) {
        const int hi = hexNibble(s.at(i * 2));
        const int lo = hexNibble(s.at(i * 2 + 1));
        if (hi < 0 || lo < 0) {
            return {};
        }
        out[i] = static_cast<char>((hi << 4) | lo);
    }
    return out;
}

} // namespace pp

