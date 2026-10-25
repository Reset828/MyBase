#include "sqliterepositories.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QMutexLocker>
qint64 SqliteNoteRepository::insertNote(const Note& note) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("INSERT INTO notes (title, content, created_time, modified_time, is_deleted, deleted_time, is_encrypted, encryption_iv) "
                  "VALUES (:title, :content, :created_time, :modified_time, :is_deleted, :deleted_time, :is_encrypted, :encryption_iv)");
    query.bindValue(":title", note.title);
    query.bindValue(":content", note.content.isEmpty() ? "" : note.content);
    query.bindValue(":created_time", note.createdTime.toMSecsSinceEpoch());
    query.bindValue(":modified_time", note.modifiedTime.toMSecsSinceEpoch());
    query.bindValue(":is_deleted", note.isDeleted ? 1 : 0);
    query.bindValue(":deleted_time", note.deletedTime.toMSecsSinceEpoch());
    query.bindValue(":is_encrypted", note.isEncrypted ? 1 : 0);
    query.bindValue(":encryption_iv", note.encryptionIv.isEmpty() ? "" : note.encryptionIv);

    if (!query.exec()) {
        qCritical() << "Failed to insert note:" << query.lastError().text();
        return -1;
    }

    qint64 id = query.lastInsertId().toLongLong();
    return id;
}

bool SqliteNoteRepository::updateNote(const Note& note) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("UPDATE notes SET title = :title, content = :content, modified_time = :modified_time, "
                  "is_deleted = :is_deleted, deleted_time = :deleted_time, is_encrypted = :is_encrypted, "
                  "encryption_iv = :encryption_iv WHERE id = :id");
    query.bindValue(":title", note.title);
    query.bindValue(":content", note.content);
    query.bindValue(":modified_time", note.modifiedTime.toMSecsSinceEpoch());
    query.bindValue(":is_deleted", note.isDeleted ? 1 : 0);
    query.bindValue(":deleted_time", note.deletedTime.toMSecsSinceEpoch());
    query.bindValue(":is_encrypted", note.isEncrypted ? 1 : 0);
    query.bindValue(":encryption_iv", note.encryptionIv);
    query.bindValue(":id", note.id);

    if (!query.exec()) {
        qCritical() << "Failed to update note:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool SqliteNoteRepository::softDeleteNote(qint64 noteId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("UPDATE notes SET is_deleted = 1, deleted_time = :deleted_time, modified_time = :modified_time WHERE id = :id");
    query.bindValue(":deleted_time", QDateTime::currentMSecsSinceEpoch());
    query.bindValue(":modified_time", QDateTime::currentMSecsSinceEpoch());
    query.bindValue(":id", noteId);

    if (!query.exec()) {
        qCritical() << "Failed to soft delete note:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool SqliteNoteRepository::permanentlyDeleteNote(qint64 noteId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("DELETE FROM notes WHERE id = :id");
    query.bindValue(":id", noteId);

    if (!query.exec()) {
        qCritical() << "Failed to permanently delete note:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool SqliteNoteRepository::cleanupTrash(int daysThreshold) {
    QMutexLocker locker(&dbMutex_);

    qint64 thresholdTime = QDateTime::currentMSecsSinceEpoch() - (daysThreshold * 24 * 60 * 60 * 1000);

    QSqlQuery query(db_);
    query.prepare("DELETE FROM notes WHERE is_deleted = 1 AND deleted_time < :threshold");
    query.bindValue(":threshold", thresholdTime);

    if (!query.exec()) {
        qCritical() << "Failed to cleanup trash:" << query.lastError().text();
        return false;
    }

    return true;
}

QVector<Note> SqliteNoteRepository::getTrashNotes() {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    QString sql = "SELECT id, title, content, created_time, modified_time, is_deleted, deleted_time, is_encrypted, encryption_iv FROM notes WHERE is_deleted = 1 ORDER BY deleted_time DESC";

    if (!query.exec(sql)) {
        qCritical() << "Failed to get trash notes:" << query.lastError().text();
        return QVector<Note>();
    }

    QVector<Note> notes;
    while (query.next()) {
        Note note;
        note.id = query.value(0).toLongLong();
        note.title = query.value(1).toString();
        note.content = query.value(2).toString();
        note.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
        note.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
        note.isDeleted = query.value(5).toBool();
        note.deletedTime = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong());
        note.isEncrypted = query.value(7).toBool();
        note.encryptionIv = query.value(8).toString();
        notes.append(note);
    }

    return notes;
}

Note SqliteNoteRepository::getNote(qint64 noteId) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    query.prepare("SELECT id, title, content, created_time, modified_time, is_deleted, deleted_time, is_encrypted, encryption_iv FROM notes WHERE id = :id");
    query.bindValue(":id", noteId);

    if (!query.exec() || !query.next()) {
        return Note();
    }

    Note note;
    note.id = query.value(0).toLongLong();
    note.title = query.value(1).toString();
    note.content = query.value(2).toString();
    note.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
    note.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
    note.isDeleted = query.value(5).toBool();
    note.deletedTime = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong());
    note.isEncrypted = query.value(7).toBool();
    note.encryptionIv = query.value(8).toString();
    return note;
}

QVector<Note> SqliteNoteRepository::getAllNotes(bool includeDeleted) {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    QString sql = "SELECT id, title, content, created_time, modified_time, is_deleted, deleted_time, is_encrypted, encryption_iv FROM notes WHERE is_encrypted = 0";
    if (!includeDeleted) {
        sql += " AND is_deleted = 0";
    }
    sql += " ORDER BY created_time ASC";

    if (!query.exec(sql)) {
        qCritical() << "Failed to get notes:" << query.lastError().text();
        return QVector<Note>();
    }

    QVector<Note> notes;
    while (query.next()) {
        Note note;
        note.id = query.value(0).toLongLong();
        note.title = query.value(1).toString();
        note.content = query.value(2).toString();
        note.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
        note.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
        note.isDeleted = query.value(5).toBool();
        note.deletedTime = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong());
        note.isEncrypted = query.value(7).toBool();
        note.encryptionIv = query.value(8).toString();
        notes.append(note);
    }

    return notes;
}

QVector<Note> SqliteNoteRepository::getEncryptedNotes() {
    QMutexLocker locker(&dbMutex_);

    QSqlQuery query(db_);
    QString sql = "SELECT id, title, content, created_time, modified_time, is_deleted, deleted_time, is_encrypted, encryption_iv FROM notes WHERE is_encrypted = 1 AND is_deleted = 0 ORDER BY created_time ASC";

    if (!query.exec(sql)) {
        qCritical() << "Failed to get encrypted notes:" << query.lastError().text();
        return QVector<Note>();
    }

    QVector<Note> notes;
    while (query.next()) {
        Note note;
        note.id = query.value(0).toLongLong();
        note.title = query.value(1).toString();
        note.content = query.value(2).toString();
        note.createdTime = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
        note.modifiedTime = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong());
        note.isDeleted = query.value(5).toBool();
        note.deletedTime = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong());
        note.isEncrypted = query.value(7).toBool();
        note.encryptionIv = query.value(8).toString();
        notes.append(note);
    }

    return notes;
}

