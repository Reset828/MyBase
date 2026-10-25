#include "databaseschema.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

namespace {

bool executeStatements(QSqlDatabase& database, const QStringList& statements) {
    for (const QString& statement : statements) {
        QSqlQuery query(database);
        if (!query.exec(statement)) {
            qCritical() << "Schema migration failed:" << statement << query.lastError().text();
            return false;
        }
    }
    return true;
}

}

bool DatabaseSchema::migrationRequired(QSqlDatabase& database, bool& required) {
    QSqlQuery exists(database);
    exists.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'"));
    if (!exists.exec()) {
        qCritical() << "Failed to inspect schema version table:" << exists.lastError().text();
        return false;
    }

    if (!exists.next()) {
        required = true;
        return true;
    }

    QSqlQuery versionQuery(database);
    if (!versionQuery.exec(QStringLiteral(
            "SELECT COUNT(*), COALESCE(MAX(version), 0) FROM schema_version")) ||
        !versionQuery.next()) {
        qCritical() << "Failed to read schema version:" << versionQuery.lastError().text();
        return false;
    }

    const int appliedVersionCount = versionQuery.value(0).toInt();
    const int currentVersion = versionQuery.value(1).toInt();
    if (currentVersion < 0 || currentVersion > CurrentVersion || appliedVersionCount != currentVersion) {
        qCritical() << "Unsupported or incomplete database schema history:" << appliedVersionCount
                    << currentVersion;
        return false;
    }

    required = currentVersion < CurrentVersion;
    return true;
}

bool DatabaseSchema::migrate(QSqlDatabase& database) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "version INTEGER NOT NULL PRIMARY KEY, "
            "applied_time INTEGER NOT NULL)"))) {
        qCritical() << "Failed to create schema_version:" << query.lastError().text();
        return false;
    }

    bool hasMigrations = false;
    if (!migrationRequired(database, hasMigrations)) {
        return false;
    }
    if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_version")) || !query.next()) {
        qCritical() << "Failed to read schema version:" << query.lastError().text();
        return false;
    }
    int version = query.value(0).toInt();

    while (hasMigrations && version < CurrentVersion) {
        const int targetVersion = version + 1;
        if (!database.transaction()) {
            qCritical() << "Failed to start schema migration to version" << targetVersion
                        << database.lastError().text();
            return false;
        }

        bool migrated = false;
        switch (targetVersion) {
        case 1:
            migrated = createVersionOne(database);
            break;
        case 2:
            migrated = migrateToVersionTwo(database);
            break;
        case 3:
            migrated = migrateToVersionThree(database);
            break;
        case 4:
            migrated = migrateToVersionFour(database);
            break;
        case 5:
            migrated = migrateToVersionFive(database);
            break;
        default:
            break;
        }

        if (migrated) {
            QSqlQuery recordVersion(database);
            recordVersion.prepare(QStringLiteral(
                "INSERT INTO schema_version(version, applied_time) VALUES (:version, :applied_time)"));
            recordVersion.bindValue(QStringLiteral(":version"), targetVersion);
            recordVersion.bindValue(QStringLiteral(":applied_time"), QDateTime::currentMSecsSinceEpoch());
            migrated = recordVersion.exec();
            if (!migrated) {
                qCritical() << "Failed to record schema version:" << recordVersion.lastError().text();
            }
        }

        if (!migrated || !database.commit()) {
            qCritical() << "Rolling back schema migration to version" << targetVersion
                        << database.lastError().text();
            database.rollback();
            return false;
        }
        version = targetVersion;
    }

    return true;
}

bool DatabaseSchema::createVersionOne(QSqlDatabase& database) {
    return executeStatements(database, {
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS notes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                content TEXT NOT NULL,
                created_time INTEGER NOT NULL,
                modified_time INTEGER NOT NULL,
                is_deleted INTEGER DEFAULT 0,
                deleted_time INTEGER DEFAULT 0
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS tags (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                parent_id INTEGER DEFAULT NULL,
                created_time INTEGER NOT NULL,
                FOREIGN KEY (parent_id) REFERENCES tags(id) ON DELETE CASCADE
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS note_tags (
                note_id INTEGER NOT NULL,
                tag_id INTEGER NOT NULL,
                created_time INTEGER NOT NULL,
                PRIMARY KEY (note_id, tag_id),
                FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE,
                FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
            )
        )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_time DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_deleted ON notes(is_deleted)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_title ON notes(title)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_created ON notes(created_time DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tags_parent ON tags(parent_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_note_tags_note ON note_tags(note_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_note_tags_tag ON note_tags(tag_id)")
    });
}

bool DatabaseSchema::migrateToVersionTwo(QSqlDatabase& database) {
    if (!ensureColumn(database, QStringLiteral("notes"), QStringLiteral("is_encrypted"),
                      QStringLiteral("INTEGER DEFAULT 0")) ||
        !ensureColumn(database, QStringLiteral("notes"), QStringLiteral("encryption_iv"),
                      QStringLiteral("TEXT DEFAULT ''")) ||
        !ensureColumn(database, QStringLiteral("tags"), QStringLiteral("sort_order"),
                      QStringLiteral("INTEGER DEFAULT 0"))) {
        return false;
    }

    return executeStatements(database, {
        QStringLiteral("UPDATE tags SET parent_id = NULL WHERE parent_id = -1"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_time DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_deleted ON notes(is_deleted)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_title ON notes(title)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_notes_created ON notes(created_time DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tags_parent ON tags(parent_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_note_tags_note ON note_tags(note_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_note_tags_tag ON note_tags(tag_id)")
    });
}

bool DatabaseSchema::ensureColumn(QSqlDatabase& database,
                                  const QString& table,
                                  const QString& column,
                                  const QString& declaration) {
    QSqlQuery columns(database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        qCritical() << "Failed to inspect table schema:" << table << columns.lastError().text();
        return false;
    }

    while (columns.next()) {
        if (columns.value(QStringLiteral("name")).toString() == column) {
            return true;
        }
    }

    QSqlQuery alter(database);
    const QString statement = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                                  .arg(table, column, declaration);
    if (!alter.exec(statement)) {
        qCritical() << "Failed to add schema column:" << statement << alter.lastError().text();
        return false;
    }
    return true;
}


bool DatabaseSchema::migrateToVersionThree(QSqlDatabase& database) {
    return executeStatements(database, {
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS note_revisions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                note_id INTEGER NOT NULL,
                title TEXT NOT NULL,
                content TEXT NOT NULL,
                created_time INTEGER NOT NULL,
                FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
            )
        )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_note_revisions_note_time ON note_revisions(note_id, created_time DESC)"),
        QStringLiteral(R"(
            CREATE TRIGGER IF NOT EXISTS notes_revision_before_update
            BEFORE UPDATE OF title, content ON notes
            WHEN OLD.is_encrypted = 0 AND NEW.is_encrypted = 0 AND (OLD.title <> NEW.title OR OLD.content <> NEW.content)
            BEGIN
                INSERT INTO note_revisions(note_id, title, content, created_time)
                VALUES (OLD.id, OLD.title, OLD.content, CAST(strftime('%s','now') AS INTEGER) * 1000);
            END
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS bookmarks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                note_id INTEGER NOT NULL,
                label TEXT NOT NULL DEFAULT '',
                position INTEGER NOT NULL,
                anchor TEXT NOT NULL DEFAULT '',
                content_fingerprint TEXT NOT NULL DEFAULT '',
                created_time INTEGER NOT NULL,
                FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
            )
        )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bookmarks_note ON bookmarks(note_id)"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS app_session (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                state_json TEXT NOT NULL,
                updated_time INTEGER NOT NULL
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS note_templates (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                content TEXT NOT NULL,
                is_builtin INTEGER NOT NULL DEFAULT 0,
                modified_time INTEGER NOT NULL
            )
        )")
    });
}


bool DatabaseSchema::migrateToVersionFour(QSqlDatabase& database) {
    if (!ensureColumn(database, QStringLiteral("bookmarks"), QStringLiteral("anchor"), QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(database, QStringLiteral("bookmarks"), QStringLiteral("content_fingerprint"), QStringLiteral("TEXT NOT NULL DEFAULT ''"))) {
        return false;
    }
    if (!ensureColumn(database, QStringLiteral("note_templates"), QStringLiteral("description"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(database, QStringLiteral("note_templates"), QStringLiteral("category"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(database, QStringLiteral("note_templates"), QStringLiteral("variables_json"),
                      QStringLiteral("TEXT NOT NULL DEFAULT '{}'")) ||
        !ensureColumn(database, QStringLiteral("note_templates"), QStringLiteral("created_time"),
                      QStringLiteral("INTEGER NOT NULL DEFAULT 0"))) {
        return false;
    }
    QSqlQuery query(database);
    return query.exec(QStringLiteral("UPDATE note_templates SET created_time=modified_time WHERE created_time=0"));
}

bool DatabaseSchema::migrateToVersionFive(QSqlDatabase& database) {
    return executeStatements(database, {
        QStringLiteral("DELETE FROM note_revisions WHERE note_id IN (SELECT id FROM notes WHERE is_encrypted = 1)"),
        QStringLiteral(R"(
            CREATE TRIGGER IF NOT EXISTS notes_clear_revisions_after_encrypt
            AFTER UPDATE OF is_encrypted ON notes
            WHEN OLD.is_encrypted = 0 AND NEW.is_encrypted = 1
            BEGIN
                DELETE FROM note_revisions WHERE note_id = NEW.id;
            END
        )")
    });
}
