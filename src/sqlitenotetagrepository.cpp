#include "sqliterepositories.h"
#include "databasesqlhelpers.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QMutexLocker>
#include <QDebug>
bool SqliteNoteTagRepository::addNoteTag(qint64 noteId, qint64 tagId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("INSERT OR IGNORE INTO note_tags (note_id, tag_id, created_time) VALUES (:note_id, :tag_id, :created_time)");
    query.bindValue(":note_id", noteId);
    query.bindValue(":tag_id", tagId);
    query.bindValue(":created_time", QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        qCritical() << "Failed to add note tag:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SqliteNoteTagRepository::removeNoteTag(qint64 noteId, qint64 tagId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("DELETE FROM note_tags WHERE note_id = :note_id AND tag_id = :tag_id");
    query.bindValue(":note_id", noteId);
    query.bindValue(":tag_id", tagId);

    if (!query.exec()) {
        qCritical() << "Failed to remove note tag:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

QVector<qint64> SqliteNoteTagRepository::getNoteTagIds(qint64 noteId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("SELECT tag_id FROM note_tags WHERE note_id = :note_id");
    query.bindValue(":note_id", noteId);

    if (!query.exec()) {
        qCritical() << "Failed to get note tag ids:" << query.lastError().text();
        return QVector<qint64>();
    }

    QVector<qint64> tagIds;
    while (query.next()) {
        tagIds.append(query.value(0).toLongLong());
    }

    return tagIds;
}

QVector<qint64> SqliteNoteTagRepository::getTagNoteIds(qint64 tagId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("SELECT note_id FROM note_tags WHERE tag_id = :tag_id");
    query.bindValue(":tag_id", tagId);

    if (!query.exec()) {
        qCritical() << "Failed to get tag note ids:" << query.lastError().text();
        return QVector<qint64>();
    }

    QVector<qint64> noteIds;
    while (query.next()) {
        noteIds.append(query.value(0).toLongLong());
    }

    return noteIds;
}

QVector<Tag> SqliteNoteTagRepository::getTagsForNote(qint64 noteId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT t.id, t.name, t.parent_id, t.sort_order, t.created_time "
        "FROM tags t INNER JOIN note_tags nt ON nt.tag_id = t.id "
        "WHERE nt.note_id = :note_id ORDER BY t.name"));
    query.bindValue(QStringLiteral(":note_id"), noteId);

    if (!query.exec()) {
        qCritical() << "Failed to get tags for note:" << query.lastError().text();
        return {};
    }

    QVector<Tag> tags;
    while (query.next()) {
        Tag tag;
        tag.id = query.value(0).toLongLong();
        tag.name = query.value(1).toString();
        tag.parentId = database_sql::tagParentId(query.value(2));
        tag.sortOrder = query.value(3).toInt();
        tag.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
        tags.append(tag);
    }
    return tags;
}

