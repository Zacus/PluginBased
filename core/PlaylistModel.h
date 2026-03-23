#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QList>

/**
 * @brief 播放列表数据模型
 *
 * 通过 qmlRegisterType 注册，QML 中：
 *   PlaylistModel { id: playlist }
 *   ListView { model: playlist }
 */
class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int  count        READ rowCount   NOTIFY countChanged)
    Q_PROPERTY(int  currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

public:
    enum Roles {
        UrlRole   = Qt::UserRole + 1,
        TitleRole,
        DurationRole,
        IsCurrentRole
    };

    explicit PlaylistModel(QObject* parent = nullptr);

    // ── QAbstractListModel 接口 ───────────────────────────────────────────
    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── 属性 ──────────────────────────────────────────────────────────────
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

public slots:
    Q_INVOKABLE void append(const QUrl& url, const QString& title = {});
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QUrl  urlAt(int index) const;
    Q_INVOKABLE void  next();
    Q_INVOKABLE void  previous();

signals:
    void countChanged();
    void currentIndexChanged(int index);
    void currentMediaRequested(QUrl url);    ///< PlayerEngine 应监听此信号

private:
    struct Entry {
        QUrl    url;
        QString title;
        qint64  duration = 0;
    };

    QList<Entry> m_entries;
    int          m_currentIndex = -1;
};
