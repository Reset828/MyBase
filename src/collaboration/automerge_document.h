#ifndef MYBASE_AUTOMERGE_DOCUMENT_H
#define MYBASE_AUTOMERGE_DOCUMENT_H

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QVector>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif
#include <automerge-c/automerge.h>
#ifdef __cplusplus
}
#endif

// Thread-safe, deliberately narrow adapter around Automerge C FFI.
// Markdown is represented as a sequence of Unicode scalar strings in an
// Automerge text object so concurrent edits merge as list operations.
class AutomergeDocument final {
public:
    AutomergeDocument();
    ~AutomergeDocument();
    AutomergeDocument(const AutomergeDocument&) = delete;
    AutomergeDocument& operator=(const AutomergeDocument&) = delete;

    bool initialize(const QString& markdown = QString(), QString* error = nullptr);
    void reset();
    bool load(const QByteArray& snapshot, QString* error = nullptr);
    QString text() const;
    QByteArray cursorAt(int utf16Position) const;
    int positionOfCursor(const QByteArray& cursor) const; // Returns a QString/Qt UTF-16 position.
    bool replaceText(const QString& markdown, QString* error = nullptr);
    QByteArray save(QString* error = nullptr) const;
    QByteArray generateSyncMessage(AMsyncState* state, QString* error = nullptr);
    bool receiveSyncMessage(AMsyncState* state, const QByteArray& message, QString* error = nullptr);

private:
    bool initializeFromResult(AMresult* owner, QString* error);
    bool applyTextDiff(const QString& markdown, QString* error);
    QString textUnlocked() const;
    QString resultError(AMresult* result) const;
    bool resultSucceeded(AMresult* result, QString* error) const;

    mutable QMutex mutex_;
    AMresult* documentOwner_ = nullptr;
    AMdoc* document_ = nullptr;
    AMresult* textObjectOwner_ = nullptr;
    const AMobjId* textObject_ = nullptr;
};

#endif
