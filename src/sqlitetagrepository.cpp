#include "sqliterepositories.h"
#include "databasesqlhelpers.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QMutexLocker>
#include <QDebug>

qint64 SqliteTagRepository::insertTag(const Tag& tag) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("INSERT INTO tags (name, parent_id, sort_order, created_time) VALUES (:name, :parent_id, :sort_order, :created_time)");
    query.bindValue(":name", tag.name);
    database_sql::bindTagParent(query, QStringLiteral(":parent_id"), tag.parentId);
    query.bindValue(":sort_order", tag.sortOrder);
    query.bindValue(":created_time", tag.createdTime.toMSecsSinceEpoch());

    if (!query.exec()) {
        qCritical() << "Failed to insert tag:" << query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool SqliteTagRepository::updateTag(const Tag& tag) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("UPDATE tags SET name = :name, parent_id = :parent_id, sort_order = :sort_order WHERE id = :id");
    query.bindValue(":name", tag.name);
    database_sql::bindTagParent(query, QStringLiteral(":parent_id"), tag.parentId);
    query.bindValue(":sort_order", tag.sortOrder);
    query.bindValue(":id", tag.id);

    if (!query.exec()) {
        qCritical() << "Failed to update tag:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool SqliteTagRepository::deleteTag(qint64 tagId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("DELETE FROM tags WHERE id = :id");
    query.bindValue(":id", tagId);

    if (!query.exec()) {
        qCritical() << "Failed to delete tag:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool SqliteTagRepository::mergeTags(qint64 sourceTagId, qint64 targetTagId) {
    if (sourceTagId == targetTagId) {
        return false;
    }

    QMutexLocker locker(&dbMutex_);
    if (!db_.transaction()) {
        qCritical() << "Failed to start tag merge transaction:" << db_.lastError().text();
        return false;
    }

    {
        QSqlQuery validationQuery(db_);
        validationQuery.prepare(QStringLiteral(
            "SELECT EXISTS(SELECT 1 FROM tags WHERE id = :source_id), "
            "EXISTS(SELECT 1 FROM tags WHERE id = :target_id)"));
        validationQuery.bindValue(QStringLiteral(":source_id"), sourceTagId);
        validationQuery.bindValue(QStringLiteral(":target_id"), targetTagId);
        if (!validationQuery.exec() || !validationQuery.next() ||
            !validationQuery.value(0).toBool() || !validationQuery.value(1).toBool()) {
            qCritical() << "Tag merge requires two existing tags:" << validationQuery.lastError().text();
            db_.rollback();
            return false;
        }
    }

    {
        QSqlQuery transferQuery(db_);
        transferQuery.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO note_tags(note_id, tag_id, created_time) "
            "SELECT note_id, :target_id, created_time FROM note_tags WHERE tag_id = :source_id"));
        transferQuery.bindValue(QStringLiteral(":source_id"), sourceTagId);
        transferQuery.bindValue(QStringLiteral(":target_id"), targetTagId);
        if (!transferQuery.exec()) {
            qCritical() << "Failed to transfer tag assignments:" << transferQuery.lastError().text();
            db_.rollback();
            return false;
        }
    }

    {
        QSqlQuery deleteQuery(db_);
        deleteQuery.prepare(QStringLiteral("DELETE FROM tags WHERE id = :source_id"));
        deleteQuery.bindValue(QStringLiteral(":source_id"), sourceTagId);
        if (!deleteQuery.exec() || deleteQuery.numRowsAffected() != 1) {
            qCritical() << "Failed to delete merged tag:" << deleteQuery.lastError().text();
            db_.rollback();
            return false;
        }
    }

    if (!db_.commit()) {
        qCritical() << "Failed to commit tag merge:" << db_.lastError().text();
        db_.rollback();
        return false;
    }
    return true;
}

Tag SqliteTagRepository::getTag(qint64 tagId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("SELECT id, name, parent_id, sort_order, created_time FROM tags WHERE id = :id");
    query.bindValue(":id", tagId);

    if (!query.exec() || !query.next()) {
        return Tag();
    }

    Tag tag;
    tag.id = query.value(0).toLongLong();
    tag.name = query.value(1).toString();
    tag.parentId = database_sql::tagParentId(query.value(2));
    tag.sortOrder = query.value(3).toInt();
    tag.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
    return tag;
}

QVector<Tag> SqliteTagRepository::getAllTags() {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    QString sql = "SELECT id, name, parent_id, sort_order, created_time FROM tags ORDER BY parent_id, sort_order, name";

    if (!query.exec(sql)) {
        qCritical() << "Failed to get tags:" << query.lastError().text();
        return QVector<Tag>();
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

qint64 SqliteTagRepository::getTagIdByName(const QString& name) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("SELECT id FROM tags WHERE name = :name");
    query.bindValue(":name", name);

    if (!query.exec()) {
        qCritical() << "Failed to get tag by name:" << query.lastError().text();
        return -1;
    }

    if (query.next()) {
        return query.value(0).toLongLong();
    }

    return -1;
}

bool SqliteTagRepository::updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder) {
    return reorderTags({TagOrderUpdate{tagId, parentId, sortOrder}});
}

bool SqliteTagRepository::reorderTags(const QVector<TagOrderUpdate>& updates) {
    QMutexLocker locker(&dbMutex_);
    if (updates.isEmpty()) {
        return true;
    }

    if (!db_.transaction()) {
        qCritical() << "Failed to start tag reorder transaction:" << db_.lastError().text();
        return false;
    }

    QSqlQuery query(db_);
    query.prepare("UPDATE tags SET parent_id = :parent_id, sort_order = :sort_order WHERE id = :id");
    for (const TagOrderUpdate& update : updates) {
        database_sql::bindTagParent(query, QStringLiteral(":parent_id"), update.parentId);
        query.bindValue(QStringLiteral(":sort_order"), update.sortOrder);
        query.bindValue(QStringLiteral(":id"), update.tagId);
        if (!query.exec() || query.numRowsAffected() != 1) {
            qCritical() << "Failed to update tag order for" << update.tagId << query.lastError().text();
            db_.rollback();
            return false;
        }
        query.finish();
    }

    if (!db_.commit()) {
        qCritical() << "Failed to commit tag reorder transaction:" << db_.lastError().text();
        db_.rollback();
        return false;
    }
    return true;
}

QVector<Tag> SqliteTagRepository::getTagsByParent(qint64 parentId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    const QString parentPredicate = parentId < 0
        ? QStringLiteral("parent_id IS NULL")
        : QStringLiteral("parent_id = :parent_id");
    QString sql = QStringLiteral("SELECT id, name, parent_id, sort_order, created_time FROM tags WHERE %1 ORDER BY sort_order, name")
                      .arg(parentPredicate);
    query.prepare(sql);
    if (parentId >= 0) {
        query.bindValue(QStringLiteral(":parent_id"), parentId);
    }

    if (!query.exec()) {
        qCritical() << "Failed to get tags by parent:" << query.lastError().text();
        return QVector<Tag>();
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

