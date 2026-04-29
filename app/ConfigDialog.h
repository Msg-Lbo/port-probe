#pragma once

#include "core/Models.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QTextEdit;

namespace pp {

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget* parent = nullptr);

    void setConfig(const SearchConfig& cfg);
    SearchConfig config() const;

private:
    QSpinBox* _sourcePort = nullptr;
    QSpinBox* _destPort = nullptr;
    QLineEdit* _destIp = nullptr;
    QSpinBox* _timeout = nullptr;
    QTextEdit* _broadcastHex = nullptr;
};

} // namespace pp

