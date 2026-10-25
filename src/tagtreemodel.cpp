#include "tagtreemodel.h"

#include <QHash>
#include <QStandardItem>
#include <algorithm>
#include <functional>

const QString TagTreeModel::TAG_MIME_TYPE = QStringLiteral("application/x-mybase-tag");

TagTreeModel::TagTreeModel(QObject* parent)
    : QStandardItemModel(parent) {
}

void TagTreeModel::setTags(const QVector<Tag>& tags) {
    removeRows(0, rowCount());

    QHash<qint64, QVector<Tag>> tagsByParent;
    for (const Tag& tag : tags) {
        tagsByParent[tag.parentId].append(tag);
    }
    for (auto it = tagsByParent.begin(); it != tagsByParent.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), [](const Tag& left, const Tag& right) {
            if (left.sortOrder != right.sortOrder) {
                return left.sortOrder < right.sortOrder;
            }
            return left.name < right.name;
        });
    }

    std::function<void(qint64, QStandardItem*)> appendChildren = [&](qint64 parentId, QStandardItem* parentItem) {
        const auto siblings = tagsByParent.constFind(parentId);
        if (siblings == tagsByParent.cend()) {
            return;
        }

        for (const Tag& tag : siblings.value()) {
            auto* item = new QStandardItem(tag.name);
            item->setData(tag.id, Qt::UserRole);
            item->setData(tag.parentId, Qt::UserRole + 1);
            item->setData(tag.sortOrder, Qt::UserRole + 2);
            if (parentItem) {
                parentItem->appendRow(item);
            } else {
                appendRow(item);
            }
            appendChildren(tag.id, item);
        }
    };
    appendChildren(-1, nullptr);
}

void TagTreeModel::setHeadingsFromMarkdown(const QString& markdown) {
    removeRows(0, rowCount());

    QStandardItem* lastLevel1 = nullptr;
    QStandardItem* lastLevel2 = nullptr;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        QString headingText;
        int level = 0;
        if (trimmed.startsWith(QStringLiteral("### "))) {
            headingText = trimmed.mid(4);
            level = 3;
        } else if (trimmed.startsWith(QStringLiteral("## "))) {
            headingText = trimmed.mid(3);
            level = 2;
        } else if (trimmed.startsWith(QStringLiteral("# "))) {
            headingText = trimmed.mid(2);
            level = 1;
        }
        if (headingText.isEmpty()) {
            continue;
        }

        auto* item = new QStandardItem(headingText);
        if (level == 1) {
            appendRow(item);
            lastLevel1 = item;
            lastLevel2 = nullptr;
        } else if (level == 2) {
            if (lastLevel1) {
                lastLevel1->appendRow(item);
            } else {
                appendRow(item);
            }
            lastLevel2 = item;
        } else if (lastLevel2) {
            lastLevel2->appendRow(item);
        } else if (lastLevel1) {
            lastLevel1->appendRow(item);
        } else {
            appendRow(item);
        }
    }
}

Qt::ItemFlags TagTreeModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags defaultFlags = QStandardItemModel::flags(index);
    
    if (index.isValid()) {
        return defaultFlags | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    } else {
        return defaultFlags | Qt::ItemIsDropEnabled;
    }
}

QStringList TagTreeModel::mimeTypes() const {
    return QStringList() << TAG_MIME_TYPE;
}

QMimeData* TagTreeModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = new QMimeData();
    QByteArray encodedData;
    
    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            qint64 tagId = index.data(Qt::UserRole).toLongLong();
            encodedData.append(QString::number(tagId).toUtf8() + "\n");
        }
    }
    
    mimeData->setData(TAG_MIME_TYPE, encodedData);
    return mimeData;
}

bool TagTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                int row, int column, const QModelIndex& parent) {
    Q_UNUSED(data);
    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return false;
}

Qt::DropActions TagTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}

Qt::DropActions TagTreeModel::supportedDragActions() const {
    return Qt::MoveAction;
}
