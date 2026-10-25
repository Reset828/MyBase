#include "tagmanager.h"
#include <QDateTime>
#include <QMutexLocker>
#include <algorithm>

namespace {
QSet<qint64> tagSubtreeIds(const QVector<Tag>& tags, qint64 rootTagId) {
    QSet<qint64> subtreeIds{rootTagId};
    bool foundDescendant = true;
    while (foundDescendant) {
        foundDescendant = false;
        for (const Tag& tag : tags) {
            if (!subtreeIds.contains(tag.id) && subtreeIds.contains(tag.parentId)) {
                subtreeIds.insert(tag.id);
                foundDescendant = true;
            }
        }
    }
    return subtreeIds;
}
}

TagManager::TagManager(ITagRepository& tagRepository,
                       INoteTagRepository& noteTagRepository,
                       QObject* parent)
    : QObject(parent), tagRepository_(tagRepository), noteTagRepository_(noteTagRepository) {
    rebuildInvertedIndex();
    notifyTagTreeChanged();
}

qint64 TagManager::createTag(const QString& name, qint64 parentId) {
    QVector<Tag> siblings = tagRepository_.getTagsByParent(parentId);
    int maxSortOrder = 0;
    for (const Tag& sibling : siblings) {
        if (sibling.sortOrder > maxSortOrder) {
            maxSortOrder = sibling.sortOrder;
        }
    }

    Tag tag;
    tag.name = name;
    tag.parentId = parentId;
    tag.sortOrder = maxSortOrder + 1;
    tag.createdTime = QDateTime::currentDateTime();

    qint64 tagId = tagRepository_.insertTag(tag);
    if (tagId > 0) {
        {
            QMutexLocker locker(&indexMutex_);
            invertedIndex_[tagId] = QSet<qint64>();
        }
        notifyTagTreeChanged();
        emit tagCreated(tagId);
    }
    return tagId;
}

bool TagManager::updateTag(qint64 tagId, const QString& name) {
    Tag tag = tagRepository_.getTag(tagId);
    if (tag.id == -1) {
        return false;
    }

    tag.name = name;
    bool result = tagRepository_.updateTag(tag);
    if (result) {
        notifyTagTreeChanged();
        emit tagUpdated(tagId);
    }
    return result;
}

bool TagManager::deleteTag(qint64 tagId) {
    const QVector<Tag> allTags = tagRepository_.getAllTags();
    const QSet<qint64> deletedTagIds = tagSubtreeIds(allTags, tagId);
    if (std::none_of(allTags.cbegin(), allTags.cend(), [tagId](const Tag& tag) {
            return tag.id == tagId;
        })) {
        return false;
    }

    if (!tagRepository_.deleteTag(tagId)) {
        return false;
    }
    {
        QMutexLocker locker(&indexMutex_);
        for (qint64 deletedTagId : deletedTagIds) {
            invertedIndex_.remove(deletedTagId);
        }
    }
    notifyTagTreeChanged();
    emit tagDeleted(tagId);
    return true;
}

bool TagManager::mergeTag(qint64 sourceTagId, qint64 targetTagId) {
    if (sourceTagId == targetTagId) {
        return false;
    }

    const QVector<Tag> allTags = tagRepository_.getAllTags();
    const QSet<qint64> sourceSubtreeIds = tagSubtreeIds(allTags, sourceTagId);
    if (!sourceSubtreeIds.contains(sourceTagId) ||
        !std::any_of(allTags.cbegin(), allTags.cend(), [targetTagId](const Tag& tag) {
            return tag.id == targetTagId;
        }) || sourceSubtreeIds.contains(targetTagId)) {
        return false;
    }

    QMutexLocker locker(&indexMutex_);
    const QSet<qint64> sourceNotes = invertedIndex_.value(sourceTagId);
    if (!tagRepository_.mergeTags(sourceTagId, targetTagId)) {
        return false;
    }

    invertedIndex_[targetTagId].unite(sourceNotes);
    for (qint64 deletedTagId : sourceSubtreeIds) {
        invertedIndex_.remove(deletedTagId);
    }
    locker.unlock();

    notifyTagTreeChanged();
    emit tagDeleted(sourceTagId);
    emit tagUpdated(targetTagId);
    return true;
}

bool TagManager::updateTagOrder(qint64 tagId, qint64 parentId, int sortOrder) {
    bool result = tagRepository_.updateTagOrder(tagId, parentId, sortOrder);
    if (result) {
        notifyTagTreeChanged();
        emit tagUpdated(tagId);
    }
    return result;
}

void TagManager::handleTagDrop(qint64 tagId, qint64 newParentId, int row) {
    Tag tag = tagRepository_.getTag(tagId);
    if (tag.id == -1) {
        return;
    }

    if (tag.id == newParentId) {
        return;
    }

    if (newParentId != -1 && tagRepository_.getTag(newParentId).id == -1) {
        return;
    }

    if (newParentId != -1) {
        qint64 checkId = newParentId;
        QSet<qint64> visited;
        while (checkId != -1) {
            if (checkId == tag.id || visited.contains(checkId)) {
                return;
            }
            visited.insert(checkId);
            Tag checkTag = tagRepository_.getTag(checkId);
            if (checkTag.id == -1) {
                return;
            }
            checkId = checkTag.parentId;
        }
    }

    QVector<Tag> targetSiblings = tagRepository_.getTagsByParent(newParentId);
    if (tag.parentId == newParentId) {
        int oldIndex = -1;
        for (int index = 0; index < targetSiblings.size(); ++index) {
            if (targetSiblings[index].id == tagId) {
                oldIndex = index;
                break;
            }
        }
        if (oldIndex >= 0 && row >= 0) {
            if (oldIndex < row) {
                row--;
            }
        }
    }

    targetSiblings.erase(std::remove_if(targetSiblings.begin(), targetSiblings.end(),
                                        [tagId](const Tag& sibling) { return sibling.id == tagId; }),
                         targetSiblings.end());
    if (row < 0 || row > targetSiblings.size()) {
        row = targetSiblings.size();
    }
    targetSiblings.insert(row, tag);

    QVector<TagOrderUpdate> updates;
    updates.reserve(targetSiblings.size());
    for (int index = 0; index < targetSiblings.size(); ++index) {
        updates.append({targetSiblings[index].id, newParentId, index + 1});
    }

    if (tag.parentId != newParentId) {
        const QVector<Tag> sourceSiblings = tagRepository_.getTagsByParent(tag.parentId);
        int sourceOrder = 1;
        for (const Tag& sibling : sourceSiblings) {
            if (sibling.id != tagId) {
                updates.append({sibling.id, tag.parentId, sourceOrder++});
            }
        }
    }

    if (tagRepository_.reorderTags(updates)) {
        notifyTagTreeChanged();
    }
}

bool TagManager::addTagToNote(qint64 noteId, qint64 tagId) {
    bool result = noteTagRepository_.addNoteTag(noteId, tagId);
    if (result) {
        {
            QMutexLocker locker(&indexMutex_);
            if (!invertedIndex_.contains(tagId)) {
                invertedIndex_[tagId] = QSet<qint64>();
            }
            invertedIndex_[tagId].insert(noteId);
        }
        emit tagDataChanged();
    }
    return result;
}

bool TagManager::removeTagFromNote(qint64 noteId, qint64 tagId) {
    bool result = noteTagRepository_.removeNoteTag(noteId, tagId);
    if (result) {
        {
            QMutexLocker locker(&indexMutex_);
            if (invertedIndex_.contains(tagId)) {
                invertedIndex_[tagId].remove(noteId);
            }
        }
        emit tagDataChanged();
    }
    return result;
}

QVector<Tag> TagManager::getNoteTags(qint64 noteId) {
    return noteTagRepository_.getTagsForNote(noteId);
}

QVector<Tag> TagManager::allTags() const {
    return tagRepository_.getAllTags();
}

QVector<qint64> TagManager::tagIdsForNote(qint64 noteId) const {
    return noteTagRepository_.getNoteTagIds(noteId);
}

qint64 TagManager::findTagIdByName(const QString& name) const {
    return tagRepository_.getTagIdByName(name);
}

QVector<qint64> TagManager::filterNotesByTag(qint64 tagId) {
    QMutexLocker locker(&indexMutex_);
    if (!invertedIndex_.contains(tagId)) {
        return QVector<qint64>();
    }
    QSet<qint64> result = invertedIndex_[tagId];
    return QVector<qint64>(result.begin(), result.end());
}

QVector<qint64> TagManager::filterNotesByTagsAND(const QVector<qint64>& tagIds) {
    QMutexLocker locker(&indexMutex_);
    if (tagIds.isEmpty()) {
        return QVector<qint64>();
    }

    QSet<qint64> result;
    bool first = true;
    for (qint64 tagId : tagIds) {
        if (!invertedIndex_.contains(tagId)) {
            return QVector<qint64>();
        }
        if (first) {
            result = invertedIndex_[tagId];
            first = false;
        } else {
            result.intersect(invertedIndex_[tagId]);
        }
    }

    return QVector<qint64>(result.begin(), result.end());
}

QVector<qint64> TagManager::filterNotesByTagsOR(const QVector<qint64>& tagIds) {
    QMutexLocker locker(&indexMutex_);
    QSet<qint64> result;
    for (qint64 tagId : tagIds) {
        if (invertedIndex_.contains(tagId)) {
            result.unite(invertedIndex_[tagId]);
        }
    }
    return QVector<qint64>(result.begin(), result.end());
}

void TagManager::notifyTagTreeChanged() {
    emit tagTreeChanged();
    emit tagDataChanged();
}

void TagManager::rebuildInvertedIndex() {
    QMutexLocker locker(&indexMutex_);
    invertedIndex_.clear();
    buildInvertedIndex();
}

void TagManager::buildInvertedIndex() {
    QVector<Tag> allTags = tagRepository_.getAllTags();
    for (const Tag& tag : allTags) {
        QVector<qint64> noteIds = noteTagRepository_.getTagNoteIds(tag.id);
        invertedIndex_[tag.id] = QSet<qint64>(noteIds.begin(), noteIds.end());
    }
}

