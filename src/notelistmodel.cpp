#include "notelistmodel.h"

#include <QStandardItem>

NoteListModel::NoteListModel(INoteRepository& noteRepository,
                             INoteTagRepository& noteTagRepository,
                             QObject* parent)
    : QStandardItemModel(parent),
      noteRepository_(noteRepository),
      noteTagRepository_(noteTagRepository) {
    setHorizontalHeaderLabels({QStringLiteral("Title"),
                               QStringLiteral("Modified"),
                               QStringLiteral("Tags")});
}

void NoteListModel::refresh() {
    removeRows(0, rowCount());

    const QVector<Note> notes = noteRepository_.getAllNotes(false);
    for (const Note& note : notes) {
        if (note.isEncrypted) {
            continue;
        }

        auto* titleItem = new QStandardItem(note.title);
        titleItem->setData(note.id, Qt::UserRole);
        titleItem->setData(note.isEncrypted, Qt::UserRole + 2);

        auto* modifiedItem = new QStandardItem(
            note.modifiedTime.toString(QStringLiteral("yyyy-MM-dd hh:mm")));

        QStringList tagNames;
        const QVector<Tag> tags = noteTagRepository_.getTagsForNote(note.id);
        tagNames.reserve(tags.size());
        for (const Tag& tag : tags) {
            tagNames.append(tag.name);
        }
        auto* tagsItem = new QStandardItem(tagNames.join(QStringLiteral(", ")));

        appendRow({titleItem, modifiedItem, tagsItem});
    }
}
