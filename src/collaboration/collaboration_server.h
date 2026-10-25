#ifndef MYBASE_COLLABORATION_SERVER_H
#define MYBASE_COLLABORATION_SERVER_H

#include <QObject>
#include <QHostAddress>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QSqlDatabase>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <memory>
#include "automerge_document.h"

class CollaborationServer final : public QObject {
    Q_OBJECT
public:
    enum class Role { ReadOnly, Editor };
    explicit CollaborationServer(const QString& databasePath, QObject* parent = nullptr);
    ~CollaborationServer() override;

    bool initialize(QString* error = nullptr);
    bool listen(const QHostAddress& address, quint16 port, QString* error = nullptr);
    void close();
    quint16 serverPort() const { return server_.serverPort(); }
    QString createInvite(const QString& roomId, Role role, qint64 expiresAtMs, QString* error = nullptr);
    bool revokeInvite(const QString& token, QString* error = nullptr);

signals:
    void listening(quint16 port);
    void errorOccurred(const QString& message);

private:
    struct Peer;
    struct Room;
    void acceptConnections();
    void handleMessage(QWebSocket* socket, const QByteArray& payload);
    void disconnectPeer(QWebSocket* socket);
    void broadcast(Room& room, const QJsonObject& message, QWebSocket* except = nullptr);
    bool persist(Room& room);
    bool authenticate(QWebSocket* socket, Peer& peer, const QJsonObject& message);

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    QWebSocketServer server_;
    QHash<QWebSocket*, Peer*> peers_;
    QHash<QString, std::shared_ptr<Room>> rooms_;
};

#endif
