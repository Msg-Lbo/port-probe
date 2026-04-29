#include "app/DeviceTableModel.h"

namespace pp {

DeviceTableModel::DeviceTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int DeviceTableModel::rowCount(const QModelIndex&) const
{
    return _devices.size();
}

int DeviceTableModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    if (index.column() == Selected && role == Qt::CheckStateRole) {
        if (index.row() >= 0 && index.row() < _selected.size()) {
            return _selected.at(index.row()) ? Qt::Checked : Qt::Unchecked;
        }
        return Qt::Unchecked;
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == Selected) {
            return static_cast<int>(Qt::AlignCenter);
        }
        return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    const auto& d = _devices.at(index.row());
    switch (index.column()) {
        case Selected: return {};
        case Name: return d.name;
        case Model: return d.model;
        case Ip: return d.ip;
        case Sn: return d.sn;
        case Mac: return d.mac;
        default: return {};
    }
}

bool DeviceTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.column() != Selected || role != Qt::CheckStateRole) {
        return false;
    }
    if (index.row() < 0 || index.row() >= _selected.size()) {
        return false;
    }
    _selected[index.row()] = (value.toInt() == Qt::Checked);
    emit dataChanged(index, index, {Qt::CheckStateRole});
    emit headerDataChanged(Qt::Horizontal, Selected, Selected);
    return true;
}

Qt::ItemFlags DeviceTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    auto f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == Selected) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role != Qt::DisplayRole) {
            return {};
        }
        if (section == Selected) {
            return QString();
        }
        switch (section) {
            case Name: return QStringLiteral("名称");
            case Model: return QStringLiteral("Model");
            case Ip: return QStringLiteral("IP");
            case Sn: return QStringLiteral("SN");
            case Mac: return QStringLiteral("MAC");
            default: return {};
        }
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    return section + 1;
}

bool DeviceTableModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant& value, int role)
{
    if (orientation == Qt::Horizontal && section == Selected && role == Qt::CheckStateRole) {
        setAllSelected(value.toInt() == Qt::Checked);
        return true;
    }
    return QAbstractTableModel::setHeaderData(section, orientation, value, role);
}

void DeviceTableModel::setDevices(QVector<DeviceInfo> devices)
{
    beginResetModel();
    _devices = std::move(devices);
    _selected = QVector<bool>(_devices.size(), false);
    endResetModel();
}

QVector<DeviceInfo> DeviceTableModel::selectedDevices() const
{
    QVector<DeviceInfo> out;
    for (int i = 0; i < _devices.size() && i < _selected.size(); ++i) {
        if (_selected.at(i)) {
            out.push_back(_devices.at(i));
        }
    }
    return out;
}

bool DeviceTableModel::allSelected() const
{
    if (_selected.isEmpty()) {
        return false;
    }
    for (const bool checked : _selected) {
        if (!checked) {
            return false;
        }
    }
    return true;
}

void DeviceTableModel::setAllSelected(bool selected)
{
    if (_selected.isEmpty()) {
        return;
    }
    for (int i = 0; i < _selected.size(); ++i) {
        _selected[i] = selected;
    }
    const auto topLeft = index(0, Selected);
    const auto bottomRight = index(_selected.size() - 1, Selected);
    emit dataChanged(topLeft, bottomRight, {Qt::CheckStateRole});
    emit headerDataChanged(Qt::Horizontal, Selected, Selected);
}

} // namespace pp

