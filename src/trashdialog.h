#ifndef TRASHDIALOG_H
#define TRASHDIALOG_H

#include <QDialog>
#include <QTableView>
#include <QStandardItemModel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include "notemanager.h"

class TrashDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrashDialog(NoteManager* noteManager, QWidget* parent = nullptr);

private slots:
    void onRestoreSelected();
    void onPermanentlyDeleteSelected();
    void onEmptyTrash();
    void onSelectionChanged();

private:
    void setupUI();
    void loadTrashNotes();
    void setupConnections();

    NoteManager* noteManager_;
    QTableView* tableView_;
    QStandardItemModel* trashModel_;
    QPushButton* restoreButton_;
    QPushButton* deleteButton_;
    QPushButton* emptyTrashButton_;
    QPushButton* closeButton_;
};

#endif // TRASHDIALOG_H
