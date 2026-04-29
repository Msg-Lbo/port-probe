#include "core/SettingsStore.h"

#include <QSettings>

namespace pp {

SettingsStore::SettingsStore(QString iniPath) : _iniPath(std::move(iniPath)) {}

SearchConfig SettingsStore::load() const
{
    QSettings s(_iniPath, QSettings::IniFormat);
    s.beginGroup("search");
    SearchConfig cfg;
    cfg.sourcePort = s.value("source_port", cfg.sourcePort).toInt();
    cfg.destPort = s.value("dest_port", cfg.destPort).toInt();
    cfg.destIp = s.value("dest_ip", cfg.destIp).toString();
    cfg.timeoutSeconds = s.value("timeout_seconds", cfg.timeoutSeconds).toInt();
    cfg.broadcastDataHex = s.value("broadcast_hex", cfg.broadcastDataHex).toString();
    s.endGroup();
    return cfg;
}

void SettingsStore::save(const SearchConfig& cfg) const
{
    QSettings s(_iniPath, QSettings::IniFormat);
    s.beginGroup("search");
    s.setValue("source_port", cfg.sourcePort);
    s.setValue("dest_port", cfg.destPort);
    s.setValue("dest_ip", cfg.destIp);
    s.setValue("timeout_seconds", cfg.timeoutSeconds);
    s.setValue("broadcast_hex", cfg.broadcastDataHex);
    s.endGroup();
    s.sync();
}

} // namespace pp

