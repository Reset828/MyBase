#ifndef TAGSELECTIONDIALOG_H
#define TAGSELECTIONDIALOG_H

#include <QDialog>
#include <QTreeView>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStandardItemModel>
#include "tagmanager.h"

class TagSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit TagSelectionDialog(TagManager* tagManager, qint64 noteId, QWidget* parent = nullptr);
    QVector<qint64> selectedTagIds() const;

private slots:
    void onFilterTextChanged(const QString& text);
    void onOkClicked();
    void onCancelClicked();

private:
    void setupUI();
    void loadTags();
    void setupConnections();
    void filterTreeByTagModel(QStandardItem* item, const QString& filterText);
    void collectSelectedTagsRecursive(QStandardItem* item);

    TagManager* tagManager_;
    qint64 noteId_;
    QTreeView* tagTreeView_;
    QStandardItemModel* tagModel_;
    QLineEdit* filterLineEdit_;
    QPushButton* okButton_;
    QPushButton* cancelButton_;
    QVector<qint64> selectedTagIds_;
};

#endif // TAGSELECTIONDIALOG_H
