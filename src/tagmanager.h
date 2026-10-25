#ifndef TAGMANAGER_H
#define TAGMANAGER_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QVector>
#include "repositories.h"

class TagManager : public QObject {
    Q_OBJECT

public:
    explicit TagManager(ITagRepository& tagRepository,
                        INoteTagRepository& noteTagRepository,
                        QObject* parent = nullptr);

    qint64 createTag(const QString& name, qint64 parentId = -1);
    bool updateTag(qint64 tagId, const QString& name);
    bool deleteTag(qint64 tagId);
    bool mergeTag(qint64 sourceTagId, qint64 targetTagId);
    bool updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder);

    bool addTagToNote(qint64 noteId, qint64 tagId);
    bool removeTagFromNote(qint64 noteId, qint64 tagId);
    QVector<Tag> getNoteTags(qint64 noteId);

    QVector<qint64> filterNotesByTag(qint64 tagId);
    QVector<qint64> filterNotesByTagsAND(const QVector<qint64>& tagIds);
    QVector<qint64> filterNotesByTagsOR(const QVector<qint64>& tagIds);

    QVector<Tag> allTags() const;
    QVector<qint64> tagIdsForNote(qint64 noteId) const;
    qint64 findTagIdByName(const QString& name) const;
    
    void handleTagDrop(qint64 tagId, qint64 newParentId, int row);

    void rebuildInvertedIndex();

signals:
    void tagCreated(qint64 tagId);
    void tagUpdated(qint64 tagId);
    void tagDeleted(qint64 tagId);
    void tagTreeChanged();
    void tagDataChanged();

private:
    ITagRepository& tagRepository_;
    INoteTagRepository& noteTagRepository_;
    QHash<qint64, QSet<qint64>> invertedIndex_;
    QMutex indexMutex_;

    void buildInvertedIndex();
    void notifyTagTreeChanged();
};

#endif // TAGMANAGER_H
