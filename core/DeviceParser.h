#pragma once

#include "core/Models.h"

#include <QVector>

namespace pp {

class DeviceParser {
public:
    QVector<DeviceInfo> parseDevices(const QVector<DeviceResponse>& responses) const;
};

} // namespace pp

