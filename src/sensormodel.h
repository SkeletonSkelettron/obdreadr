#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <vector>

class SensorSeries;

// Every sensor the car reported, with whether the user wants it charted
// (active) and whether it is one of the few compared and shown big (pinned).
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
        PinnedRole,
        AvailableRole,
        SeriesRole,
    };

    struct Entry {
        QString key;           // "01:0C", or "ATRV" for the adapter voltage
        int pid = -1;          // mode 01 PID, -1 for the adapter voltage
        bool favorite = false;
        bool active = false;
        bool pinned = false;
        bool available = true; // false once the car keeps answering NO DATA
        int misses = 0;
        qint64 lastPollMs = -1000000;
        SensorSeries *series = nullptr;

        bool shown() const { return active || pinned; }
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
    const Entry &entry(int row) const { return m_entries[size_t(row)]; }

    void setActive(int row, bool active);
    void setPinned(int row, bool pinned);
    void setAll(Role role, bool on);
    void setAvailable(int row, bool available);
    QStringList keys(Role role) const;

signals:
    // Active, pinned or available changed for some sensor.
    void selectionChanged();

private:
    bool setFlag(int row, Role role, bool on);

    std::vector<Entry> m_entries;
};

// The available sensors that have one flag set: the active ones for the
// charts and values tabs, the pinned ones for the overlay and big tabs.
class SensorFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    SensorFilterModel(SensorModel::Role role, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    SensorModel::Role m_role;
};
