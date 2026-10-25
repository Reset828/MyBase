#include "collaboration_server.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

struct CollaborationServer::Peer {
    QString roomId;
    QString peerId;
    QString displayName;
    Role role = Role::ReadOnly;
    qint64 lastCursorAtMs = 0;
    AMresult* syncStateOwner = nullptr;
    AMsyncState* syncState = nullptr;
    ~Peer() { if (syncStateOwner) AMresultFree(syncStateOwner); }
};
struct CollaborationServer::Room {
    QString id;
    std::unique_ptr<AutomergeDocument> document = std::make_unique<AutomergeDocument>();
    QHash<QString, QPointer<QWebSocket>> members;
    bool loaded = false;
};

CollaborationServer::CollaborationServer(const QString& databasePath, QObject* parent)
    : QObject(parent), databasePath_(databasePath),
      connectionName_(QStringLiteral("mybase-collab-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))),
      server_(QStringLiteral("MyBase collaboration"), QWebSocketServer::NonSecureMode) {
    connect(&server_, &QWebSocketServer::newConnection, this, &CollaborationServer::acceptConnections);
}

CollaborationServer::~CollaborationServer() {
    close();
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool CollaborationServer::initialize(QString* error) {
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) { if (error) *error = database_.lastError().text(); return false; }
    QSqlQuery query(database_);
    const QStringList schema = {
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS collaboration_rooms(room_id TEXT PRIMARY KEY,snapshot BLOB NOT NULL,updated_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS collaboration_invites(token_hash TEXT PRIMARY KEY,room_id TEXT NOT NULL,role INTEGER NOT NULL,expires_at INTEGER NOT NULL,revoked INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_collaboration_invites_room ON collaboration_invites(room_id)")
    };
    for (const QString& statement : schema) {
        if (!query.exec(statement)) { if (error) *error = query.lastError().text(); return false; }
    }
    return true;
}

bool CollaborationServer::listen(const QHostAddress& address, quint16 port, QString* error) {
    if (!database_.isOpen() && !initialize(error)) return false;
    if (!server_.listen(address, port)) { if (error) *error = server_.errorString(); return false; }
    emit listening(server_.serverPort());
    return true;
}

void CollaborationServer::close() {
    server_.close();
    const auto sockets = peers_.keys();
    for (QWebSocket* socket : sockets) {
        Peer* peer = peers_.take(socket);
        if (socket) { socket->disconnect(this); socket->close(); socket->deleteLater(); }
        delete peer;
    }
    rooms_.clear();
}

QString CollaborationServer::createInvite(const QString& roomId, Role role, qint64 expiresAtMs, QString* error) {
    if (roomId.trimmed().isEmpty() || expiresAtMs <= QDateTime::currentMSecsSinceEpoch()) {
        if (error) *error = QStringLiteral("房间 ID 或邀请有效期无效");
        return {};
    }
    if (!database_.isOpen() && !initialize(error)) return {};
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces) +
                          QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray hash = QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO collaboration_invites(token_hash,room_id,role,expires_at,revoked) VALUES(?,?,?,?,0)"));
    query.addBindValue(QString::fromLatin1(hash)); query.addBindValue(roomId);
    query.addBindValue(role == Role::Editor ? 1 : 0); query.addBindValue(expiresAtMs);
    if (!query.exec()) { if (error) *error = query.lastError().text(); return {}; }
    return token;
}

bool CollaborationServer::revokeInvite(const QString& token, QString* error) {
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSqlQuery query(database_); query.prepare(QStringLiteral("UPDATE collaboration_invites SET revoked=1 WHERE token_hash=?"));
    query.addBindValue(hash);
    if (!query.exec()) { if (error) *error = query.lastError().text(); return false; }
    return query.numRowsAffected() > 0;
}

void CollaborationServer::acceptConnections() {
    while (server_.hasPendingConnections()) {
        QWebSocket* socket = server_.nextPendingConnection();
        if (!socket) continue;
        if (peers_.size() >= 128) { socket->close(); socket->deleteLater(); continue; }
        socket->setParent(this);
        socket->setMaxAllowedIncomingFrameSize(16 * 1024 * 1024);
        socket->setMaxAllowedIncomingMessageSize(16 * 1024 * 1024);
        auto* peer = new Peer;
        AMresult* state = AMsyncStateInit();
        if (!state || AMresultStatus(state) != AM_STATUS_OK ||
            !AMitemToSyncState(AMresultItem(state), &peer->syncState)) {
            if (state) AMresultFree(state); delete peer; socket->close(); socket->deleteLater(); continue;
        }
        peer->syncStateOwner = state;
        peers_.insert(socket, peer);
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString& text) { handleMessage(socket, text.toUtf8()); });
        connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray& bytes) { handleMessage(socket, bytes); });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] { disconnectPeer(socket); });
    }
}

bool CollaborationServer::authenticate(QWebSocket* socket, Peer& peer, const QJsonObject& message) {
    if (peer.roomId.size()) return true;
    const QString roomId = message.value(QStringLiteral("room")).toString();
    const QString token = message.value(QStringLiteral("invite")).toString();
    if (roomId.isEmpty() || roomId.size() > 128 || token.isEmpty() || token.size() > 128) return false;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSqlQuery query(database_); query.prepare(QStringLiteral("SELECT role,expires_at,revoked FROM collaboration_invites WHERE token_hash=? AND room_id=?"));
    query.addBindValue(hash); query.addBindValue(roomId);
    if (!query.exec() || !query.next() || query.value(2).toBool() || query.value(1).toLongLong() < QDateTime::currentMSecsSinceEpoch()) return false;
    peer.role = query.value(0).toInt() == 1 ? Role::Editor : Role::ReadOnly;
    peer.roomId = roomId;
    peer.peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    peer.displayName = message.value(QStringLiteral("name")).toString().left(48).trimmed();
    if (peer.displayName.isEmpty()) peer.displayName = QStringLiteral("成员");
    auto& room = rooms_[roomId]; if (!room) { room = std::make_shared<Room>(); room->id = roomId; }
    if (!room->loaded) {
        QSqlQuery snapshot(database_);
        snapshot.prepare(QStringLiteral("SELECT snapshot FROM collaboration_rooms WHERE room_id=?"));
        snapshot.addBindValue(roomId);
        QString error;
        if (!snapshot.exec()) {
            emit errorOccurred(snapshot.lastError().text());
            return false;
        }
        if (snapshot.next()) {
            if (!room->document->load(snapshot.value(0).toByteArray(), &error)) {
                emit errorOccurred(error);
                return false;
            }
        } else {
            const QString encodedInitial = message.value(QStringLiteral("snapshot")).toString();
            const QByteArray initial = QByteArray::fromBase64(encodedInitial.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!encodedInitial.isEmpty() && initial.isEmpty()) {
                emit errorOccurred(QStringLiteral("初始协作快照编码无效"));
                return false;
            }
            if (!initial.isEmpty()) {
                if (initial.size() > 12 * 1024 * 1024 || !room->document->load(initial, &error)) {
                    emit errorOccurred(error.isEmpty() ? QStringLiteral("初始协作快照超过大小限制") : error);
                    return false;
                }
            } else if (!room->document->initialize(QString(), &error)) {
                emit errorOccurred(error);
                return false;
            }
        }
        room->loaded = true;
    }
    room->members.insert(peer.peerId, socket);
    return true;
}

void CollaborationServer::handleMessage(QWebSocket* socket, const QByteArray& payload) {
    Peer* peer = peers_.value(socket, nullptr);
    if (!peer || payload.size() > 16 * 1024 * 1024) { if (socket) socket->close(); return; }
    QJsonParseError parseError; const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) { socket->close(); return; }
    QJsonObject message = document.object(); const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("join")) {
        if (!authenticate(socket, *peer, message)) { socket->sendTextMessage(QStringLiteral("{\"type\":\"error\",\"message\":\"invite rejected\"}")); socket->close(); return; }
        Room& room = *rooms_[peer->roomId];
        QString snapshotError; const QByteArray snapshot = room.document->save(&snapshotError);
        if (snapshot.isEmpty()) { emit errorOccurred(snapshotError); socket->close(); return; }
        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("joined")}, {QStringLiteral("peer"), peer->peerId}, {QStringLiteral("role"), peer->role == Role::Editor ? QStringLiteral("editor") : QStringLiteral("read-only")}, {QStringLiteral("markdown"), room.document->text()}, {QStringLiteral("snapshot"), QString::fromLatin1(snapshot.toBase64())}}).toJson(QJsonDocument::Compact)));
        QJsonArray memberList;
        for (auto it = room.members.cbegin(); it != room.members.cend(); ++it) {
            const Peer* member = peers_.value(it.value(), nullptr);
            if (member) memberList.append(QJsonObject{{QStringLiteral("id"), member->peerId}, {QStringLiteral("name"), member->displayName}});
        }
        QJsonObject membersMessage{{QStringLiteral("type"), QStringLiteral("members")}, {QStringLiteral("members"), memberList}};
        broadcast(room, membersMessage, socket);
        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(membersMessage).toJson(QJsonDocument::Compact)));
        QString syncError;
        const QByteArray initialSync = room.document->generateSyncMessage(peer->syncState, &syncError);
        if (!initialSync.isEmpty()) socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")}, {QStringLiteral("payload"), QString::fromLatin1(initialSync.toBase64())}}).toJson(QJsonDocument::Compact)));
        persist(room);
        return;
    }
    if (peer->roomId.isEmpty()) { socket->close(); return; }
    Room& room = *rooms_[peer->roomId];
    if (type == QStringLiteral("sync")) {
        const QString encodedPayload = message.value(QStringLiteral("payload")).toString();
        const QByteArray bytes = QByteArray::fromBase64(encodedPayload.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!encodedPayload.isEmpty() && bytes.isEmpty()) { socket->close(); return; }
        if (bytes.size() > 12 * 1024 * 1024) { socket->close(); return; }
        QString error;
        const QByteArray before = peer->role == Role::ReadOnly ? room.document->save(&error) : QByteArray();
        const QString priorText = peer->role == Role::ReadOnly ? room.document->text() : QString();
        if (!room.document->receiveSyncMessage(peer->syncState, bytes, &error)) { emit errorOccurred(error); return; }
        if (peer->role == Role::ReadOnly && room.document->text() != priorText) {
            QString rollbackError; room.document->load(before, &rollbackError);
            socket->close(); emit errorOccurred(QStringLiteral("只读协作者尝试修改文档")); return;
        }
        if (!persist(room)) {
            const QString failure = QString::fromUtf8(QJsonDocument(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("message"), QStringLiteral("服务端快照写入失败，操作未确认，请保留本地离线队列并重试")}
            }).toJson(QJsonDocument::Compact));
            socket->sendTextMessage(failure);
            return;
        }
        const QString ack = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("ack")}, {QStringLiteral("sequence"), message.value(QStringLiteral("sequence"))}}).toJson(QJsonDocument::Compact));
        socket->sendTextMessage(ack);
        const QByteArray senderUpdate = room.document->generateSyncMessage(peer->syncState, &error);
        if (!senderUpdate.isEmpty()) socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")}, {QStringLiteral("payload"), QString::fromLatin1(senderUpdate.toBase64())}}).toJson(QJsonDocument::Compact)));
        for (QPointer<QWebSocket> target : room.members) {
            if (!target || target == socket) continue;
            Peer* targetPeer = peers_.value(target, nullptr); if (!targetPeer) continue;
            const QByteArray outgoing = room.document->generateSyncMessage(targetPeer->syncState, &error);
            if (!outgoing.isEmpty()) target->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")}, {QStringLiteral("payload"), QString::fromLatin1(outgoing.toBase64())}}).toJson(QJsonDocument::Compact)));
        }
    } else if (type == QStringLiteral("cursor")) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - peer->lastCursorAtMs < 40) return;
        peer->lastCursorAtMs = now;
        const QString cursorAnchor = message.value(QStringLiteral("cursorAnchor")).toString();
        const QString cursorPosition = message.value(QStringLiteral("cursorPosition")).toString();
        if (cursorAnchor.size() > 1024 || cursorPosition.size() > 1024) return;
        const QByteArray anchorBytes = QByteArray::fromBase64(cursorAnchor.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const QByteArray positionBytes = QByteArray::fromBase64(cursorPosition.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if ((!cursorAnchor.isEmpty() && (anchorBytes.isEmpty() || anchorBytes.size() > 512)) ||
            (!cursorPosition.isEmpty() && (positionBytes.isEmpty() || positionBytes.size() > 512))) return;
        QJsonObject forwarded{{QStringLiteral("type"), QStringLiteral("cursor")},
                              {QStringLiteral("peer"), peer->peerId},
                              {QStringLiteral("name"), peer->displayName},
                              {QStringLiteral("anchor"), qBound(0, message.value(QStringLiteral("anchor")).toInt(), 16 * 1024 * 1024)},
                              {QStringLiteral("position"), qBound(0, message.value(QStringLiteral("position")).toInt(), 16 * 1024 * 1024)}};
        if (!cursorAnchor.isEmpty() && !cursorPosition.isEmpty()) {
            forwarded.insert(QStringLiteral("cursorAnchor"), cursorAnchor);
            forwarded.insert(QStringLiteral("cursorPosition"), cursorPosition);
        }
        broadcast(room, forwarded, socket);
    }
}

void CollaborationServer::disconnectPeer(QWebSocket* socket) {
    Peer* peer = peers_.take(socket); if (!peer) return;
    if (!peer->roomId.isEmpty() && rooms_.contains(peer->roomId)) {
        Room& room = *rooms_[peer->roomId]; room.members.remove(peer->peerId);
        QJsonArray memberList;
        for (auto it = room.members.cbegin(); it != room.members.cend(); ++it) {
            const Peer* member = peers_.value(it.value(), nullptr);
            if (member) memberList.append(QJsonObject{{QStringLiteral("id"), member->peerId}, {QStringLiteral("name"), member->displayName}});
        }
        broadcast(room, QJsonObject{{QStringLiteral("type"), QStringLiteral("members")}, {QStringLiteral("members"), memberList}});
        persist(room);
    }
    delete peer; socket->deleteLater();
}

void CollaborationServer::broadcast(Room& room, const QJsonObject& message, QWebSocket* except) {
    const QString frame = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    for (QPointer<QWebSocket> socket : room.members) if (socket && socket != except) socket->sendTextMessage(frame);
}

bool CollaborationServer::persist(Room& room) {
    QString error;
    const QByteArray snapshot = room.document->save(&error);
    if (snapshot.isEmpty()) {
        emit errorOccurred(error.isEmpty() ? QStringLiteral("协作文档快照为空") : error);
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO collaboration_rooms(room_id,snapshot,updated_at) VALUES(?,?,?) ON CONFLICT(room_id) DO UPDATE SET snapshot=excluded.snapshot,updated_at=excluded.updated_at"));
    query.addBindValue(room.id);
    query.addBindValue(snapshot);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        emit errorOccurred(query.lastError().text());
        return false;
    }
    return true;
}
