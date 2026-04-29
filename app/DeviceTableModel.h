#pragma once

#include "core/Models.h"

#include <QAbstractTableModel>
#include <QVector>

namespace pp {

class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        Selected = 0,
        Name,
        Model,
        Ip,
        Sn,
        Mac,
        ColumnCount
    };

    explicit DeviceTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant& value, int role) override;

    void setDevices(QVector<DeviceInfo> devices);
    QVector<DeviceInfo> selectedDevices() const;
    bool allSelected() const;
    void setAllSelected(bool selected);
    const QVector<DeviceInfo>& devices() const { return _devices; }

private:
    QVector<DeviceInfo> _devices;
    QVector<bool> _selected;
};

} // namespace pp

