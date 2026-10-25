#include "databasemanager.h"
#include "databaseschema.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QVariant>

namespace {
bool hasValidSqliteIntegrity(const QString& databasePath) {
    const QString connectionName = QStringLiteral("mybase-validate-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool valid = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next()) {
                valid = query.value(0).toString() == QStringLiteral("ok") && !query.next();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return valid;
}

bool removeSqliteSidecars(const QString& databasePath) {
    const QStringList sidecars = {
        databasePath + QStringLiteral("-wal"),
        databasePath + QStringLiteral("-shm")
    };
    for (const QString& sidecar : sidecars) {
        if (QFile::exists(sidecar) && !QFile::remove(sidecar)) {
            return false;
        }
    }
    return true;
}
}

DatabaseManager::DatabaseManager(const QString& connectionName)
    : connectionName_(connectionName),
      noteRepository_(db_, dbMutex_),
      tagRepository_(db_, dbMutex_),
      noteTagRepository_(db_, dbMutex_),
      searchRepository_(db_, dbMutex_, databasePath_) {
}

DatabaseManager::~DatabaseManager() {
    if (db_.isOpen()) {
        db_.close();
    }
    db_ = QSqlDatabase();
    if (QSqlDatabase::contains(connectionName_)) {
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool DatabaseManager::backupDatabase(const QString& backupPath) {
    QMutexLocker locker(&dbMutex_);

    if (!db_.isOpen()) {
        qCritical() << "Database is not open for backup";
        return false;
    }

    QString sourcePath = db_.databaseName();
    if (!QFile::exists(sourcePath)) {
        qCritical() << "Source database file does not exist:" << sourcePath;
        return false;
    }

    QDir backupDir = QFileInfo(backupPath).absoluteDir();
    if (!backupDir.exists()) {
        if (!backupDir.mkpath(".")) {
            qCritical() << "Failed to create backup directory:" << backupDir.absolutePath();
            return false;
        }
    }

    QSqlQuery backupQuery(db_);
    QString escapedPath = backupPath;
    escapedPath.replace("'", "''");
    QString sql = "VACUUM INTO '" + escapedPath + "'";
    if (!backupQuery.exec(sql)) {
        qCritical() << "Failed to backup database:" << backupQuery.lastError().text();
        return false;
    }

    return QFile::exists(backupPath);
}

bool DatabaseManager::restoreDatabase(const QString& backupPath) {
    QMutexLocker locker(&dbMutex_);

    if (!QFile::exists(backupPath)) {
        qCritical() << "Backup file does not exist:" << backupPath;
        return false;
    }

    if (!db_.isOpen()) {
        qCritical() << "Database is not open for restore";
        return false;
    }

    QString targetPath = db_.databaseName();
    const QString canonicalTargetPath = QFileInfo(targetPath).canonicalFilePath();
    const QString canonicalBackupPath = QFileInfo(backupPath).canonicalFilePath();
    if (!canonicalTargetPath.isEmpty() && canonicalTargetPath == canonicalBackupPath) {
        qCritical() << "Cannot restore a database from itself:" << backupPath;
        return false;
    }

    const QString restorePath = targetPath + QStringLiteral(".restore-%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString previousPath = targetPath + QStringLiteral(".previous-%1")
                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QFile::copy(backupPath, restorePath)) {
        qCritical() << "Failed to stage database backup:" << backupPath << "->" << restorePath;
        return false;
    }

    if (!hasValidSqliteIntegrity(restorePath)) {
        qCritical() << "Refusing to restore an invalid SQLite database:" << backupPath;
        QFile::remove(restorePath);
        return false;
    }

    {
        QSqlQuery checkpointQuery(db_);
        if (!checkpointQuery.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")) ||
            !checkpointQuery.next() || checkpointQuery.value(0).toInt() != 0) {
            qCritical() << "Failed to checkpoint database before restore:" << checkpointQuery.lastError().text();
            QFile::remove(restorePath);
            return false;
        }
    }

    db_.close();
    auto reopenCurrentDatabase = [this]() {
        if (!db_.open()) {
            return false;
        }
        if (!configureConnection()) {
            db_.close();
            return false;
        }
        return true;
    };

    if (!removeSqliteSidecars(targetPath)) {
        qCritical() << "Failed to clear SQLite sidecar files before restore:" << targetPath;
        reopenCurrentDatabase();
        QFile::remove(restorePath);
        return false;
    }

    if (!QFile::rename(targetPath, previousPath)) {
        qCritical() << "Failed to preserve the current database before restore:" << targetPath;
        reopenCurrentDatabase();
        QFile::remove(restorePath);
        return false;
    }

    if (!QFile::rename(restorePath, targetPath)) {
        qCritical() << "Failed to install staged database backup:" << restorePath;
        const bool originalRestored = QFile::rename(previousPath, targetPath);
        if (originalRestored && !reopenCurrentDatabase()) {
            qCritical() << "Failed to reopen the original database after restore rollback:" << db_.lastError().text();
        }
        QFile::remove(restorePath);
        return false;
    }

    if (!reopenCurrentDatabase()) {
        qCritical() << "Failed to open restored database:" << db_.lastError().text();
        db_.close();
        removeSqliteSidecars(targetPath);
        const QString failedRestorePath = targetPath + QStringLiteral(".failed-%1")
                                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool movedFailedRestore = QFile::rename(targetPath, failedRestorePath);
        if (!movedFailedRestore) {
            movedFailedRestore = QFile::remove(targetPath);
        }
        const bool originalRestored = movedFailedRestore && QFile::rename(previousPath, targetPath);
        if (originalRestored) {
            reopenCurrentDatabase();
        } else {
            qCritical() << "Failed to restore the original database after rollback:" << previousPath;
        }
        if (movedFailedRestore) {
            QFile::remove(failedRestorePath);
        }
        return false;
    }

    if (!QFile::remove(previousPath)) {
        qWarning() << "Restored database successfully but could not remove previous database:" << previousPath;
    }
    return true;
}

bool DatabaseManager::initialize(const QString& dbPath) {
    QMutexLocker locker(&dbMutex_);

    if (initialized_ && db_.isOpen() && db_.databaseName() == dbPath) {
        return true;
    }

    initialized_ = false;
    if (db_.isOpen()) {
        db_.close();
    }
    if (QSqlDatabase::contains(connectionName_)) {
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }

    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db_.setDatabaseName(dbPath);

    if (!db_.open()) {
        qCritical() << "Failed to open database:" << db_.lastError().text();
        return false;
    }
    databasePath_ = dbPath;

    if (!configureConnection()) {
        db_.close();
        databasePath_.clear();
        return false;
    }

    if (!createTablesInternal()) {
        qCritical() << "Failed to create tables";
        db_.close();
        databasePath_.clear();
        return false;
    }

    if (!createFTS5IndexInternal()) {
        qCritical() << "Failed to create FTS5 index";
        db_.close();
        databasePath_.clear();
        return false;
    }

    initialized_ = true;
    return true;
}

bool DatabaseManager::configureConnection() {
    QSqlQuery query(db_);
    const QStringList pragmas = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
        QStringLiteral("PRAGMA busy_timeout = 5000")
    };

    for (const QString& pragma : pragmas) {
        if (!query.exec(pragma)) {
            qCritical() << "Failed to configure SQLite:" << pragma << query.lastError().text();
            return false;
        }
    }
    return true;
}

bool DatabaseManager::createTables() {
    QMutexLocker locker(&dbMutex_);
    return createTablesInternal();
}

bool DatabaseManager::createTablesInternal() {
    bool migrationRequired = false;
    if (!DatabaseSchema::migrationRequired(db_, migrationRequired)) {
        return false;
    }

    if (migrationRequired && QFileInfo(databasePath_).size() > 0) {
        const QString backupPath = databasePath_ + QStringLiteral(".pre-migration-%1-%2.db")
                                       .arg(DatabaseSchema::CurrentVersion)
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlQuery backupQuery(db_);
        QString escapedPath = backupPath;
        escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
        if (!backupQuery.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedPath))) {
            qCritical() << "Failed to create pre-migration backup:" << backupQuery.lastError().text();
            return false;
        }
    }

    return DatabaseSchema::migrate(db_);
}

bool DatabaseManager::createFTS5Index() {
    QMutexLocker locker(&dbMutex_);
    return createFTS5IndexInternal();
}

bool DatabaseManager::createFTS5IndexInternal() {
    QSqlQuery query(db_);

    const QString createFTS5Table = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
            note_id UNINDEXED,
            title,
            content,
            tokenize='unicode61 remove_diacritics 2'
        )
    )";

    const QString createTriggerInsert = R"(
        CREATE TRIGGER IF NOT EXISTS notes_ai AFTER INSERT ON notes BEGIN
            INSERT INTO notes_fts(note_id, title, content)
            VALUES (new.id, new.title, new.content);
        END
    )";

    const QString createTriggerDelete = R"(
        CREATE TRIGGER IF NOT EXISTS notes_ad AFTER DELETE ON notes BEGIN
            DELETE FROM notes_fts WHERE note_id = old.id;
        END
    )";

    const QString createTriggerUpdate = R"(
        CREATE TRIGGER IF NOT EXISTS notes_au AFTER UPDATE ON notes BEGIN
            DELETE FROM notes_fts WHERE note_id = old.id;
            INSERT INTO notes_fts(note_id, title, content)
            VALUES (new.id, new.title, new.content);
        END
    )";

    const QStringList sqlStatements = {
        createFTS5Table,
        createTriggerInsert,
        createTriggerDelete,
        createTriggerUpdate
    };

    for (const QString& sql : sqlStatements) {
        if (!query.exec(sql)) {
            qCritical() << "Failed to execute FTS5 SQL:" << query.lastError().text();
            return false;
        }
    }

    return true;
}

qint64 DatabaseManager::insertNote(const Note& note) {
    return noteRepository_.insertNote(note);
}

bool DatabaseManager::updateNote(const Note& note) {
    return noteRepository_.updateNote(note);
}

bool DatabaseManager::softDeleteNote(qint64 noteId) {
    return noteRepository_.softDeleteNote(noteId);
}

bool DatabaseManager::permanentlyDeleteNote(qint64 noteId) {
    return noteRepository_.permanentlyDeleteNote(noteId);
}

bool DatabaseManager::cleanupTrash(int daysThreshold) {
    return noteRepository_.cleanupTrash(daysThreshold);
}

Note DatabaseManager::getNote(qint64 noteId) {
    return noteRepository_.getNote(noteId);
}

QVector<Note> DatabaseManager::getAllNotes(bool includeDeleted) {
    return noteRepository_.getAllNotes(includeDeleted);
}

QVector<Note> DatabaseManager::getEncryptedNotes() {
    return noteRepository_.getEncryptedNotes();
}

QVector<Note> DatabaseManager::getTrashNotes() {
    return noteRepository_.getTrashNotes();
}

qint64 DatabaseManager::insertTag(const Tag& tag) {
    return tagRepository_.insertTag(tag);
}

bool DatabaseManager::updateTag(const Tag& tag) {
    return tagRepository_.updateTag(tag);
}

bool DatabaseManager::deleteTag(qint64 tagId) {
    return tagRepository_.deleteTag(tagId);
}

bool DatabaseManager::mergeTags(qint64 sourceTagId, qint64 targetTagId) {
    return tagRepository_.mergeTags(sourceTagId, targetTagId);
}

Tag DatabaseManager::getTag(qint64 tagId) {
    return tagRepository_.getTag(tagId);
}

QVector<Tag> DatabaseManager::getAllTags() {
    return tagRepository_.getAllTags();
}

qint64 DatabaseManager::getTagIdByName(const QString& name) {
    return tagRepository_.getTagIdByName(name);
}

bool DatabaseManager::updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder) {
    return tagRepository_.updateTagOrder(tagId, parentId, sortOrder);
}

bool DatabaseManager::reorderTags(const QVector<TagOrderUpdate>& updates) {
    return tagRepository_.reorderTags(updates);
}

QVector<Tag> DatabaseManager::getTagsByParent(qint64 parentId) {
    return tagRepository_.getTagsByParent(parentId);
}

bool DatabaseManager::addNoteTag(qint64 noteId, qint64 tagId) {
    return noteTagRepository_.addNoteTag(noteId, tagId);
}

bool DatabaseManager::removeNoteTag(qint64 noteId, qint64 tagId) {
    return noteTagRepository_.removeNoteTag(noteId, tagId);
}

QVector<qint64> DatabaseManager::getNoteTagIds(qint64 noteId) {
    return noteTagRepository_.getNoteTagIds(noteId);
}

QVector<qint64> DatabaseManager::getTagNoteIds(qint64 tagId) {
    return noteTagRepository_.getTagNoteIds(tagId);
}

QVector<Tag> DatabaseManager::getTagsForNote(qint64 noteId) {
    return noteTagRepository_.getTagsForNote(noteId);
}

QVector<SearchResult> DatabaseManager::searchNotes(const QString& ftsQuery, int snippetContextLength) {
    return searchRepository_.searchNotes(ftsQuery, snippetContextLength);
}

QVector<SearchResult> DatabaseManager::searchNotesByTags(const QString& ftsQuery,
                                                         const QSet<qint64>& tagIds,
                                                         TagMatchMode matchMode,
                                                         int snippetContextLength) {
    return searchRepository_.searchNotesByTags(ftsQuery, tagIds, matchMode, snippetContextLength);
}


