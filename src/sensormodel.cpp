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
    const Entry &e = m_entries[size_t(index.row())];
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
    if (!index.isValid() || index.row() >= size() || role != ActiveRole)
        return false;
    setActive(index.row(), value.toBool());
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
    emit activeChanged();
}

void SensorModel::clear()
{
    setEntries({});
}

void SensorModel::setActive(int row, bool active)
{
    Entry &e = entry(row);
    if (e.active == active)
        return;
    e.active = active;
    const QModelIndex i = index(row);
    emit dataChanged(i, i, {ActiveRole});
    emit activeChanged();
}

void SensorModel::setAllActive(bool active)
{
    for (int row = 0; row < size(); ++row) {
        Entry &e = entry(row);
        if (e.active != active) {
            e.active = active;
            const QModelIndex i = index(row);
            emit dataChanged(i, i, {ActiveRole});
        }
    }
    emit activeChanged();
}

void SensorModel::setAvailable(int row, bool available)
{
    Entry &e = entry(row);
    if (e.available == available)
        return;
    e.available = available;
    const QModelIndex i = index(row);
    emit dataChanged(i, i, {AvailableRole});
}

QStringList SensorModel::activeKeys() const
{
    QStringList keys;
    for (const Entry &e : m_entries) {
        if (e.active)
            keys << e.key;
    }
    return keys;
}

bool ActiveSensorModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    return i.data(SensorModel::ActiveRole).toBool() && i.data(SensorModel::AvailableRole).toBool();
}
