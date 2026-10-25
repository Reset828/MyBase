#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include "collaboration_server.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MyBaseCollaborationServer"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption databaseOption({QStringLiteral("d"), QStringLiteral("database")}, QStringLiteral("SQLite collaboration store"), QStringLiteral("path"), QStringLiteral("mybase-collaboration.db"));
    QCommandLineOption addressOption({QStringLiteral("a"), QStringLiteral("address")}, QStringLiteral("Bind address (default loopback only)"), QStringLiteral("address"), QStringLiteral("127.0.0.1"));
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")}, QStringLiteral("WebSocket port"), QStringLiteral("port"), QStringLiteral("8765"));
    QCommandLineOption inviteRoomOption(QStringLiteral("create-invite"), QStringLiteral("Create a revocable invitation for room ID"), QStringLiteral("room"));
    QCommandLineOption roleOption(QStringLiteral("role"), QStringLiteral("Invitation role: editor or read-only"), QStringLiteral("role"), QStringLiteral("editor"));
    QCommandLineOption expiryOption(QStringLiteral("expires-hours"), QStringLiteral("Invitation lifetime in hours (1-8760)"), QStringLiteral("hours"), QStringLiteral("168"));
    QCommandLineOption revokeOption(QStringLiteral("revoke-invite"), QStringLiteral("Revoke an invitation token"), QStringLiteral("token"));
    parser.addOption(databaseOption); parser.addOption(addressOption); parser.addOption(portOption);
    parser.addOption(inviteRoomOption); parser.addOption(roleOption); parser.addOption(expiryOption); parser.addOption(revokeOption); parser.process(app);
    bool portOk = false; const uint port = parser.value(portOption).toUInt(&portOk);
    const QHostAddress address(parser.value(addressOption));
    if (!portOk || port == 0 || port > 65535 || address.isNull()) return 2;
    const QString dbPath = QFileInfo(parser.value(databaseOption)).absoluteFilePath();
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    CollaborationServer server(dbPath);
    QObject::connect(&server, &CollaborationServer::errorOccurred, &app, [](const QString& error) { qCritical().noquote() << error; });
    QString error;
    if (!server.initialize(&error)) { qCritical().noquote() << error; return 1; }
    if (parser.isSet(revokeOption)) {
        if (!server.revokeInvite(parser.value(revokeOption), &error)) { qCritical().noquote() << (error.isEmpty() ? QStringLiteral("Invitation not found") : error); return 1; }
        return 0;
    }
    if (parser.isSet(inviteRoomOption)) {
        bool hoursOk = false; const int hours = parser.value(expiryOption).toInt(&hoursOk);
        const QString role = parser.value(roleOption).toLower();
        if (!hoursOk || hours < 1 || hours > 8760 || (role != QStringLiteral("editor") && role != QStringLiteral("read-only"))) return 2;
        const auto inviteRole = role == QStringLiteral("editor") ? CollaborationServer::Role::Editor : CollaborationServer::Role::ReadOnly;
        const QString token = server.createInvite(parser.value(inviteRoomOption), inviteRole, QDateTime::currentMSecsSinceEpoch() + qint64(hours) * 60 * 60 * 1000, &error);
        if (token.isEmpty()) { qCritical().noquote() << error; return 1; }
        qInfo().noquote() << token;
        return 0;
    }
    if (!server.listen(address, static_cast<quint16>(port), &error)) { qCritical().noquote() << error; return 1; }
    qInfo().noquote() << "MyBase collaboration server listening on" << address.toString() << server.serverPort();
    return app.exec();
}
