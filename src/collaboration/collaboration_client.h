#ifndef MYBASE_COLLABORATION_CLIENT_H
#define MYBASE_COLLABORATION_CLIENT_H

#include <QObject>
#include <QUrl>
#include <QWebSocket>
#include <QTimer>
#include <QFile>
#include <QHash>
#include <QStringList>
#include "automerge_document.h"

class CollaborationClient final : public QObject {
    Q_OBJECT
public:
    explicit CollaborationClient(const QString& offlineQueuePath, QObject* parent = nullptr);
    ~CollaborationClient() override;
    bool join(const QUrl& server, const QString& room, const QString& invitationToken,
              const QString& displayName, const QString& initialMarkdown,
              bool encryptedNote, bool sensitiveContentConfirmed, QString* error = nullptr);
    void leave();
    bool setLocalMarkdown(const QString& markdown, QString* error = nullptr);
    void sendCursor(int anchor, int position);
    bool isConnected() const;
    bool isReadOnly() const;
    QString peerId() const;

signals:
    void connected();
    void disconnected();
    void markdownChanged(const QString& markdown);
    void membersChanged(const QStringList& members);
    void remoteCursor(const QString& peerId, const QString& name, int anchor, int position);
    void roleChanged(bool readOnly);
    void statusMessage(const QString& message);

private:
    struct PendingCursor {
        QString name;
        QByteArray anchor;
        QByteArray position;
    };

    void openSocket();
    void sendJoin();
    void handleMessage(const QString& message);
    void queueOrSend(const QByteArray& frame);
    void flushQueue();
    void sendDocumentSync();
    bool saveQueue();
    void loadQueue();
    void resolvePendingCursors();

    QWebSocket socket_;
    QTimer reconnectTimer_;
    QTimer heartbeatTimer_;
    QTimer syncDebounceTimer_;
    QUrl serverUrl_;
    QString roomId_;
    QString invitationToken_;
    QString displayName_;
    QString peerId_;
    QString offlineQueuePath_;
    QString offlineQueueBasePath_;
    QStringList queuedFrames_;
    QHash<QString, PendingCursor> pendingCursors_;
    AutomergeDocument document_;
    AMresult* syncStateOwner_ = nullptr;
    AMsyncState* syncState_ = nullptr;
    bool joining_ = false;
    bool joined_ = false;
    bool readOnly_ = false;
    int reconnectAttempt_ = 0;
    qint64 nextSequence_ = 1;
    bool hasJoinedOnce_ = false;
    bool replaceDocumentOnJoin_ = true;
    bool intentionalClose_ = false;
    bool resetSyncState(QString* error = nullptr);
};

#endif
