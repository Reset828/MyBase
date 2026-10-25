#ifndef NOTEMANAGER_H
#define NOTEMANAGER_H

#include <QObject>
#include "note.h"
#include "repositories.h"

class NoteManager : public QObject {
    Q_OBJECT

public:
    explicit NoteManager(INoteRepository& noteRepository, QObject* parent = nullptr);

    qint64 createNote(const QString& title, const QString& content);
    bool updateNote(qint64 noteId, const QString& title, const QString& content);
    bool deleteNote(qint64 noteId);
    Note getNote(qint64 noteId);
    QVector<Note> getAllNotes(bool includeDeleted = false);

    bool moveToTrash(qint64 noteId);
    bool restoreFromTrash(qint64 noteId);
    bool permanentlyDelete(qint64 noteId);
    QVector<Note> getTrashNotes();
    int cleanupTrash(int daysThreshold = 30);

    bool encryptNote(qint64 noteId, const QString& password);
    bool decryptNote(qint64 noteId, const QString& password);
    bool isNoteEncrypted(qint64 noteId);
    QByteArray decryptNoteContent(qint64 noteId, const QString& password);
    QVector<Note> getEncryptedNotes();

signals:
    void noteCreated(qint64 noteId);
    void noteUpdated(qint64 noteId);
    void noteDeleted(qint64 noteId);
    void noteListChanged();
    void noteMovedToTrash(qint64 noteId);
    void noteRestoredFromTrash(qint64 noteId);
    void noteEncrypted(qint64 noteId);
    void noteDecrypted(qint64 noteId);

private:
    INoteRepository& noteRepository_;
};

#endif // NOTEMANAGER_H
