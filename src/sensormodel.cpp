#include "sensormodel.h"

#include "sensorseries.h"

int SensorModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : size();
}

QVariant SensorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    const Entry &e = entry(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.series ? e.series->name() : QString();
    case KeyRole:
        return e.key;
    case UnitRole:
        return e.series ? e.series->unit() : QString();
    case PidTextRole:
        return e.pid < 0 ? QStringLiteral("AT RV")
                         : QStringLiteral("01 %1").arg(e.pid, 2, 16, QLatin1Char('0')).toUpper();
    case ActiveRole:
        return e.active;
    case PinnedRole:
        return e.pinned;
    case AvailableRole:
        return e.available;
    case SeriesRole:
        return QVariant::fromValue<QObject *>(e.series);
    default:
        return {};
    }
}

bool SensorModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= size() || (role != ActiveRole && role != PinnedRole))
        return false;
    if (setFlag(index.row(), Role(role), value.toBool()))
        emit selectionChanged();
    return true;
}

Qt::ItemFlags SensorModel::flags(const QModelIndex &index) const
{
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

QHash<int, QByteArray> SensorModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {NameRole, "name"},
        {UnitRole, "unit"},
        {PidTextRole, "pidText"},
        {ActiveRole, "active"},
        {PinnedRole, "pinned"},
        {AvailableRole, "available"},
        {SeriesRole, "series"},
    };
}

void SensorModel::setEntries(std::vector<Entry> entries)
{
    beginResetModel();
    for (Entry &e : m_entries)
        e.series->deleteLater();
    m_entries = std::move(entries);
    endResetModel();
    emit selectionChanged();
}

void SensorModel::clear()
{
    setEntries({});
}

bool SensorModel::setFlag(int row, Role role, bool on)
{
    Entry &e = entry(row);
    bool &flag = role == ActiveRole ? e.active : role == PinnedRole ? e.pinned : e.available;
    if (flag == on)
        return false;
    flag = on;
    const QModelIndex i = index(row);
    emit dataChanged(i, i, {role});
    return true;
}

void SensorModel::setActive(int row, bool active)
{
    if (setFlag(row, ActiveRole, active))
        emit selectionChanged();
}

void SensorModel::setPinned(int row, bool pinned)
{
    if (setFlag(row, PinnedRole, pinned))
        emit selectionChanged();
}

void SensorModel::setAvailable(int row, bool available)
{
    if (setFlag(row, AvailableRole, available))
        emit selectionChanged();
}

void SensorModel::setAll(Role role, bool on)
{
    bool changed = false;
    for (int row = 0; row < size(); ++row)
        changed = setFlag(row, role, on) || changed;
    if (changed)
        emit selectionChanged();
}

QStringList SensorModel::keys(Role role) const
{
    QStringList keys;
    for (const Entry &e : m_entries) {
        if ((role == ActiveRole && e.active) || (role == PinnedRole && e.pinned))
            keys << e.key;
    }
    return keys;
}

SensorFilterModel::SensorFilterModel(SensorModel::Role role, QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_role(role)
{
}

bool SensorFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    return i.data(m_role).toBool() && i.data(SensorModel::AvailableRole).toBool();
}
