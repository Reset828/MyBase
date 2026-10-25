#include "tagselectiondialog.h"
#include <QDialogButtonBox>
#include <QLabel>

TagSelectionDialog::TagSelectionDialog(TagManager* tagManager, qint64 noteId, QWidget* parent)
    : QDialog(parent), tagManager_(tagManager), noteId_(noteId), tagModel_(nullptr) {
    setWindowTitle("Select Tags");
    resize(300, 400);

    setupUI();
    loadTags();
    setupConnections();
}

void TagSelectionDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* filterLabel = new QLabel("Filter tags:");
    mainLayout->addWidget(filterLabel);

    filterLineEdit_ = new QLineEdit();
    filterLineEdit_->setPlaceholderText("Type to filter tags...");
    mainLayout->addWidget(filterLineEdit_);

    tagTreeView_ = new QTreeView();
    tagTreeView_->setHeaderHidden(true);
    tagTreeView_->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(tagTreeView_);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    okButton_ = buttonBox->button(QDialogButtonBox::Ok);
    cancelButton_ = buttonBox->button(QDialogButtonBox::Cancel);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &TagSelectionDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &TagSelectionDialog::onCancelClicked);

    mainLayout->addWidget(buttonBox);
}

void TagSelectionDialog::loadTags() {
    tagModel_ = new QStandardItemModel(this);
    tagModel_->setHorizontalHeaderLabels({"Tags"});

    QVector<Tag> allTags = tagManager_->allTags();

    QHash<qint64, QStandardItem*> tagItems;
    for (const Tag& tag : allTags) {
        QStandardItem* item = new QStandardItem(tag.name);
        item->setData(tag.id, Qt::UserRole);
        item->setData(tag.parentId, Qt::UserRole + 1);
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);
        tagItems[tag.id] = item;
    }

    QVector<qint64> currentTagIds = tagManager_->tagIdsForNote(noteId_);
    for (qint64 tagId : currentTagIds) {
        if (tagItems.contains(tagId)) {
            tagItems[tagId]->setCheckState(Qt::Checked);
        }
    }

    for (const Tag& tag : allTags) {
        QStandardItem* item = tagItems[tag.id];
        if (tag.parentId == -1 || !tagItems.contains(tag.parentId)) {
            tagModel_->appendRow(item);
        } else {
            tagItems[tag.parentId]->appendRow(item);
        }
    }

    tagTreeView_->setModel(tagModel_);
}

void TagSelectionDialog::setupConnections() {
    connect(filterLineEdit_, &QLineEdit::textChanged, this, &TagSelectionDialog::onFilterTextChanged);
}

void TagSelectionDialog::onFilterTextChanged(const QString& text) {
    if (!tagModel_) {
        return;
    }

    QString filterText = text.toLower();
    tagTreeView_->setUpdatesEnabled(false);
    filterTreeByTagModel(tagModel_->invisibleRootItem(), filterText);
    tagTreeView_->setUpdatesEnabled(true);
}

void TagSelectionDialog::filterTreeByTagModel(QStandardItem* item, const QString& filterText) {
    if (!item) {
        return;
    }

    bool hasVisibleChild = false;
    for (int i = 0; i < item->rowCount(); ++i) {
        QStandardItem* child = item->child(i);
        if (child) {
            filterTreeByTagModel(child, filterText);
            if (child->isEnabled()) {
                hasVisibleChild = true;
            }
        }
    }

    bool match = filterText.isEmpty() || item->text().toLower().contains(filterText);
    item->setEnabled(match || hasVisibleChild);
}

void TagSelectionDialog::onOkClicked() {
    selectedTagIds_.clear();

    for (int i = 0; i < tagModel_->rowCount(); ++i) {
        QStandardItem* item = tagModel_->item(i, 0);
        collectSelectedTagsRecursive(item);
    }

    QVector<qint64> currentTagIds = tagManager_->tagIdsForNote(noteId_);
    for (qint64 tagId : selectedTagIds_) {
        if (!currentTagIds.contains(tagId)) {
            tagManager_->addTagToNote(noteId_, tagId);
        }
    }
    for (qint64 tagId : currentTagIds) {
        if (!selectedTagIds_.contains(tagId)) {
            tagManager_->removeTagFromNote(noteId_, tagId);
        }
    }

    accept();
}

void TagSelectionDialog::collectSelectedTagsRecursive(QStandardItem* item) {
    if (!item) {
        return;
    }

    if (item->isCheckable() && item->checkState() == Qt::Checked) {
        qint64 tagId = item->data(Qt::UserRole).toLongLong();
        if (tagId != -1) {
            selectedTagIds_.append(tagId);
        }
    }

    for (int i = 0; i < item->rowCount(); ++i) {
        collectSelectedTagsRecursive(item->child(i));
    }
}

void TagSelectionDialog::onCancelClicked() {
    reject();
}

QVector<qint64> TagSelectionDialog::selectedTagIds() const {
    return selectedTagIds_;
}
