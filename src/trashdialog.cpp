#include "trashdialog.h"
#include "themestylesheet.h"
#include <QHeaderView>
#include <QDateTime>

TrashDialog::TrashDialog(NoteManager* noteManager, QWidget* parent)
    : QDialog(parent), noteManager_(noteManager) {
    setWindowTitle(tr("回收站"));
    resize(800, 500);
    setStyleSheet(getTrashDialogStyle());
    setupUI();
    setupConnections();
    loadTrashNotes();
}

void TrashDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    tableView_ = new QTableView();
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setAlternatingRowColors(true);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->setShowGrid(false);

    trashModel_ = new QStandardItemModel(this);
    trashModel_->setHorizontalHeaderLabels({tr("标题"), tr("删除时间"), tr("修改时间")});
    tableView_->setModel(trashModel_);

    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    restoreButton_ = new QPushButton(tr("恢复"));
    restoreButton_->setEnabled(false);

    deleteButton_ = new QPushButton(tr("彻底删除"));
    deleteButton_->setEnabled(false);

    emptyTrashButton_ = new QPushButton(tr("清空回收站"));

    closeButton_ = new QPushButton(tr("关闭"));

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addWidget(restoreButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(emptyTrashButton_);
    buttonLayout->addWidget(closeButton_);

    mainLayout->addWidget(tableView_);
    mainLayout->addLayout(buttonLayout);
}

void TrashDialog::setupConnections() {
    connect(tableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TrashDialog::onSelectionChanged);
    connect(restoreButton_, &QPushButton::clicked, this, &TrashDialog::onRestoreSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &TrashDialog::onPermanentlyDeleteSelected);
    connect(emptyTrashButton_, &QPushButton::clicked, this, &TrashDialog::onEmptyTrash);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void TrashDialog::loadTrashNotes() {
    trashModel_->removeRows(0, trashModel_->rowCount());

    QVector<Note> trashNotes = noteManager_->getTrashNotes();
    for (const Note& note : trashNotes) {
        if (!note.isDeleted) {
            continue;
        }

        QList<QStandardItem*> rowItems;

        QStandardItem* titleItem = new QStandardItem(note.title);
        titleItem->setData(note.id, Qt::UserRole);
        rowItems.append(titleItem);

        QStandardItem* deletedItem = new QStandardItem(note.deletedTime.toString("yyyy-MM-dd hh:mm"));
        rowItems.append(deletedItem);

        QStandardItem* modifiedItem = new QStandardItem(note.modifiedTime.toString("yyyy-MM-dd hh:mm"));
        rowItems.append(modifiedItem);

        trashModel_->appendRow(rowItems);
    }
}

void TrashDialog::onSelectionChanged() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    bool hasSelection = !selected.isEmpty();
    restoreButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
}

void TrashDialog::onRestoreSelected() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    qint64 noteId = selected.first().data(Qt::UserRole).toLongLong();
    if (noteManager_->restoreFromTrash(noteId)) {
        loadTrashNotes();
        QMessageBox::information(this, tr("成功"), tr("笔记已恢复"));
    } else {
        QMessageBox::warning(this, tr("错误"), tr("恢复笔记失败"));
    }
}

void TrashDialog::onPermanentlyDeleteSelected() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    qint64 noteId = selected.first().data(Qt::UserRole).toLongLong();
    Note note = noteManager_->getNote(noteId);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("彻底删除"),
        tr("确定要彻底删除笔记 \"%1\" 吗？此操作不可恢复。").arg(note.title),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (noteManager_->permanentlyDelete(noteId)) {
            loadTrashNotes();
            QMessageBox::information(this, tr("成功"), tr("笔记已彻底删除"));
        } else {
            QMessageBox::warning(this, tr("错误"), tr("彻底删除笔记失败"));
        }
    }
}

void TrashDialog::onEmptyTrash() {
    QVector<Note> trashNotes = noteManager_->getTrashNotes();
    int deletedCount = 0;

    for (const Note& note : trashNotes) {
        if (note.isDeleted && noteManager_->permanentlyDelete(note.id)) {
            deletedCount++;
        }
    }

    loadTrashNotes();
    QMessageBox::information(this, tr("清空回收站"),
                             tr("已清空回收站，共删除 %1 篇笔记。").arg(deletedCount));
}
