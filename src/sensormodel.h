#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <vector>

class SensorSeries;

// Every sensor the car reported, with whether the user wants it charted.
class SensorModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        UnitRole,
        PidTextRole,
        ActiveRole,
        AvailableRole,
        SeriesRole,
    };

    struct Entry {
        QString key;           // "01:0C", or "ATRV" for the adapter voltage
        int pid = -1;          // mode 01 PID, -1 for the adapter voltage
        bool favorite = false;
        bool active = false;
        bool available = true; // false once the car keeps answering NO DATA
        int misses = 0;
        qint64 lastPollMs = -1000000;
        SensorSeries *series = nullptr;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(std::vector<Entry> entries);
    void clear();
    int size() const { return int(m_entries.size()); }
    Entry &entry(int row) { return m_entries[size_t(row)]; }

    void setActive(int row, bool active);
    void setAllActive(bool active);
    void setAvailable(int row, bool available);
    QStringList activeKeys() const;

signals:
    void activeChanged();

private:
    std::vector<Entry> m_entries;
};

// The sensors shown on the dashboard: chosen by the user and still answering.
class ActiveSensorModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};
