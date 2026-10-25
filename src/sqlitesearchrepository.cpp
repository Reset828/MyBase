#include "sqliterepositories.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QMutexLocker>
#include <QUuid>
#include <QDateTime>
#include <QDebug>
class ScopedSqliteReadConnection {
public:
    explicit ScopedSqliteReadConnection(const QString& databasePath)
        : connectionName_(QStringLiteral("mybase-read-%1")
                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
        database_.setDatabaseName(databasePath);
        database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!database_.open()) {
            error_ = database_.lastError().text();
            return;
        }

        {
            QSqlQuery query(database_);
            if (!query.exec(QStringLiteral("PRAGMA query_only = ON")) ||
                !query.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
                error_ = query.lastError().text();
                database_.close();
                database_ = QSqlDatabase();
                QSqlDatabase::removeDatabase(connectionName_);
                connectionName_.clear();
                return;
            }
        }
        ready_ = true;
    }

    ~ScopedSqliteReadConnection() {
        if (!connectionName_.isEmpty()) {
            database_.close();
            database_ = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName_);
        }
    }

    bool isReady() const { return ready_; }
    const QString& error() const { return error_; }
    QSqlDatabase& database() { return database_; }

private:
    QString connectionName_;
    QSqlDatabase database_;
    QString error_;
    bool ready_ = false;
};


QVector<SearchResult> SqliteSearchRepository::searchNotes(const QString& ftsQuery, int snippetContextLength) {
    QVector<SearchResult> results;
    if (ftsQuery.trimmed().isEmpty()) {
        return results;
    }

    QString databasePath;
    {
        QMutexLocker locker(&dbMutex_);
        databasePath = databasePath_;
    }
    if (databasePath.isEmpty()) {
        return results;
    }

    ScopedSqliteReadConnection readConnection(databasePath);
    if (!readConnection.isReady()) {
        qCritical() << "Failed to open SQLite search connection:" << readConnection.error();
        return results;
    }

    QSqlQuery query(readConnection.database());
    query.prepare(QStringLiteral(
        "SELECT n.id, n.title, n.modified_time, "
        "snippet(notes_fts, 1, '<b>', '</b>', '...', :context) AS title_snippet, "
        "snippet(notes_fts, 2, '<b>', '</b>', '...', :context) AS content_snippet, "
        "bm25(notes_fts) AS score "
        "FROM notes_fts "
        "INNER JOIN notes n ON n.id = notes_fts.note_id "
        "WHERE n.is_deleted = 0 AND notes_fts MATCH :ftsQuery "
        "ORDER BY score ASC"));
    query.bindValue(QStringLiteral(":ftsQuery"), ftsQuery);
    query.bindValue(QStringLiteral(":context"), qBound(1, snippetContextLength, 200));

    if (!query.exec()) {
        qCritical() << "FTS query failed:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        SearchResult result;
        result.noteId = query.value(0).toLongLong();
        result.title = query.value(1).toString();
        result.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong());
        result.snippet = query.value(4).toString();
        if (result.snippet.isEmpty()) {
            result.snippet = query.value(3).toString();
        }
        result.relevanceScore = query.value(5).toDouble();
        results.append(result);
    }
    return results;
}

QVector<SearchResult> SqliteSearchRepository::searchNotesByTags(const QString& ftsQuery,
                                                               const QSet<qint64>& tagIds,
                                                               TagMatchMode matchMode,
                                                               int snippetContextLength) {
    QVector<SearchResult> results;

    QString databasePath;
    {
        QMutexLocker locker(&dbMutex_);
        databasePath = databasePath_;
    }
    if (databasePath.isEmpty()) {
        return results;
    }

    ScopedSqliteReadConnection readConnection(databasePath);
    if (!readConnection.isReady()) {
        qCritical() << "Failed to open SQLite search connection:" << readConnection.error();
        return results;
    }

    const bool hasQuery = !ftsQuery.trimmed().isEmpty();
    const QList<qint64> orderedTagIds = tagIds.values();
    QStringList tagPlaceholders;
    for (int i = 0; i < orderedTagIds.size(); ++i) {
        tagPlaceholders.append(QStringLiteral(":tag%1").arg(i));
    }

    QStringList conditions{QStringLiteral("n.is_deleted = 0")};
    if (hasQuery) {
        conditions.append(QStringLiteral("notes_fts MATCH :ftsQuery"));
    }
    if (!orderedTagIds.isEmpty()) {
        QString tagCondition = QStringLiteral(
            "n.id IN (SELECT nt.note_id FROM note_tags nt WHERE nt.tag_id IN (%1)")
            .arg(tagPlaceholders.join(QStringLiteral(", ")));
        if (matchMode == TagMatchMode::And) {
            tagCondition += QStringLiteral(" GROUP BY nt.note_id HAVING COUNT(DISTINCT nt.tag_id) = :tagCount");
        }
        tagCondition += QLatin1Char(')');
        conditions.append(tagCondition);
    }

    QString sql = QStringLiteral("SELECT n.id, n.title, n.modified_time");
    if (hasQuery) {
        sql += QStringLiteral(
            ", snippet(notes_fts, 1, '<b>', '</b>', '...', :context) AS title_snippet, "
            "snippet(notes_fts, 2, '<b>', '</b>', '...', :context) AS content_snippet, "
            "bm25(notes_fts) AS score");
    }
    sql += hasQuery
        ? QStringLiteral(" FROM notes_fts INNER JOIN notes n ON n.id = notes_fts.note_id WHERE %1 ORDER BY score ASC")
              .arg(conditions.join(QStringLiteral(" AND ")))
        : QStringLiteral(" FROM notes n WHERE %1 ORDER BY n.modified_time DESC")
              .arg(conditions.join(QStringLiteral(" AND ")));

    QSqlQuery query(readConnection.database());
    query.prepare(sql);
    if (hasQuery) {
        query.bindValue(QStringLiteral(":ftsQuery"), ftsQuery);
        query.bindValue(QStringLiteral(":context"), qBound(1, snippetContextLength, 200));
    }
    for (int i = 0; i < orderedTagIds.size(); ++i) {
        query.bindValue(tagPlaceholders.at(i), orderedTagIds.at(i));
    }
    if (!orderedTagIds.isEmpty() && matchMode == TagMatchMode::And) {
        query.bindValue(QStringLiteral(":tagCount"), orderedTagIds.size());
    }

    if (!query.exec()) {
        qCritical() << "Combined search failed:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        SearchResult result;
        result.noteId = query.value(0).toLongLong();
        result.title = query.value(1).toString();
        result.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong());
        if (hasQuery) {
            result.snippet = query.value(4).toString();
            if (result.snippet.isEmpty()) {
                result.snippet = query.value(3).toString();
            }
            result.relevanceScore = query.value(5).toDouble();
        }
        results.append(result);
    }
    return results;
}

