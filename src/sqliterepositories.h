#ifndef SQLITEREPOSITORIES_H
#define SQLITEREPOSITORIES_H

#include <QMutex>
#include <QSqlDatabase>
#include <QString>

#include "repositories.h"

class SqliteRepositoryAdapterBase {
protected:
    SqliteRepositoryAdapterBase(QSqlDatabase& database, QMutex& mutex)
        : db_(database), dbMutex_(mutex) {
    }

    QSqlDatabase& db_;
    QMutex& dbMutex_;
};

class SqliteNoteRepository final : private SqliteRepositoryAdapterBase,
                                   public INoteRepository {
public:
    SqliteNoteRepository(QSqlDatabase& database, QMutex& mutex)
        : SqliteRepositoryAdapterBase(database, mutex) {
    }

    qint64 insertNote(const Note& note) override;
    bool updateNote(const Note& note) override;
    bool softDeleteNote(qint64 noteId) override;
    bool permanentlyDeleteNote(qint64 noteId) override;
    bool cleanupTrash(int daysThreshold) override;
    Note getNote(qint64 noteId) override;
    QVector<Note> getAllNotes(bool includeDeleted = false) override;
    QVector<Note> getEncryptedNotes() override;
    QVector<Note> getTrashNotes() override;
};

class SqliteTagRepository final : private SqliteRepositoryAdapterBase,
                                  public ITagRepository {
public:
    SqliteTagRepository(QSqlDatabase& database, QMutex& mutex)
        : SqliteRepositoryAdapterBase(database, mutex) {
    }

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
};

class SqliteNoteTagRepository final : private SqliteRepositoryAdapterBase,
                                      public INoteTagRepository {
public:
    SqliteNoteTagRepository(QSqlDatabase& database, QMutex& mutex)
        : SqliteRepositoryAdapterBase(database, mutex) {
    }

    bool addNoteTag(qint64 noteId, qint64 tagId) override;
    bool removeNoteTag(qint64 noteId, qint64 tagId) override;
    QVector<qint64> getNoteTagIds(qint64 noteId) override;
    QVector<qint64> getTagNoteIds(qint64 tagId) override;
    QVector<Tag> getTagsForNote(qint64 noteId) override;
};

class SqliteSearchRepository final : private SqliteRepositoryAdapterBase,
                                     public ISearchRepository {
public:
    SqliteSearchRepository(QSqlDatabase& database, QMutex& mutex, QString& databasePath)
        : SqliteRepositoryAdapterBase(database, mutex), databasePath_(databasePath) {
    }

    QVector<SearchResult> searchNotes(const QString& ftsQuery, int snippetContextLength) override;
    QVector<SearchResult> searchNotesByTags(const QString& ftsQuery,
                                            const QSet<qint64>& tagIds,
                                            TagMatchMode matchMode,
                                            int snippetContextLength) override;

private:
    QString& databasePath_;
};

#endif // SQLITEREPOSITORIES_H

