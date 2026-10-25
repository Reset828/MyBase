#ifndef NOTELISTMODEL_H
#define NOTELISTMODEL_H

#include <QStandardItemModel>
#include "repositories.h"

class NoteListModel final : public QStandardItemModel {
public:
    NoteListModel(INoteRepository& noteRepository,
                  INoteTagRepository& noteTagRepository,
                  QObject* parent = nullptr);

    void refresh();

private:
    INoteRepository& noteRepository_;
    INoteTagRepository& noteTagRepository_;
};

#endif // NOTELISTMODEL_H
