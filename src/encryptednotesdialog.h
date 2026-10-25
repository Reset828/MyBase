#ifndef ENCRYPTEDNOTESDIALOG_H
#define ENCRYPTEDNOTESDIALOG_H

#include <QDialog>
#include <QTableView>
#include <QStandardItemModel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include "notemanager.h"
#include "encryptednoteviewdialog.h"

class EncryptedNotesDialog : public QDialog {
    Q_OBJECT

public:
    explicit EncryptedNotesDialog(NoteManager* noteManager, QWidget* parent = nullptr);

private slots:
    void onDecryptSelected();
    void onViewSelected();
    void onDeleteSelected();
    void onSelectionChanged();

private:
    void setupUI();
    void loadEncryptedNotes();
    void setupConnections();

    NoteManager* noteManager_;
    QTableView* tableView_;
    QStandardItemModel* encryptedModel_;
    QPushButton* viewButton_;
    QPushButton* decryptButton_;
    QPushButton* deleteButton_;
    QPushButton* closeButton_;
};

#endif // ENCRYPTEDNOTESDIALOG_H
