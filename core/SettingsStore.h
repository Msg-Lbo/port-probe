#pragma once

#include "core/Models.h"

#include <QString>

namespace pp {

class SettingsStore {
public:
    explicit SettingsStore(QString iniPath);

    SearchConfig load() const;
    void save(const SearchConfig& cfg) const;

private:
    QString _iniPath;
};

} // namespace pp

