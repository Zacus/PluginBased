#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QList>
#include <QQmlEngine>

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count        READ rowCount     NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex
               NOTIFY currentIndexChanged)

public:
    enum Roles {
        UrlRole      = Qt::UserRole + 1,
        TitleRole,
        DurationRole,
        IsCurrentRole
    };

    explicit PlaylistModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int  currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

public slots:
    Q_INVOKABLE void append(const QUrl& url, const QString& title = {});
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QUrl urlAt(int index) const;
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

signals:
    void countChanged();
    void currentIndexChanged(int index);
    void currentMediaRequested(QUrl url);

private:
    struct Entry { QUrl url; QString title; qint64 duration = 0; };
    QList<Entry> m_entries;
    int          m_currentIndex = -1;
};
