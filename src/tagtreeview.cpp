#include "tagtreeview.h"

TagTreeView::TagTreeView(QWidget* parent)
    : QTreeView(parent), tagManager_(nullptr), tagTreeModel_(new TagTreeModel(this)), currentTagId_(-1) {
    setDragDropMode(QAbstractItemView::InternalMove);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHeaderHidden(true);
    setExpandsOnDoubleClick(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setDefaultDropAction(Qt::MoveAction);
    setModel(tagTreeModel_);

    setupContextMenu();
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        QVector<qint64> tagIds;
        for (const QModelIndex& selected : selectionModel()->selectedRows()) {
            const qint64 id = getTagIdFromIndex(selected);
            if (id > 0 && !tagIds.contains(id)) {
                tagIds.append(id);
            }
        }
        emit tagSelectionChanged(tagIds);
    });
}

void TagTreeView::setTagManager(TagManager* tagManager) {
    if (tagManager_) {
        disconnect(tagManager_, &TagManager::tagTreeChanged, this, nullptr);
    }
    tagManager_ = tagManager;
    if (tagManager_) {
        connect(tagManager_, &TagManager::tagTreeChanged, this, &TagTreeView::showTagHierarchy);
        showTagHierarchy();
    } else {
        tagTreeModel_->setTags({});
    }
}

void TagTreeView::showTagHierarchy() {
    tagTreeModel_->setTags(tagManager_ ? tagManager_->allTags() : QVector<Tag>{});
}

void TagTreeView::showNoteHeadings(const QString& markdown) {
    tagTreeModel_->setHeadingsFromMarkdown(markdown);
}

void TagTreeView::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndex index = indexAt(event->pos());
    currentTagId_ = getTagIdFromIndex(index);

    actionRenameTag_->setEnabled(currentTagId_ != -1);
    actionDeleteTag_->setEnabled(currentTagId_ != -1);
    actionMergeTag_->setEnabled(currentTagId_ != -1);
    actionNewSubTag_->setEnabled(currentTagId_ != -1);

    contextMenu_->exec(event->globalPos());
}

void TagTreeView::mousePressEvent(QMouseEvent* event) {
    const QModelIndex clickedIndex = indexAt(event->pos());
    QTreeView::mousePressEvent(event);
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (clickedIndex.isValid()) {
        emit tagSelected(getTagIdFromIndex(clickedIndex));
    } else {
        clearSelection();
        emit tagDeselected();
    }
}

void TagTreeView::dragEnterEvent(QDragEnterEvent* event) {
    QTreeView::dragEnterEvent(event);
}

void TagTreeView::dragMoveEvent(QDragMoveEvent* event) {
    QTreeView::dragMoveEvent(event);
}

void TagTreeView::dropEvent(QDropEvent* event) {
    if (!tagManager_) {
        event->ignore();
        return;
    }

    QModelIndex targetIndex = indexAt(event->pos());
    qint64 draggedTagId = -1;
    
    QModelIndexList selectedIndexes = selectionModel()->selectedIndexes();
    if (!selectedIndexes.isEmpty()) {
        draggedTagId = getTagIdFromIndex(selectedIndexes.first());
    }
    
    if (draggedTagId <= 0) {
        event->ignore();
        return;
    }

    qint64 targetTagId = -1;
    qint64 targetParentId = -1;
    
    if (targetIndex.isValid()) {
        targetTagId = getTagIdFromIndex(targetIndex);
        targetParentId = targetIndex.data(Qt::UserRole + 1).toLongLong();
    }

    if (draggedTagId == targetTagId) {
        event->ignore();
        return;
    }

    DropIndicatorPosition dropPos = dropIndicatorPosition();
    
    qint64 newParentId = -1;
    int insertRow = 0;
    
    if (!targetIndex.isValid()) {
        newParentId = -1;
        insertRow = -1;
    } else if (dropPos == QAbstractItemView::AboveItem) {
        newParentId = targetParentId;
        insertRow = targetIndex.row();
    } else if (dropPos == QAbstractItemView::BelowItem) {
        newParentId = targetParentId;
        insertRow = targetIndex.row() + 1;
    } else {
        newParentId = targetTagId;
        insertRow = -1;
    }

    tagManager_->handleTagDrop(draggedTagId, newParentId, insertRow);
    
    event->accept();
}

void TagTreeView::setupContextMenu() {
    contextMenu_ = new QMenu(this);

    actionNewRootTag_ = new QAction(QStringLiteral("新建根标签"), this);
    actionNewSubTag_ = new QAction(QStringLiteral("新建子标签"), this);
    actionRenameTag_ = new QAction(QStringLiteral("重命名"), this);
    actionDeleteTag_ = new QAction(QStringLiteral("删除"), this);
    actionMergeTag_ = new QAction(QStringLiteral("合并标签"), this);

    connect(actionNewRootTag_, &QAction::triggered, this, &TagTreeView::onNewRootTag);
    connect(actionNewSubTag_, &QAction::triggered, this, &TagTreeView::onNewSubTag);
    connect(actionRenameTag_, &QAction::triggered, this, &TagTreeView::onRenameTag);
    connect(actionDeleteTag_, &QAction::triggered, this, &TagTreeView::onDeleteTag);
    connect(actionMergeTag_, &QAction::triggered, this, &TagTreeView::onMergeTag);

    contextMenu_->addAction(actionNewRootTag_);
    contextMenu_->addAction(actionNewSubTag_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(actionRenameTag_);
    contextMenu_->addAction(actionDeleteTag_);
    contextMenu_->addAction(actionMergeTag_);
}

qint64 TagTreeView::getTagIdFromIndex(const QModelIndex& index) const {
    if (index.isValid() && index.model()) {
        QVariant data = index.data(Qt::UserRole);
        if (data.isValid()) {
            return data.toLongLong();
        }
    }
    return -1;
}

QString TagTreeView::getTagNameFromIndex(const QModelIndex& index) const {
    if (index.isValid() && index.model()) {
        return index.data(Qt::DisplayRole).toString();
    }
    return QString();
}

void TagTreeView::onNewRootTag() {
    bool ok;
    QString name = QInputDialog::getText(this, QStringLiteral("新建根标签"),
                                         QStringLiteral("标签名称:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        tagManager_->createTag(name.trimmed(), -1);
    }
}

void TagTreeView::onNewSubTag() {
    if (currentTagId_ == -1) {
        return;
    }

    bool ok;
    QString name = QInputDialog::getText(this, QStringLiteral("新建子标签"),
                                         QStringLiteral("标签名称:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        tagManager_->createTag(name.trimmed(), currentTagId_);
    }
}

void TagTreeView::onRenameTag() {
    if (currentTagId_ == -1) {
        return;
    }

    QString currentName = getTagNameFromIndex(selectionModel()->currentIndex());
    bool ok;
    QString newName = QInputDialog::getText(this, QStringLiteral("重命名标签"),
                                            QStringLiteral("标签名称:"), QLineEdit::Normal, currentName, &ok);
    if (ok && !newName.trimmed().isEmpty() && newName != currentName) {
        tagManager_->updateTag(currentTagId_, newName.trimmed());
    }
}

void TagTreeView::onDeleteTag() {
    if (currentTagId_ == -1) {
        return;
    }

    QString tagName = getTagNameFromIndex(selectionModel()->currentIndex());
    int result = QMessageBox::question(this, QStringLiteral("删除标签"),
                                       QStringLiteral("确定要删除标签 \"%1\" 吗？\n此操作不可撤销。").arg(tagName),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result == QMessageBox::Yes) {
        tagManager_->deleteTag(currentTagId_);
    }
}

void TagTreeView::onMergeTag() {
    if (currentTagId_ == -1) {
        return;
    }

    QStandardItemModel* model = tagTreeModel_;
    QStringList tagNames;
    QHash<QString, qint64> nameToId;

    for (int i = 0; i < model->rowCount(); ++i) {
        QModelIndex idx = model->index(i, 0);
        collectTagNames(idx, tagNames, nameToId);
    }

    tagNames.removeAll(getTagNameFromIndex(selectionModel()->currentIndex()));
    if (tagNames.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("合并标签"), QStringLiteral("没有其他标签可供合并。"));
        return;
    }

    bool ok;
    QString targetName = QInputDialog::getItem(this, QStringLiteral("合并标签"),
                                               QStringLiteral("选择目标标签:"), tagNames, 0, false, &ok);
    if (ok && !targetName.isEmpty()) {
        qint64 targetId = nameToId.value(targetName, -1);
        if (targetId != -1) {
            tagManager_->mergeTag(currentTagId_, targetId);
        }
    }
}

void TagTreeView::collectTagNames(const QModelIndex& index, QStringList& names, QHash<QString, qint64>& nameToId) {
    if (!index.isValid()) {
        return;
    }

    qint64 tagId = getTagIdFromIndex(index);
    QString tagName = index.data(Qt::DisplayRole).toString();
    if (tagId != -1 && !tagName.isEmpty()) {
        names.append(tagName);
        nameToId[tagName] = tagId;
    }

    QStandardItemModel* model = tagTreeModel_;
    for (int i = 0; i < model->rowCount(index); ++i) {
        collectTagNames(model->index(i, 0, index), names, nameToId);
    }
}
