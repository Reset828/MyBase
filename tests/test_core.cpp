#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QSet>
#include <limits>

#include "aescrypto.h"
#include "applicationcontext.h"
#include "databasemanager.h"
#include "databaseschema.h"
#include "note.h"
#include "notemanager.h"
#include "notelistmodel.h"
#include "searchengine.h"
#include "tagmanager.h"
#include "tagtreemodel.h"
#include "vaultmanager.h"

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void noteJsonRoundTrip();
    void aesRoundTrip();
    void databaseAndFtsWorkflow();
    void databaseMigratesLegacySchema();
    void databaseSchemaMigrationRollsBackOnFailure();
    void applicationContextComposesFeatureServices();
    void databaseBackupRestoreIsSafe();
    void noteManagerCrudAndTrashSignals();
    void noteManagerEncryptionSignals();
    void noteListModelPresentation();
    void tagRootsChildrenAndCascades();
    void tagTreeModelProjectsTagsAndHeadings();
    void tagMergeIsAtomicAndKeepsIndexConsistent();
    void tagReorderIsTransactional();
    void vaultPasswordLifecycle();
    void searchQueryEscaping();
};

void CoreTests::noteJsonRoundTrip() {
    Note original;
    original.id = 42;
    original.title = QStringLiteral("Architecture");
    original.content = QStringLiteral("# Ports and adapters");
    original.createdTime = QDateTime::fromMSecsSinceEpoch(1000);
    original.modifiedTime = QDateTime::fromMSecsSinceEpoch(2000);
    original.isDeleted = true;
    original.deletedTime = QDateTime::fromMSecsSinceEpoch(3000);
    original.isEncrypted = true;
    original.encryptionIv = QStringLiteral("iv");

    const Note restored = Note::fromJson(original.toJson());
    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.title, original.title);
    QCOMPARE(restored.content, original.content);
    QCOMPARE(restored.createdTime, original.createdTime);
    QCOMPARE(restored.modifiedTime, original.modifiedTime);
    QCOMPARE(restored.isDeleted, original.isDeleted);
    QCOMPARE(restored.deletedTime, original.deletedTime);
    QCOMPARE(restored.isEncrypted, original.isEncrypted);
    QCOMPARE(restored.encryptionIv, original.encryptionIv);
}

void CoreTests::aesRoundTrip() {
    const QByteArray plaintext("confidential markdown content");
    QByteArray iv;
    const QByteArray ciphertext = AesCrypto::encrypt(plaintext, QStringLiteral("correct horse"), iv);

    QVERIFY(!ciphertext.isEmpty());
    QCOMPARE(iv.left(4), QByteArray("MBG2"));
    QCOMPARE(iv.size(), 32);
    QCOMPARE(AesCrypto::decrypt(ciphertext, QStringLiteral("correct horse"), iv), plaintext);
    QVERIFY(AesCrypto::decrypt(ciphertext, QStringLiteral("wrong password"), iv).isEmpty());

    QByteArray tamperedCiphertext = ciphertext;
    tamperedCiphertext[0] = static_cast<char>(tamperedCiphertext[0] ^ 0x01);
    QVERIFY(AesCrypto::decrypt(tamperedCiphertext, QStringLiteral("correct horse"), iv).isEmpty());

    QByteArray tamperedMetadata = iv;
    tamperedMetadata[5] = static_cast<char>(tamperedMetadata[5] ^ 0x01);
    QVERIFY(AesCrypto::decrypt(ciphertext, QStringLiteral("correct horse"), tamperedMetadata).isEmpty());

    const QByteArray legacyIv = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEA==");
    const QByteArray legacyCiphertext = QByteArray::fromBase64(
        "UpY3S+ghl/XnFnefbf06AZ65lIwLy/vhwJSbkr+hwQ+6rfEHmphfxbtAU1aezslj");
    QCOMPARE(AesCrypto::decrypt(legacyCiphertext, QStringLiteral("legacy password"), legacyIv),
             QByteArray("[MYBASE_ENCRYPTED]legacy note content"));
}

void CoreTests::databaseAndFtsWorkflow() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("core_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("core.db"))));

    Note note;
    note.title = QStringLiteral("SQLite architecture");
    note.content = QStringLiteral("Ports make the database replaceable.");
    note.createdTime = QDateTime::currentDateTime();
    note.modifiedTime = note.createdTime;
    const qint64 noteId = database.insertNote(note);
    QVERIFY(noteId > 0);

    const Note loaded = database.getNote(noteId);
    QCOMPARE(loaded.title, note.title);
    QCOMPARE(loaded.content, note.content);

    SearchEngine search(database);
    const QVector<SearchResult> results = search.searchNotes(QStringLiteral("replaceable"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().noteId, noteId);

    auto asyncResults = search.searchNotesAsync(QStringLiteral("replaceable"));
    asyncResults.waitForFinished();
    QVERIFY(asyncResults.isFinished());
    QCOMPARE(asyncResults.result().size(), 1);
    QCOMPARE(asyncResults.result().first().noteId, noteId);

    QVERIFY(database.softDeleteNote(noteId));
    QVERIFY(database.getAllNotes().isEmpty());
    QCOMPARE(database.getTrashNotes().size(), 1);
}

void CoreTests::databaseMigratesLegacySchema() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("legacy-schema.db"));
    const QString legacyConnectionName = QStringLiteral("legacy_schema_seed_connection");
    {
        QSqlDatabase legacyDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), legacyConnectionName);
        legacyDatabase.setDatabaseName(databasePath);
        QVERIFY(legacyDatabase.open());
        QSqlQuery query(legacyDatabase);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT NOT NULL, content TEXT NOT NULL, "
            "created_time INTEGER NOT NULL, modified_time INTEGER NOT NULL, is_deleted INTEGER DEFAULT 0, "
            "deleted_time INTEGER DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, parent_id INTEGER DEFAULT -1, "
            "created_time INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE note_tags (note_id INTEGER NOT NULL, tag_id INTEGER NOT NULL, created_time INTEGER NOT NULL, "
            "PRIMARY KEY(note_id, tag_id))")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE schema_version (version INTEGER NOT NULL PRIMARY KEY, applied_time INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO schema_version VALUES (1, 1000)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO notes VALUES (11, 'Legacy note', 'Keep this text', 1000, 2000, 0, 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO tags VALUES (21, 'Legacy root', -1, 1000)")));
        legacyDatabase.close();
    }
    QSqlDatabase::removeDatabase(legacyConnectionName);

    const QString migratedConnectionName = QStringLiteral("legacy_schema_migrated_connection");
    DatabaseManager database(migratedConnectionName);
    QVERIFY(database.initialize(databasePath));
    QCOMPARE(database.getNote(11).content, QStringLiteral("Keep this text"));
    QCOMPARE(database.getNote(11).isEncrypted, false);
    QCOMPARE(database.getTag(21).parentId, qint64(-1));

    QSqlQuery versionQuery(QSqlDatabase::database(migratedConnectionName));
    QVERIFY(versionQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")));
    QVERIFY(versionQuery.next());
    QCOMPARE(versionQuery.value(0).toInt(), DatabaseSchema::CurrentVersion);

    QSqlQuery columnsQuery(QSqlDatabase::database(migratedConnectionName));
    QVERIFY(columnsQuery.exec(QStringLiteral("PRAGMA table_info(tags)")));
    bool hasSortOrder = false;
    while (columnsQuery.next()) {
        hasSortOrder = hasSortOrder || columnsQuery.value(QStringLiteral("name")).toString() == QStringLiteral("sort_order");
    }
    QVERIFY(hasSortOrder);
    const QStringList migrationBackups = QDir(tempDir.path()).entryList(
        {QStringLiteral("legacy-schema.db.pre-migration-*.db")}, QDir::Files);
    QCOMPARE(migrationBackups.size(), 1);
}

void CoreTests::databaseSchemaMigrationRollsBackOnFailure() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString connectionName = QStringLiteral("schema_migration_failure_connection");
    const QString databasePath = tempDir.filePath(QStringLiteral("schema-failure.db"));
    {
        QSqlDatabase seedDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        seedDatabase.setDatabaseName(databasePath);
        QVERIFY(seedDatabase.open());
        QSqlQuery query(seedDatabase);
        QVERIFY(query.exec(QStringLiteral("CREATE VIEW notes AS SELECT 1 AS id, 1 AS modified_time")));
        seedDatabase.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    DatabaseManager database(connectionName);
    QVERIFY(!database.initialize(databasePath));

    QSqlDatabase failedDatabase = QSqlDatabase::database(connectionName);
    QVERIFY(failedDatabase.isOpen());
    QSqlQuery tablesQuery(failedDatabase);
    QVERIFY(tablesQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN ('tags', 'note_tags')")));
    QVERIFY(tablesQuery.next());
    QCOMPARE(tablesQuery.value(0).toInt(), 0);

    QSqlQuery versionQuery(failedDatabase);
    QVERIFY(versionQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_version")));
    QVERIFY(versionQuery.next());
    QCOMPARE(versionQuery.value(0).toInt(), 0);

    QSqlQuery repairQuery(failedDatabase);
    QVERIFY(repairQuery.exec(QStringLiteral("DROP VIEW notes")));
    failedDatabase.close();
    QVERIFY(database.initialize(databasePath));
    QSqlQuery repairedVersionQuery(QSqlDatabase::database(connectionName));
    QVERIFY(repairedVersionQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")));
    QVERIFY(repairedVersionQuery.next());
    QCOMPARE(repairedVersionQuery.value(0).toInt(), DatabaseSchema::CurrentVersion);
}

void CoreTests::applicationContextComposesFeatureServices() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ApplicationContext context(tempDir.filePath(QStringLiteral("application-context.db")));
    QVERIFY(context.initialize());

    const qint64 tagId = context.tags().createTag(QStringLiteral("Architecture"));
    QVERIFY(tagId > 0);
    const qint64 noteId = context.notes().createNote(
        QStringLiteral("Ports"), QStringLiteral("A feature stays behind its owned port."));
    QVERIFY(noteId > 0);

    const QVector<SearchResult> results = context.search().searchNotes(QStringLiteral("owned"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().noteId, noteId);
    QCOMPARE(context.database().getTag(tagId).name, QStringLiteral("Architecture"));
}

void CoreTests::databaseBackupRestoreIsSafe() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("backup_restore_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("primary.db"))));

    Note note;
    note.title = QStringLiteral("Before restore");
    note.content = QStringLiteral("Primary database content");
    note.createdTime = QDateTime::currentDateTime();
    note.modifiedTime = note.createdTime;
    const qint64 noteId = database.insertNote(note);
    QVERIFY(noteId > 0);

    const QString backupPath = tempDir.filePath(QStringLiteral("backup.db"));
    QVERIFY(database.backupDatabase(backupPath));

    note = database.getNote(noteId);
    note.title = QStringLiteral("After backup");
    QVERIFY(database.updateNote(note));

    const QString invalidBackupPath = tempDir.filePath(QStringLiteral("invalid-backup.db"));
    QFile invalidBackup(invalidBackupPath);
    QVERIFY(invalidBackup.open(QIODevice::WriteOnly));
    QCOMPARE(invalidBackup.write("not a sqlite database"), qint64(21));
    invalidBackup.close();

    QVERIFY(!database.restoreDatabase(invalidBackupPath));
    QCOMPARE(database.getNote(noteId).title, QStringLiteral("After backup"));

    QVERIFY(database.restoreDatabase(backupPath));
    QCOMPARE(database.getNote(noteId).title, QStringLiteral("Before restore"));
}

void CoreTests::noteManagerCrudAndTrashSignals() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("note_manager_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("note-manager.db"))));
    NoteManager manager(database);

    QSignalSpy createdSpy(&manager, &NoteManager::noteCreated);
    QSignalSpy updatedSpy(&manager, &NoteManager::noteUpdated);
    QSignalSpy listChangedSpy(&manager, &NoteManager::noteListChanged);

    const QString originalContent = QStringLiteral("Private note content");
    const qint64 noteId = manager.createNote(QStringLiteral("Initial title"), originalContent);
    QVERIFY(noteId > 0);
    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(listChangedSpy.count(), 1);

    QVERIFY(manager.updateNote(noteId, QStringLiteral("Updated title"), originalContent));
    QCOMPARE(manager.getNote(noteId).title, QStringLiteral("Updated title"));
    QCOMPARE(updatedSpy.count(), 1);

    QVERIFY(manager.moveToTrash(noteId));
    QCOMPARE(manager.getTrashNotes().size(), 1);
    QVERIFY(manager.restoreFromTrash(noteId));
    QVERIFY(manager.getTrashNotes().isEmpty());

    QCOMPARE(listChangedSpy.count(), 4);
}

void CoreTests::noteManagerEncryptionSignals() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("note_manager_encryption_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("note-encryption.db"))));
    NoteManager manager(database);

    QSignalSpy encryptedSpy(&manager, &NoteManager::noteEncrypted);
    QSignalSpy decryptedSpy(&manager, &NoteManager::noteDecrypted);
    QSignalSpy listChangedSpy(&manager, &NoteManager::noteListChanged);

    const QString originalContent = QStringLiteral("Private note content");
    const qint64 noteId = manager.createNote(QStringLiteral("Private note"), originalContent);
    QVERIFY(noteId > 0);
    QCOMPARE(listChangedSpy.count(), 1);

    QVERIFY(manager.encryptNote(noteId, QStringLiteral("test-password")));
    QVERIFY(manager.isNoteEncrypted(noteId));
    QCOMPARE(encryptedSpy.count(), 1);
    QVERIFY(manager.decryptNote(noteId, QStringLiteral("test-password")));
    QVERIFY(!manager.isNoteEncrypted(noteId));
    QCOMPARE(manager.getNote(noteId).content, originalContent);
    QCOMPARE(decryptedSpy.count(), 1);
    QCOMPARE(listChangedSpy.count(), 3);

    Note legacyNote;
    legacyNote.title = QStringLiteral("Legacy encrypted note");
    legacyNote.content = QStringLiteral(
        "UpY3S+ghl/XnFnefbf06AZ65lIwLy/vhwJSbkr+hwQ+6rfEHmphfxbtAU1aezslj");
    legacyNote.createdTime = QDateTime::currentDateTime();
    legacyNote.modifiedTime = legacyNote.createdTime;
    legacyNote.isEncrypted = true;
    legacyNote.encryptionIv = QStringLiteral("AQIDBAUGBwgJCgsMDQ4PEA==");
    const qint64 legacyNoteId = database.insertNote(legacyNote);
    QVERIFY(legacyNoteId > 0);
    QVERIFY(manager.decryptNote(legacyNoteId, QStringLiteral("legacy password")));
    QCOMPARE(manager.getNote(legacyNoteId).content, QStringLiteral("legacy note content"));
}

void CoreTests::noteListModelPresentation() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("note_list_model_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("note-list.db"))));

    Tag tag;
    tag.name = QStringLiteral("Architecture");
    tag.createdTime = QDateTime::currentDateTime();
    const qint64 tagId = database.insertTag(tag);
    QVERIFY(tagId > 0);

    Note visibleNote;
    visibleNote.title = QStringLiteral("Visible note");
    visibleNote.content = QStringLiteral("Displayed in the note list");
    visibleNote.createdTime = QDateTime::currentDateTime();
    visibleNote.modifiedTime = visibleNote.createdTime;
    const qint64 visibleNoteId = database.insertNote(visibleNote);
    QVERIFY(visibleNoteId > 0);
    QVERIFY(database.addNoteTag(visibleNoteId, tagId));

    Note encryptedNote = visibleNote;
    encryptedNote.title = QStringLiteral("Hidden encrypted note");
    encryptedNote.isEncrypted = true;
    QVERIFY(database.insertNote(encryptedNote) > 0);

    NoteListModel model(database, database);
    model.refresh();

    QCOMPARE(model.columnCount(), 3);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Title"));
    QCOMPARE(model.index(0, 0).data().toString(), visibleNote.title);
    QCOMPARE(model.index(0, 0).data(Qt::UserRole).toLongLong(), visibleNoteId);
    QCOMPARE(model.index(0, 2).data().toString(), QStringLiteral("Architecture"));
}

void CoreTests::searchQueryEscaping() {
    SearchEngine search;
    QCOMPARE(search.buildFTS5Query(QStringLiteral("alpha beta")), QStringLiteral("\"alpha\" AND \"beta\""));
    QCOMPARE(search.buildFTS5Query(QStringLiteral("a\"b")), QStringLiteral("\"a\"\"b\""));
    QVERIFY(search.buildFTS5Query(QString()).isEmpty());
}

void CoreTests::tagRootsChildrenAndCascades() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("tags.db"));
    const QString legacyConnectionName = QStringLiteral("tag_legacy_seed_connection");
    {
        QSqlDatabase legacyDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), legacyConnectionName);
        legacyDatabase.setDatabaseName(databasePath);
        QVERIFY(legacyDatabase.open());

        QSqlQuery legacyQuery(legacyDatabase);
        QVERIFY(legacyQuery.exec(QStringLiteral(
            "CREATE TABLE tags ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL UNIQUE, "
            "parent_id INTEGER DEFAULT -1, "
            "sort_order INTEGER DEFAULT 0, "
            "created_time INTEGER NOT NULL, "
            "FOREIGN KEY(parent_id) REFERENCES tags(id) ON DELETE CASCADE)")));
        QVERIFY(legacyQuery.exec(QStringLiteral(
            "INSERT INTO tags(id, name, parent_id, sort_order, created_time) "
            "VALUES (7, 'Legacy root', -1, 0, 1)")));
        legacyDatabase.close();
    }
    QSqlDatabase::removeDatabase(legacyConnectionName);

    DatabaseManager database(QStringLiteral("tag_test_connection"));
    QVERIFY(database.initialize(databasePath));

    const qint64 migratedRootId = database.getTagIdByName(QStringLiteral("Legacy root"));
    QCOMPARE(migratedRootId, qint64(7));
    QCOMPARE(database.getTag(migratedRootId).parentId, qint64(-1));

    Tag root;
    root.name = QStringLiteral("Root");
    root.parentId = -1;
    root.createdTime = QDateTime::currentDateTime();
    const qint64 rootId = database.insertTag(root);
    QVERIFY2(rootId > 0, "A root tag must persist with a SQL NULL parent.");

    Tag child;
    child.name = QStringLiteral("Child");
    child.parentId = rootId;
    child.createdTime = QDateTime::currentDateTime();
    const qint64 childId = database.insertTag(child);
    QVERIFY(childId > 0);

    QCOMPARE(database.getTag(rootId).parentId, qint64(-1));
    QCOMPARE(database.getTag(childId).parentId, rootId);
    QCOMPARE(database.getTagsByParent(-1).size(), 2);
    QCOMPARE(database.getTagsByParent(rootId).size(), 1);

    Note note;
    note.title = QStringLiteral("Tagged note");
    note.content = QStringLiteral("Cascade test");
    note.createdTime = QDateTime::currentDateTime();
    note.modifiedTime = note.createdTime;
    const qint64 noteId = database.insertNote(note);
    QVERIFY(noteId > 0);
    QVERIFY(database.addNoteTag(noteId, childId));

    QVERIFY(database.deleteTag(rootId));
    QCOMPARE(database.getTag(rootId).id, qint64(-1));
    QCOMPARE(database.getTag(childId).id, qint64(-1));
    QVERIFY(database.getTagNoteIds(childId).isEmpty());
}

void CoreTests::tagTreeModelProjectsTagsAndHeadings() {
    Tag laterRoot;
    laterRoot.id = 10;
    laterRoot.name = QStringLiteral("Later root");
    laterRoot.parentId = -1;
    laterRoot.sortOrder = 2;

    Tag earlierRoot;
    earlierRoot.id = 20;
    earlierRoot.name = QStringLiteral("Earlier root");
    earlierRoot.parentId = -1;
    earlierRoot.sortOrder = 1;

    Tag child;
    child.id = 11;
    child.name = QStringLiteral("Child");
    child.parentId = laterRoot.id;

    TagTreeModel model;
    model.setTags({laterRoot, child, earlierRoot});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0).data().toString(), earlierRoot.name);
    const QModelIndex laterRootIndex = model.index(1, 0);
    QCOMPARE(laterRootIndex.data(Qt::UserRole).toLongLong(), laterRoot.id);
    QCOMPARE(model.rowCount(laterRootIndex), 1);
    QCOMPARE(model.index(0, 0, laterRootIndex).data().toString(), child.name);

    model.setHeadingsFromMarkdown(QStringLiteral(
        "# First\n## Section\n### Detail\n# Second\n## Loose section"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0).data().toString(), QStringLiteral("First"));
    const QModelIndex sectionIndex = model.index(0, 0, model.index(0, 0));
    QCOMPARE(sectionIndex.data().toString(), QStringLiteral("Section"));
    QCOMPARE(model.rowCount(sectionIndex), 1);
    QCOMPARE(model.index(0, 0, sectionIndex).data().toString(), QStringLiteral("Detail"));
    QCOMPARE(model.index(0, 0, model.index(1, 0)).data().toString(), QStringLiteral("Loose section"));
}

void CoreTests::tagMergeIsAtomicAndKeepsIndexConsistent() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("tag_merge_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("tag-merge.db"))));

    Tag source;
    source.name = QStringLiteral("Source");
    source.createdTime = QDateTime::currentDateTime();
    const qint64 sourceId = database.insertTag(source);
    QVERIFY(sourceId > 0);

    Tag target;
    target.name = QStringLiteral("Target");
    target.createdTime = QDateTime::currentDateTime();
    const qint64 targetId = database.insertTag(target);
    QVERIFY(targetId > 0);

    Tag sourceChild;
    sourceChild.name = QStringLiteral("Source child");
    sourceChild.parentId = sourceId;
    sourceChild.createdTime = QDateTime::currentDateTime();
    const qint64 sourceChildId = database.insertTag(sourceChild);
    QVERIFY(sourceChildId > 0);

    Note sharedNote;
    sharedNote.title = QStringLiteral("Shared assignment");
    sharedNote.content = QStringLiteral("tag merge");
    sharedNote.createdTime = QDateTime::currentDateTime();
    sharedNote.modifiedTime = sharedNote.createdTime;
    const qint64 sharedNoteId = database.insertNote(sharedNote);
    QVERIFY(sharedNoteId > 0);

    Note sourceOnlyNote = sharedNote;
    sourceOnlyNote.title = QStringLiteral("Source-only assignment");
    const qint64 sourceOnlyNoteId = database.insertNote(sourceOnlyNote);
    QVERIFY(sourceOnlyNoteId > 0);

    Note childTaggedNote = sharedNote;
    childTaggedNote.title = QStringLiteral("Child assignment");
    const qint64 childTaggedNoteId = database.insertNote(childTaggedNote);
    QVERIFY(childTaggedNoteId > 0);

    TagManager manager(database, database);
    QVERIFY(manager.addTagToNote(sharedNoteId, sourceId));
    QVERIFY(manager.addTagToNote(sharedNoteId, targetId));
    QVERIFY(manager.addTagToNote(sourceOnlyNoteId, sourceId));
    QVERIFY(manager.addTagToNote(childTaggedNoteId, sourceChildId));

    QVERIFY(!manager.mergeTag(sourceId, sourceId));
    QVERIFY(!manager.mergeTag(sourceId, sourceChildId));
    QVERIFY(!database.mergeTags(sourceId, std::numeric_limits<qint64>::max()));
    QCOMPARE(database.getTag(sourceId).id, sourceId);
    QCOMPARE(database.getTag(sourceChildId).id, sourceChildId);
    QCOMPARE(database.getNoteTagIds(sharedNoteId).size(), 2);

    QVERIFY(manager.mergeTag(sourceId, targetId));
    QCOMPARE(database.getTag(sourceId).id, qint64(-1));
    QCOMPARE(database.getTag(sourceChildId).id, qint64(-1));
    QVERIFY(manager.filterNotesByTag(sourceChildId).isEmpty());
    QCOMPARE(database.getNoteTagIds(sharedNoteId), QVector<qint64>{targetId});
    QCOMPARE(database.getNoteTagIds(sourceOnlyNoteId), QVector<qint64>{targetId});
    QSet<qint64> indexedTargetNotes;
    for (qint64 noteId : manager.filterNotesByTag(targetId)) {
        indexedTargetNotes.insert(noteId);
    }
    QSet<qint64> expectedTargetNotes;
    expectedTargetNotes.insert(sharedNoteId);
    expectedTargetNotes.insert(sourceOnlyNoteId);
    QCOMPARE(indexedTargetNotes, expectedTargetNotes);
    QVERIFY(manager.filterNotesByTag(sourceId).isEmpty());
}

void CoreTests::tagReorderIsTransactional() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    DatabaseManager database(QStringLiteral("tag_reorder_test_connection"));
    QVERIFY(database.initialize(tempDir.filePath(QStringLiteral("tag-reorder.db"))));

    Tag first;
    first.name = QStringLiteral("First");
    first.createdTime = QDateTime::currentDateTime();
    const qint64 firstId = database.insertTag(first);
    QVERIFY(firstId > 0);

    Tag second;
    second.name = QStringLiteral("Second");
    second.createdTime = QDateTime::currentDateTime();
    const qint64 secondId = database.insertTag(second);
    QVERIFY(secondId > 0);

    Tag destination;
    destination.name = QStringLiteral("Destination");
    destination.createdTime = QDateTime::currentDateTime();
    const qint64 destinationId = database.insertTag(destination);
    QVERIFY(destinationId > 0);

    QVERIFY(database.reorderTags({{firstId, -1, 1}, {secondId, -1, 2}, {destinationId, -1, 3}}));
    QVERIFY(!database.reorderTags({{firstId, -1, 9}, {999999, -1, 1}}));
    QCOMPARE(database.getTag(firstId).sortOrder, 1);
    QCOMPARE(database.getTag(secondId).sortOrder, 2);

    TagManager manager(database, database);
    manager.handleTagDrop(secondId, -1, 0);
    QCOMPARE(database.getTagsByParent(-1).first().id, secondId);
    QCOMPARE(database.getTag(firstId).sortOrder, 2);

    manager.handleTagDrop(secondId, destinationId, 0);
    QCOMPARE(database.getTag(secondId).parentId, destinationId);
    QCOMPARE(database.getTag(secondId).sortOrder, 1);
    QCOMPARE(database.getTag(firstId).sortOrder, 1);
}

void CoreTests::vaultPasswordLifecycle() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());

    QCoreApplication::setOrganizationName(QStringLiteral("MyBaseTests"));
    QCoreApplication::setApplicationName(QStringLiteral("VaultPasswordLifecycle"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings settings;
    settings.clear();
    settings.sync();

    VaultManager vault;
    QVERIFY(vault.initialize());
    QVERIFY(!vault.isPasswordConfigured());
    QVERIFY(!vault.verifyPassword(QStringLiteral("anything")));
    QVERIFY(!vault.setPassword(QStringLiteral("short")));
    QVERIFY(vault.setPassword(QStringLiteral("long-enough-password")));
    QCOMPARE(settings.value(QStringLiteral("vault/passwordVersion")).toInt(), 2);
    QVERIFY(vault.isPasswordConfigured());
    QVERIFY(vault.verifyPassword(QStringLiteral("long-enough-password")));
    QVERIFY(!vault.verifyPassword(QStringLiteral("wrong-password")));

    VaultManager restoredVault;
    QVERIFY(restoredVault.initialize());
    QVERIFY(restoredVault.isPasswordConfigured());
    QVERIFY(restoredVault.verifyPassword(QStringLiteral("long-enough-password")));

    const QByteArray legacySalt(32, 'L');
    QByteArray legacyHash = QCryptographicHash::hash(
        QStringLiteral("legacy-password").toUtf8() + legacySalt, QCryptographicHash::Sha256);
    for (int iteration = 1; iteration < 100000; ++iteration) {
        legacyHash = QCryptographicHash::hash(
            legacyHash + QStringLiteral("legacy-password").toUtf8() + legacySalt,
            QCryptographicHash::Sha256);
    }
    settings.setValue(QStringLiteral("vault/passwordSalt"), legacySalt.toBase64());
    settings.setValue(QStringLiteral("vault/passwordHash"), legacyHash.toBase64());
    settings.remove(QStringLiteral("vault/passwordVersion"));
    settings.sync();

    VaultManager legacyVault;
    QVERIFY(legacyVault.initialize());
    QVERIFY(!legacyVault.verifyPassword(QStringLiteral("wrong-password")));
    QCOMPARE(settings.value(QStringLiteral("vault/passwordVersion"), 1).toInt(), 1);
    QVERIFY(legacyVault.verifyPassword(QStringLiteral("legacy-password")));
    QCOMPARE(settings.value(QStringLiteral("vault/passwordVersion")).toInt(), 2);

    VaultManager migratedVault;
    QVERIFY(migratedVault.initialize());
    QVERIFY(migratedVault.verifyPassword(QStringLiteral("legacy-password")));
}

QTEST_MAIN(CoreTests)
#include "test_core.moc"
