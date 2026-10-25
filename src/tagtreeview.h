#ifndef TAGTREEVIEW_H
#define TAGTREEVIEW_H

#include <QTreeView>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QHeaderView>
#include <QVector>
#include "tagmanager.h"
#include "tagtreemodel.h"

class TagTreeView : public QTreeView {
    Q_OBJECT

public:
    explicit TagTreeView(QWidget* parent = nullptr);
    void setTagManager(TagManager* tagManager);
    void showTagHierarchy();
    void showNoteHeadings(const QString& markdown);

signals:
    void tagSelected(qint64 tagId);
    void tagSelectionChanged(const QVector<qint64>& tagIds);
    void tagDeselected();
    void tagTextSelected(const QString& tagText);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onRenameTag();
    void onDeleteTag();
    void onMergeTag();
    void onNewSubTag();
    void onNewRootTag();

private:
    void setupContextMenu();
    qint64 getTagIdFromIndex(const QModelIndex& index) const;
    QString getTagNameFromIndex(const QModelIndex& index) const;
    void collectTagNames(const QModelIndex& index, QStringList& names, QHash<QString, qint64>& nameToId);

    TagManager* tagManager_;
    TagTreeModel* tagTreeModel_;
    QMenu* contextMenu_;
    QAction* actionNewRootTag_;
    QAction* actionNewSubTag_;
    QAction* actionRenameTag_;
    QAction* actionDeleteTag_;
    QAction* actionMergeTag_;
    qint64 currentTagId_;
};

#endif // TAGTREEVIEW_H
