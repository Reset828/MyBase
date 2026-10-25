#include "notemanager.h"
#include "aescrypto.h"
#include <QDebug>
#include <QDateTime>

NoteManager::NoteManager(INoteRepository& noteRepository, QObject* parent)
    : QObject(parent), noteRepository_(noteRepository) {
}

qint64 NoteManager::createNote(const QString& title, const QString& content) {
    Note note;
    note.title = title;
    note.content = content;
    note.createdTime = QDateTime::currentDateTime();
    note.modifiedTime = QDateTime::currentDateTime();
    note.isDeleted = false;

    qint64 noteId = noteRepository_.insertNote(note);
    if (noteId != -1) {
        emit noteCreated(noteId);
        emit noteListChanged();
    }
    return noteId;
}

bool NoteManager::updateNote(qint64 noteId, const QString& title, const QString& content) {
    Note note = noteRepository_.getNote(noteId);
    if (note.id == -1) {
        return false;
    }

    note.title = title;
    note.content = content;
    note.modifiedTime = QDateTime::currentDateTime();

    bool result = noteRepository_.updateNote(note);
    if (result) {
        emit noteUpdated(noteId);
        emit noteListChanged();
    }
    return result;
}

bool NoteManager::deleteNote(qint64 noteId) {
    bool result = noteRepository_.softDeleteNote(noteId);
    if (result) {
        emit noteDeleted(noteId);
        emit noteListChanged();
    }
    return result;
}

Note NoteManager::getNote(qint64 noteId) {
    return noteRepository_.getNote(noteId);
}

QVector<Note> NoteManager::getAllNotes(bool includeDeleted) {
    return noteRepository_.getAllNotes(includeDeleted);
}

bool NoteManager::moveToTrash(qint64 noteId) {
    bool result = noteRepository_.softDeleteNote(noteId);
    if (result) {
        emit noteMovedToTrash(noteId);
        emit noteListChanged();
    }
    return result;
}

bool NoteManager::restoreFromTrash(qint64 noteId) {
    Note note = noteRepository_.getNote(noteId);
    if (note.id == -1) {
        return false;
    }

    note.isDeleted = false;
    note.modifiedTime = QDateTime::currentDateTime();

    bool result = noteRepository_.updateNote(note);
    if (result) {
        emit noteRestoredFromTrash(noteId);
        emit noteListChanged();
    }
    return result;
}

bool NoteManager::permanentlyDelete(qint64 noteId) {
    bool result = noteRepository_.permanentlyDeleteNote(noteId);
    if (result) {
        emit noteDeleted(noteId);
        emit noteListChanged();
    }
    return result;
}

int NoteManager::cleanupTrash(int daysThreshold) {
    QVector<Note> trashNotes = noteRepository_.getTrashNotes();
    QVector<qint64> notesToDelete;

    QDateTime now = QDateTime::currentDateTime();
    for (const Note& note : trashNotes) {
        if (note.deletedTime.isValid() && note.deletedTime.daysTo(now) >= daysThreshold) {
            notesToDelete.append(note.id);
        }
    }

    int deletedCount = 0;
    for (qint64 noteId : notesToDelete) {
        if (noteRepository_.permanentlyDeleteNote(noteId)) {
            emit noteDeleted(noteId);
            deletedCount++;
        }
    }

    if (deletedCount > 0) {
        emit noteListChanged();
    }

    return deletedCount;
}

QVector<Note> NoteManager::getTrashNotes() {
    return noteRepository_.getTrashNotes();
}

bool NoteManager::encryptNote(qint64 noteId, const QString& password) {
    Note note = noteRepository_.getNote(noteId);
    if (note.id == -1) {
        return false;
    }

    if (note.isEncrypted) {
        return false;
    }

    QString contentToEncrypt = QStringLiteral("[MYBASE_ENCRYPTED]") + note.content;
    
    QByteArray iv;
    QByteArray encryptedContent = AesCrypto::encrypt(contentToEncrypt.toUtf8(), password, iv);
    if (encryptedContent.isEmpty() && !contentToEncrypt.isEmpty()) {
        return false;
    }

    note.content = encryptedContent.toBase64();
    note.isEncrypted = true;
    note.encryptionIv = iv.toBase64();
    note.modifiedTime = QDateTime::currentDateTime();

    bool result = noteRepository_.updateNote(note);
    if (result) {
        emit noteUpdated(noteId);
        emit noteEncrypted(noteId);
        emit noteListChanged();
    }
    return result;
}

bool NoteManager::decryptNote(qint64 noteId, const QString& password) {
    Note note = noteRepository_.getNote(noteId);
    if (note.id == -1) {
        return false;
    }

    if (!note.isEncrypted) {
        return false;
    }

    QByteArray encryptedContent = QByteArray::fromBase64(note.content.toUtf8());
    QByteArray iv = QByteArray::fromBase64(note.encryptionIv.toUtf8());
    
    QByteArray decryptedContent = AesCrypto::decrypt(encryptedContent, password, iv);
    if (decryptedContent.isEmpty()) {
        return false;
    }

    QString decryptedStr = QString::fromUtf8(decryptedContent);
    const QString marker = QStringLiteral("[MYBASE_ENCRYPTED]");
    
    if (decryptedStr.startsWith(marker)) {
        note.content = decryptedStr.mid(marker.length());
    } else if (decryptedStr.contains(marker)) {
        int pos = decryptedStr.indexOf(marker);
        note.content = decryptedStr.mid(pos + marker.length());
    } else {
        return false;
    }
    
    note.isEncrypted = false;
    note.encryptionIv.clear();
    note.modifiedTime = QDateTime::currentDateTime();

    bool result = noteRepository_.updateNote(note);
    if (result) {
        emit noteUpdated(noteId);
        emit noteDecrypted(noteId);
        emit noteListChanged();
    }
    return result;
}

bool NoteManager::isNoteEncrypted(qint64 noteId) {
    Note note = noteRepository_.getNote(noteId);
    return note.isEncrypted;
}

QByteArray NoteManager::decryptNoteContent(qint64 noteId, const QString& password) {
    Note note = noteRepository_.getNote(noteId);
    if (note.id == -1 || !note.isEncrypted) {
        return QByteArray();
    }

    QByteArray encryptedContent = QByteArray::fromBase64(note.content.toUtf8());
    QByteArray iv = QByteArray::fromBase64(note.encryptionIv.toUtf8());
    
    QByteArray decryptedContent = AesCrypto::decrypt(encryptedContent, password, iv);
    if (decryptedContent.isEmpty()) {
        return QByteArray();
    }

    QString decryptedStr = QString::fromUtf8(decryptedContent);
    const QString marker = QStringLiteral("[MYBASE_ENCRYPTED]");
    
    if (decryptedStr.startsWith(marker)) {
        return decryptedStr.mid(marker.length()).toUtf8();
    }
    
    if (decryptedStr.contains(QStringLiteral("[MYBASE_ENCRYPTED]"))) {
        int pos = decryptedStr.indexOf(marker);
        return decryptedStr.mid(pos + marker.length()).toUtf8();
    }
    
    return QByteArray();
}

QVector<Note> NoteManager::getEncryptedNotes() {
    return noteRepository_.getEncryptedNotes();
}
