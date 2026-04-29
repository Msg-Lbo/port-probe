#pragma once

#include "core/Models.h"

#include <QVector>

namespace pp {

class NicService {
public:
    QVector<InterfaceOption> listIPv4() const;
    bool isLocalIp(const QString& ip) const;
};

} // namespace pp

