#include "encryptednotesdialog.h"
#include "passworddialog.h"
#include <QHeaderView>
#include <QDateTime>

EncryptedNotesDialog::EncryptedNotesDialog(NoteManager* noteManager, QWidget* parent)
    : QDialog(parent), noteManager_(noteManager) {
    setWindowTitle(tr("加密笔记"));
    resize(700, 450);

    QString dialogStyle = R"(
        QDialog {
            background-color: #f5f9f5;
        }
        QTableView {
            background-color: #ffffff;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            gridline-color: #e8f5e8;
            selection-background-color: #d4edda;
            selection-color: #1a4a1a;
            alternate-background-color: #f5faf5;
        }
        QHeaderView::section {
            background-color: #eaf4ea;
            color: #2e5c2e;
            border: none;
            border-bottom: 1px solid #c8e6c8;
            padding: 8px 10px;
            font-weight: bold;
        }
        QPushButton {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 7px 18px;
            color: #2e5c2e;
        }
        QPushButton:hover {
            background-color: #d4edda;
            border: 1px solid #a8d8a8;
        }
        QPushButton:pressed {
            background-color: #c8e6c8;
        }
        QPushButton:disabled {
            background-color: #f0f5f0;
            color: #a0c0a0;
            border: 1px solid #d8e8d8;
        }
        QLabel {
            color: #2e5c2e;
        }
    )";
    setStyleSheet(dialogStyle);

    setupUI();
    setupConnections();
    loadEncryptedNotes();
}

void EncryptedNotesDialog::setupUI() {
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

    encryptedModel_ = new QStandardItemModel(this);
    encryptedModel_->setHorizontalHeaderLabels({tr("标题"), tr("加密时间"), tr("修改时间")});
    tableView_->setModel(encryptedModel_);

    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    viewButton_ = new QPushButton(tr("查看"));
    viewButton_->setEnabled(false);

    decryptButton_ = new QPushButton(tr("取消加密"));
    decryptButton_->setEnabled(false);

    deleteButton_ = new QPushButton(tr("删除"));
    deleteButton_->setEnabled(false);
    deleteButton_->setStyleSheet("QPushButton { background-color: #ffebee; color: #c62828; } QPushButton:hover { background-color: #ffcdd2; }");

    closeButton_ = new QPushButton(tr("关闭"));

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addWidget(viewButton_);
    buttonLayout->addWidget(decryptButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton_);

    mainLayout->addWidget(tableView_);
    mainLayout->addLayout(buttonLayout);
}

void EncryptedNotesDialog::setupConnections() {
    connect(tableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &EncryptedNotesDialog::onSelectionChanged);
    connect(viewButton_, &QPushButton::clicked, this, &EncryptedNotesDialog::onViewSelected);
    connect(decryptButton_, &QPushButton::clicked, this, &EncryptedNotesDialog::onDecryptSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &EncryptedNotesDialog::onDeleteSelected);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void EncryptedNotesDialog::loadEncryptedNotes() {
    encryptedModel_->removeRows(0, encryptedModel_->rowCount());

    QVector<Note> encryptedNotes = noteManager_->getEncryptedNotes();
    for (const Note& note : encryptedNotes) {
        QList<QStandardItem*> rowItems;

        QStandardItem* titleItem = new QStandardItem(QStringLiteral("🔒 ") + note.title);
        titleItem->setData(note.id, Qt::UserRole);
        rowItems.append(titleItem);

        QStandardItem* encryptedItem = new QStandardItem(note.modifiedTime.toString("yyyy-MM-dd hh:mm"));
        rowItems.append(encryptedItem);

        QStandardItem* modifiedItem = new QStandardItem(note.modifiedTime.toString("yyyy-MM-dd hh:mm"));
        rowItems.append(modifiedItem);

        encryptedModel_->appendRow(rowItems);
    }
}

void EncryptedNotesDialog::onSelectionChanged() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    bool hasSelection = !selected.isEmpty();
    viewButton_->setEnabled(hasSelection);
    decryptButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
}

void EncryptedNotesDialog::onViewSelected() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    qint64 noteId = selected.first().data(Qt::UserRole).toLongLong();
    
    PasswordDialog dialog(PasswordDialog::Decrypt, this);
    dialog.setConfirmMode(false);
    if (dialog.exec() == QDialog::Accepted) {
        QString password = dialog.getPassword();
        QByteArray decryptedContent = noteManager_->decryptNoteContent(noteId, password);
        if (decryptedContent.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("解密失败，请检查密码是否正确"));
            return;
        }

        Note note = noteManager_->getNote(noteId);
        
        EncryptedNoteViewDialog viewDialog(this);
        viewDialog.setNoteContent(note.title, QString::fromUtf8(decryptedContent));
        viewDialog.exec();
    }
}

void EncryptedNotesDialog::onDecryptSelected() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    qint64 noteId = selected.first().data(Qt::UserRole).toLongLong();
    Note note = noteManager_->getNote(noteId);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("取消加密"),
        tr("确定要取消笔记 \"%1\" 的加密吗？").arg(note.title),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        PasswordDialog dialog(PasswordDialog::Decrypt, this);
        dialog.setConfirmMode(false);
        if (dialog.exec() == QDialog::Accepted) {
            QString password = dialog.getPassword();
            
            if (noteManager_->decryptNote(noteId, password)) {
                loadEncryptedNotes();
                QMessageBox::information(this, tr("成功"), tr("笔记已取消加密，已恢复到笔记列表"));
                accept();
            } else {
                QMessageBox::warning(this, tr("错误"), tr("解密失败，请检查密码是否正确"));
            }
        }
    }
}

void EncryptedNotesDialog::onDeleteSelected() {
    QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    qint64 noteId = selected.first().data(Qt::UserRole).toLongLong();
    Note note = noteManager_->getNote(noteId);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("删除加密笔记"),
        tr("确定要永久删除加密笔记 \"%1\" 吗？\n\n此操作不可恢复！").arg(note.title),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (noteManager_->permanentlyDelete(noteId)) {
            loadEncryptedNotes();
            QMessageBox::information(this, tr("成功"), tr("加密笔记已永久删除"));
            
            if (encryptedModel_->rowCount() == 0) {
                accept();
            }
        } else {
            QMessageBox::warning(this, tr("错误"), tr("删除笔记失败"));
        }
    }
}
