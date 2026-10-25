#ifndef MARKDOWNOUTLINEMODEL_H
#define MARKDOWNOUTLINEMODEL_H

#include <QStandardItemModel>

class MarkdownOutlineModel final : public QStandardItemModel {
    Q_OBJECT
public:
    explicit MarkdownOutlineModel(QObject* parent = nullptr);
    void setMarkdown(const QString& markdown);
    static constexpr int PositionRole = Qt::UserRole + 1;
    static constexpr int LevelRole = Qt::UserRole + 2;
};

#endif // MARKDOWNOUTLINEMODEL_H
