#ifndef MYBASE_REPOSITORIES_H
#define MYBASE_REPOSITORIES_H

#include <QSet>
#include <QString>
#include <QVector>

#include "note.h"
#include "searchresult.h"
#include "tag.h"

enum class TagMatchMode {
    And,
    Or
};

struct TagOrderUpdate {
    qint64 tagId = -1;
    qint64 parentId = -1;
    int sortOrder = 0;
};

// Application-facing ports. Concrete storage and indexing implementations live
// outside the application services that consume these interfaces.
class INoteRepository {
public:
    virtual ~INoteRepository() = default;

    virtual qint64 insertNote(const Note& note) = 0;
    virtual bool updateNote(const Note& note) = 0;
    virtual bool softDeleteNote(qint64 noteId) = 0;
    virtual bool permanentlyDeleteNote(qint64 noteId) = 0;
    virtual bool cleanupTrash(int daysThreshold) = 0;
    virtual Note getNote(qint64 noteId) = 0;
    virtual QVector<Note> getAllNotes(bool includeDeleted = false) = 0;
    virtual QVector<Note> getEncryptedNotes() = 0;
    virtual QVector<Note> getTrashNotes() = 0;
};

class ITagRepository {
public:
    virtual ~ITagRepository() = default;

    virtual qint64 insertTag(const Tag& tag) = 0;
    virtual bool updateTag(const Tag& tag) = 0;
    virtual bool deleteTag(qint64 tagId) = 0;
    virtual bool mergeTags(qint64 sourceTagId, qint64 targetTagId) = 0;
    virtual Tag getTag(qint64 tagId) = 0;
    virtual QVector<Tag> getAllTags() = 0;
    virtual qint64 getTagIdByName(const QString& name) = 0;
    virtual bool updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder) = 0;
    virtual bool reorderTags(const QVector<TagOrderUpdate>& updates) = 0;
    virtual QVector<Tag> getTagsByParent(qint64 parentId) = 0;
};

class INoteTagRepository {
public:
    virtual ~INoteTagRepository() = default;

    virtual bool addNoteTag(qint64 noteId, qint64 tagId) = 0;
    virtual bool removeNoteTag(qint64 noteId, qint64 tagId) = 0;
    virtual QVector<qint64> getNoteTagIds(qint64 noteId) = 0;
    virtual QVector<qint64> getTagNoteIds(qint64 tagId) = 0;
    virtual QVector<Tag> getTagsForNote(qint64 noteId) = 0;
};

class IKnowledgeRepository : public INoteRepository,
                             public ITagRepository,
                             public INoteTagRepository {
public:
    ~IKnowledgeRepository() override = default;
};

class ISearchRepository {
public:
    virtual ~ISearchRepository() = default;

    virtual QVector<SearchResult> searchNotes(const QString& ftsQuery, int snippetContextLength) = 0;
    virtual QVector<SearchResult> searchNotesByTags(const QString& ftsQuery,
                                                    const QSet<qint64>& tagIds,
                                                    TagMatchMode matchMode,
                                                    int snippetContextLength) = 0;
};

#endif // MYBASE_REPOSITORIES_H

