#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QString>
#include <QVector>
#include "note.h"
#include "tag.h"
#include "repositories.h"
#include "sqliterepositories.h"

class DatabaseManager : public IKnowledgeRepository, public ISearchRepository {
public:
    explicit DatabaseManager(const QString& connectionName = QLatin1String("default"));
    ~DatabaseManager();

    bool initialize(const QString& dbPath);
    bool createTables();
    bool createFTS5Index();

    bool backupDatabase(const QString& backupPath);
    bool restoreDatabase(const QString& backupPath);

    QSqlDatabase& connection() { return db_; }

    INoteRepository& noteRepository() { return noteRepository_; }
    ITagRepository& tagRepository() { return tagRepository_; }
    INoteTagRepository& noteTagRepository() { return noteTagRepository_; }
    ISearchRepository& searchRepository() { return searchRepository_; }

    qint64 insertNote(const Note& note) override;
    bool updateNote(const Note& note) override;
    bool softDeleteNote(qint64 noteId) override;
    bool permanentlyDeleteNote(qint64 noteId) override;
    bool cleanupTrash(int daysThreshold) override;
    Note getNote(qint64 noteId) override;
    QVector<Note> getAllNotes(bool includeDeleted = false) override;
    QVector<Note> getEncryptedNotes() override;
    QVector<Note> getTrashNotes() override;

    qint64 insertTag(const Tag& tag) override;
    bool updateTag(const Tag& tag) override;
    bool deleteTag(qint64 tagId) override;
    bool mergeTags(qint64 sourceTagId, qint64 targetTagId) override;
    Tag getTag(qint64 tagId) override;
    QVector<Tag> getAllTags() override;
    qint64 getTagIdByName(const QString& name) override;
    bool updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder) override;
    bool reorderTags(const QVector<TagOrderUpdate>& updates) override;
    QVector<Tag> getTagsByParent(qint64 parentId) override;

    bool addNoteTag(qint64 noteId, qint64 tagId) override;
    bool removeNoteTag(qint64 noteId, qint64 tagId) override;
    QVector<qint64> getNoteTagIds(qint64 noteId) override;
    QVector<qint64> getTagNoteIds(qint64 tagId) override;
    QVector<Tag> getTagsForNote(qint64 noteId) override;

    QVector<SearchResult> searchNotes(const QString& ftsQuery, int snippetContextLength) override;
    QVector<SearchResult> searchNotesByTags(const QString& ftsQuery,
                                            const QSet<qint64>& tagIds,
                                            TagMatchMode matchMode,
                                            int snippetContextLength) override;

private:
    bool createTablesInternal();
    bool createFTS5IndexInternal();
    bool configureConnection();

    QString connectionName_;
    QString databasePath_;
    QSqlDatabase db_;
    QMutex dbMutex_;
    SqliteNoteRepository noteRepository_;
    SqliteTagRepository tagRepository_;
    SqliteNoteTagRepository noteTagRepository_;
    SqliteSearchRepository searchRepository_;
    bool initialized_ = false;
};

#endif // DATABASEMANAGER_H

