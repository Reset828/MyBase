#include "collaboration_client.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QCryptographicHash>

namespace {
constexpr int kMaximumQueuedFrames = 2048;
constexpr qsizetype kMaximumQueuedBytes = 64 * 1024 * 1024;

qsizetype queuedBytes(const QStringList& frames) {
    qsizetype total = 0;
    for (const QString& frame : frames) total += frame.size();
    return total;
}
}

CollaborationClient::CollaborationClient(const QString& offlineQueuePath, QObject* parent)
    : QObject(parent), offlineQueuePath_(offlineQueuePath), offlineQueueBasePath_(offlineQueuePath) {
    reconnectTimer_.setInterval(2000);
    reconnectTimer_.setSingleShot(true);
    syncDebounceTimer_.setInterval(150);
    syncDebounceTimer_.setSingleShot(true);
    connect(&syncDebounceTimer_, &QTimer::timeout, this, &CollaborationClient::sendDocumentSync);
    connect(&reconnectTimer_, &QTimer::timeout, this, &CollaborationClient::openSocket);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this] {
        if (joined_ && socket_.state() == QAbstractSocket::ConnectedState) socket_.ping();
    });
    socket_.setMaxAllowedIncomingFrameSize(16 * 1024 * 1024);
    socket_.setMaxAllowedIncomingMessageSize(16 * 1024 * 1024);
    connect(&socket_, &QWebSocket::connected, this, [this] {
        reconnectAttempt_ = 0;
        heartbeatTimer_.start();
        sendJoin();
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        if (intentionalClose_) return;
        heartbeatTimer_.stop();
        joined_ = false; joining_ = false;
        pendingCursors_.clear();
        replaceDocumentOnJoin_ = false;
        if (syncStateOwner_) { AMresultFree(syncStateOwner_); syncStateOwner_ = nullptr; syncState_ = nullptr; }
        resetSyncState();
        emit disconnected();
        if (!serverUrl_.isEmpty()) {
            const int delayMs = qMin(30000, 1000 * (1 << qBound(0, reconnectAttempt_, 5)));
            ++reconnectAttempt_;
            reconnectTimer_.start(delayMs);
            emit statusMessage(QStringLiteral("协作连接将在 %1 秒后重连").arg(delayMs / 1000));
        }
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, &CollaborationClient::handleMessage);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& bytes) { handleMessage(QString::fromUtf8(bytes)); });
}

CollaborationClient::~CollaborationClient() {
    reconnectTimer_.stop();
    socket_.abort();
    if (syncStateOwner_) AMresultFree(syncStateOwner_);
}

bool CollaborationClient::join(const QUrl& server, const QString& room, const QString& invitationToken,
                               const QString& displayName, const QString& initialMarkdown,
                               bool encryptedNote, bool sensitiveContentConfirmed, QString* error) {
    if (encryptedNote) { if (error) *error = QStringLiteral("加密笔记禁止参与实时协作"); return false; }
    if (!sensitiveContentConfirmed) { if (error) *error = QStringLiteral("加入协作前必须确认笔记内容会同步到协作服务器"); return false; }
    if ((server.scheme() != QStringLiteral("ws") && server.scheme() != QStringLiteral("wss")) ||
        server.host().isEmpty() || room.trimmed().isEmpty() || invitationToken.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("协作服务器、房间或邀请令牌无效"); return false;
    }
    leave();
    replaceDocumentOnJoin_ = true; hasJoinedOnce_ = false; readOnly_ = false;
    serverUrl_ = server; roomId_ = room.trimmed(); invitationToken_ = invitationToken.trimmed();
    const QByteArray roomHash = QCryptographicHash::hash(roomId_.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    offlineQueuePath_ = offlineQueueBasePath_ + QStringLiteral(".") + QString::fromLatin1(roomHash) + QStringLiteral(".json");
    queuedFrames_.clear(); nextSequence_ = 1; loadQueue();
    displayName_ = displayName.trimmed().left(48);
    if (!document_.initialize(initialMarkdown, error)) return false;
    if (!resetSyncState(error)) return false;
    openSocket();
    return true;
}

void CollaborationClient::leave() {
    reconnectTimer_.stop(); heartbeatTimer_.stop(); syncDebounceTimer_.stop();
    reconnectAttempt_ = 0;
    serverUrl_.clear(); roomId_.clear(); invitationToken_.clear();
    joined_ = false; joining_ = false; readOnly_ = false; peerId_.clear();
    pendingCursors_.clear();
    intentionalClose_ = true;
    socket_.abort();
    document_.reset();
    if (syncStateOwner_) { AMresultFree(syncStateOwner_); syncStateOwner_ = nullptr; syncState_ = nullptr; }
}

bool CollaborationClient::setLocalMarkdown(const QString& markdown, QString* error) {
    if (readOnly_) { if (error) *error = QStringLiteral("当前协作角色为只读"); return false; }
    if (!document_.replaceText(markdown, error)) return false;
    if (syncState_) syncDebounceTimer_.start();
    return true;
}

void CollaborationClient::sendCursor(int anchor, int position) {
    if (!joined_ || socket_.state() != QAbstractSocket::ConnectedState) return;
    const QByteArray anchorCursor = document_.cursorAt(anchor);
    const QByteArray positionCursor = document_.cursorAt(position);
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("cursor")},
                        {QStringLiteral("anchor"), qMax(0, anchor)},
                        {QStringLiteral("position"), qMax(0, position)}};
    if (!anchorCursor.isEmpty() && !positionCursor.isEmpty()) {
        message.insert(QStringLiteral("cursorAnchor"), QString::fromLatin1(anchorCursor.toBase64()));
        message.insert(QStringLiteral("cursorPosition"), QString::fromLatin1(positionCursor.toBase64()));
    }
    const QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact);
    socket_.sendTextMessage(QString::fromUtf8(frame));
}

bool CollaborationClient::isConnected() const { return socket_.state() == QAbstractSocket::ConnectedState && joined_; }
bool CollaborationClient::isReadOnly() const { return readOnly_; }
QString CollaborationClient::peerId() const { return peerId_; }

bool CollaborationClient::resetSyncState(QString* error) {
    if (syncStateOwner_) { AMresultFree(syncStateOwner_); syncStateOwner_ = nullptr; syncState_ = nullptr; }
    AMresult* state = AMsyncStateInit();
    if (!state || AMresultStatus(state) != AM_STATUS_OK || !AMitemToSyncState(AMresultItem(state), &syncState_)) {
        if (state) AMresultFree(state);
        if (error) *error = QStringLiteral("无法初始化 Automerge 同步状态");
        return false;
    }
    syncStateOwner_ = state;
    return true;
}

void CollaborationClient::openSocket() {
    if (serverUrl_.isEmpty()) return;
    if (socket_.state() != QAbstractSocket::UnconnectedState) { reconnectTimer_.start(); return; }
    intentionalClose_ = false;
    socket_.open(serverUrl_);
}

void CollaborationClient::sendJoin() {
    if (joining_ || roomId_.isEmpty()) return;
    joining_ = true;
    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("join")},
        {QStringLiteral("room"), roomId_}, {QStringLiteral("invite"), invitationToken_}, {QStringLiteral("name"), displayName_},
        {QStringLiteral("snapshot"), QString::fromLatin1(document_.save().toBase64())}}).toJson(QJsonDocument::Compact)));
}

void CollaborationClient::handleMessage(const QString& raw) {
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject message = doc.object(); const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("joined")) {
        const QByteArray snapshot = QByteArray::fromBase64(
            message.value(QStringLiteral("snapshot")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        QString loadError;
        if (snapshot.isEmpty() || (replaceDocumentOnJoin_ && !document_.load(snapshot, &loadError))) { emit statusMessage(loadError.isEmpty() ? QStringLiteral("服务器未提供有效协作快照") : loadError); socket_.close(); return; }
        replaceDocumentOnJoin_ = false; hasJoinedOnce_ = true;
        joining_ = false; joined_ = true;
        peerId_ = message.value(QStringLiteral("peer")).toString();
        readOnly_ = message.value(QStringLiteral("role")).toString() == QStringLiteral("read-only");
        emit roleChanged(readOnly_); emit connected();
        emit markdownChanged(document_.text());
        if (!readOnly_) flushQueue();
        QString error; const QByteArray outgoing = readOnly_ ? QByteArray() : document_.generateSyncMessage(syncState_, &error);
        if (!outgoing.isEmpty()) queueOrSend(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")}, {QStringLiteral("payload"), QString::fromLatin1(outgoing.toBase64())}}).toJson(QJsonDocument::Compact));
    } else if (type == QStringLiteral("sync")) {
        const QByteArray bytes = QByteArray::fromBase64(
            message.value(QStringLiteral("payload")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        QString error;
        if (!document_.receiveSyncMessage(syncState_, bytes, &error)) { emit statusMessage(error); return; }
        emit markdownChanged(document_.text());
        resolvePendingCursors();
        if (queuedFrames_.size() >= kMaximumQueuedFrames) {
            emit statusMessage(QStringLiteral("离线同步队列已满，等待确认后继续同步"));
            return;
        }
        const QByteArray response = document_.generateSyncMessage(syncState_, &error);
        if (!response.isEmpty()) queueOrSend(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")}, {QStringLiteral("payload"), QString::fromLatin1(response.toBase64())}}).toJson(QJsonDocument::Compact));
    } else if (type == QStringLiteral("ack")) {
        const qint64 acknowledged = message.value(QStringLiteral("sequence")).toVariant().toLongLong();
        for (qsizetype i = queuedFrames_.size() - 1; i >= 0; --i) {
            const QJsonDocument queued = QJsonDocument::fromJson(QByteArray::fromBase64(
                queuedFrames_.at(i).toLatin1(), QByteArray::AbortOnBase64DecodingErrors));
            if (queued.isObject() && queued.object().value(QStringLiteral("sequence")).toVariant().toLongLong() == acknowledged) queuedFrames_.removeAt(i);
        }
        saveQueue();
        if (joined_ && !readOnly_) syncDebounceTimer_.start();
    } else if (type == QStringLiteral("members")) {
        QStringList members; for (const QJsonValue& value : message.value(QStringLiteral("members")).toArray()) {
            if (value.isObject()) members.append(value.toObject().value(QStringLiteral("id")).toString());
            else if (value.isString()) members.append(value.toString());
        }
        for (auto it = pendingCursors_.begin(); it != pendingCursors_.end();) {
            if (!members.contains(it.key())) it = pendingCursors_.erase(it);
            else ++it;
        }
        emit membersChanged(members);
    } else if (type == QStringLiteral("cursor")) {
        int anchor = message.value(QStringLiteral("anchor")).toInt();
        int position = message.value(QStringLiteral("position")).toInt();
        const QString peer = message.value(QStringLiteral("peer")).toString();
        const QString name = message.value(QStringLiteral("name")).toString();
        const QString encodedAnchor = message.value(QStringLiteral("cursorAnchor")).toString();
        const QString encodedPosition = message.value(QStringLiteral("cursorPosition")).toString();
        if (!encodedAnchor.isEmpty() && !encodedPosition.isEmpty() &&
            encodedAnchor.size() <= 1024 && encodedPosition.size() <= 1024) {
            const QByteArray anchorBytes = QByteArray::fromBase64(encodedAnchor.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            const QByteArray positionBytes = QByteArray::fromBase64(encodedPosition.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            const int stableAnchor = document_.positionOfCursor(anchorBytes);
            const int stablePosition = document_.positionOfCursor(positionBytes);
            if (stableAnchor >= 0 && stablePosition >= 0) {
                anchor = stableAnchor;
                position = stablePosition;
                pendingCursors_.remove(peer);
            } else if (!peer.isEmpty() && !anchorBytes.isEmpty() && !positionBytes.isEmpty()) {
                pendingCursors_.insert(peer, PendingCursor{name, anchorBytes, positionBytes});
            }
        }
        emit remoteCursor(peer, name, anchor, position);
    } else if (type == QStringLiteral("error")) emit statusMessage(message.value(QStringLiteral("message")).toString());
}

void CollaborationClient::queueOrSend(const QByteArray& frame) {
    QJsonDocument source = QJsonDocument::fromJson(frame);
    if (!source.isObject()) return;
    QJsonObject message = source.object();
    const qint64 sequence = nextSequence_++;
    message.insert(QStringLiteral("sequence"), sequence);
    const QByteArray sequenced = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const QString encoded = QString::fromLatin1(sequenced.toBase64());
    if (queuedFrames_.size() >= kMaximumQueuedFrames ||
        queuedBytes(queuedFrames_) + encoded.size() > kMaximumQueuedBytes) {
        resetSyncState();
        emit statusMessage(QStringLiteral("离线同步队列已达到容量上限，已停止缓存新操作"));
        return;
    }
    queuedFrames_.append(encoded);
    if (!saveQueue()) {
        queuedFrames_.removeLast();
        resetSyncState();
        emit statusMessage(QStringLiteral("无法持久化离线同步操作"));
        return;
    }
    if (joined_ && socket_.state() == QAbstractSocket::ConnectedState) socket_.sendTextMessage(QString::fromUtf8(sequenced));
}

void CollaborationClient::sendDocumentSync() {
    if (readOnly_ || !syncState_) return;
    if (queuedFrames_.size() >= kMaximumQueuedFrames) {
        emit statusMessage(QStringLiteral("离线同步队列已满，等待确认后继续同步"));
        return;
    }
    QString error;
    const QByteArray payload = document_.generateSyncMessage(syncState_, &error);
    if (!payload.isEmpty()) queueOrSend(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync")},
        {QStringLiteral("payload"), QString::fromLatin1(payload.toBase64())}}).toJson(QJsonDocument::Compact));
    else if (!error.isEmpty()) emit statusMessage(error);
}

void CollaborationClient::flushQueue() {
    if (!joined_) return;
    for (const QString& encoded : queuedFrames_) {
        const QByteArray frame = QByteArray::fromBase64(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!frame.isEmpty()) socket_.sendTextMessage(QString::fromUtf8(frame));
    }
}

bool CollaborationClient::saveQueue() {
    const QFileInfo info(offlineQueuePath_); QDir().mkpath(info.absolutePath());
    QSaveFile file(offlineQueuePath_); if (!file.open(QIODevice::WriteOnly)) return false;
    QJsonArray items; for (const QString& item : queuedFrames_) items.append(item);
    const QByteArray payload = QJsonDocument(items).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size()) { file.cancelWriting(); return false; }
    return file.commit();
}

void CollaborationClient::loadQueue() {
    QFile file(offlineQueuePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumQueuedBytes) {
        if (file.exists()) emit statusMessage(QStringLiteral("离线同步队列文件无效或超过容量上限"));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        emit statusMessage(QStringLiteral("离线同步队列格式无效，已忽略"));
        return;
    }
    for (const QJsonValue& value : doc.array()) {
        if (!value.isString() || queuedFrames_.size() >= kMaximumQueuedFrames) break;
        const QString encoded = value.toString();
        if (queuedBytes(queuedFrames_) + encoded.size() > kMaximumQueuedBytes) break;
        const QJsonDocument frame = QJsonDocument::fromJson(QByteArray::fromBase64(
            encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors));
        if (!frame.isObject() || frame.object().value(QStringLiteral("type")).toString() != QStringLiteral("sync")) continue;
        const qint64 sequence = frame.object().value(QStringLiteral("sequence")).toVariant().toLongLong();
        if (sequence <= 0) continue;
        queuedFrames_.append(encoded);
        nextSequence_ = qMax(nextSequence_, sequence + 1);
    }
    if (!saveQueue()) emit statusMessage(QStringLiteral("无法整理本地离线同步队列"));
}

void CollaborationClient::resolvePendingCursors() {
    for (auto it = pendingCursors_.begin(); it != pendingCursors_.end();) {
        const int anchor = document_.positionOfCursor(it->anchor);
        const int position = document_.positionOfCursor(it->position);
        if (anchor < 0 || position < 0) {
            ++it;
            continue;
        }
        const QString peer = it.key();
        const QString name = it->name;
        it = pendingCursors_.erase(it);
        emit remoteCursor(peer, name, anchor, position);
    }
}
