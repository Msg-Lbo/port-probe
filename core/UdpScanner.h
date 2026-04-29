#pragma once

#include "core/Models.h"
#include "core/NicService.h"

#include <QString>

namespace pp {

class UdpScanner {
public:
    explicit UdpScanner(const NicService& nicService);

    SearchResult search(const QString& interfaceIp, const SearchConfig& cfg) const;

private:
    const NicService& _nicService;
};

} // namespace pp

