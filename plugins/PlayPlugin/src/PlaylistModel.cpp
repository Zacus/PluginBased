#include "PlaylistModel.h"
#include "Logger.h"

#include <QFileInfo>

PlaylistModel::PlaylistModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int PlaylistModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entries.size());
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const auto& e = m_entries.at(index.row());
    switch (role) {
    case UrlRole:      return e.url;
    case TitleRole:    return e.title.isEmpty()
                              ? QFileInfo(e.url.toLocalFile()).baseName()
                              : e.title;
    case DurationRole: return e.duration;
    case IsCurrentRole:return (index.row() == m_currentIndex);
    default:           return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        {UrlRole,       "url"},
        {TitleRole,     "title"},
        {DurationRole,  "duration"},
        {IsCurrentRole, "isCurrent"},
    };
}

void PlaylistModel::setCurrentIndex(int idx)
{
    if (idx < -1 || idx >= m_entries.size()) return;
    const int old = m_currentIndex;
    m_currentIndex = idx;

    // 通知两行刷新 isCurrent
    if (old >= 0) emit dataChanged(index(old), index(old), {IsCurrentRole});
    if (idx >= 0) emit dataChanged(index(idx), index(idx), {IsCurrentRole});

    emit currentIndexChanged(m_currentIndex);
    if (idx >= 0) {
        emit currentMediaRequested(m_entries.at(idx).url);
    }
}

void PlaylistModel::append(const QUrl& url, const QString& title)
{
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.push_back({url, title, 0});
    endInsertRows();
    emit countChanged();
    LOG_DEBUG("PlaylistModel: appended {}", url.toString().toStdString());
}

void PlaylistModel::remove(int idx)
{
    if (idx < 0 || idx >= m_entries.size()) return;
    beginRemoveRows({}, idx, idx);
    m_entries.removeAt(idx);
    endRemoveRows();
    emit countChanged();

    if (m_currentIndex == idx) {
        m_currentIndex = -1;
        emit currentIndexChanged(m_currentIndex);
    } else if (m_currentIndex > idx) {
        --m_currentIndex;
        emit currentIndexChanged(m_currentIndex);
    }
}

void PlaylistModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_currentIndex = -1;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged(m_currentIndex);
}

QUrl PlaylistModel::urlAt(int idx) const
{
    if (idx < 0 || idx >= m_entries.size()) return {};
    return m_entries.at(idx).url;
}

void PlaylistModel::next()
{
    if (m_entries.isEmpty()) return;
    setCurrentIndex((m_currentIndex + 1) % m_entries.size());
}

void PlaylistModel::previous()
{
    if (m_entries.isEmpty()) return;
    int idx = m_currentIndex - 1;
    if (idx < 0) idx = m_entries.size() - 1;
    setCurrentIndex(idx);
}
