#ifndef TAGTREEMODEL_H
#define TAGTREEMODEL_H

#include <QStandardItemModel>
#include "tag.h"
#include <QMimeData>
#include <QStringList>

class TagTreeModel : public QStandardItemModel {
    Q_OBJECT

public:
    explicit TagTreeModel(QObject* parent = nullptr);

    void setTags(const QVector<Tag>& tags);
    void setHeadingsFromMarkdown(const QString& markdown);

    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;

signals:
    void tagDropped(qint64 tagId, qint64 newParentId, int newRow);

private:
    static const QString TAG_MIME_TYPE;
};

#endif // TAGTREEMODEL_H
