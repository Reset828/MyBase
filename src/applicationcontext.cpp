#include "applicationcontext.h"

#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QSettings>
#include <QUrl>
#include "contentdelivery.h"

ApplicationContext::ApplicationContext(const QString& databasePath)
    : databasePath_(databasePath),
      database_(),
      noteManager_(database_.noteRepository()),
      tagManager_(database_.tagRepository(), database_.noteTagRepository()),
      searchEngine_(database_.searchRepository()),
      localFeatures_(database_.connection()),
      collaboration_(databasePath + QStringLiteral(".collaboration-queue")) {
}

bool ApplicationContext::initialize() {
    if (initialized_) {
        return true;
    }

    if (!database_.initialize(databasePath_)) {
        return false;
    }
    const auto builtIns = TemplateService::builtInTemplates();
    for (auto it = builtIns.cbegin(); it != builtIns.cend(); ++it) {
        QJsonObject variables;
        QRegularExpressionMatchIterator matches = QRegularExpression(QStringLiteral("\\{\\{([A-Za-z0-9_]+)\\}\\}")).globalMatch(it.value());
        while (matches.hasNext()) {
            const QString key = matches.next().captured(1);
            variables.insert(key, QJsonObject{{QStringLiteral("default"), QString()},
                                              {QStringLiteral("required"), key == QStringLiteral("title")}});
        }
        const QString category = it.key() == QStringLiteral("周报") ? QStringLiteral("汇报") :
                                it.key() == QStringLiteral("读书笔记") ? QStringLiteral("阅读") :
                                it.key() == QStringLiteral("会议纪要") ? QStringLiteral("会议") : QStringLiteral("写作");
        QSqlQuery query(database_.connection());
        query.prepare(QStringLiteral(R"(
            INSERT INTO note_templates(name,content,is_builtin,modified_time,description,category,variables_json,created_time)
            VALUES(:name,:content,1,:time,:description,:category,:variables,:time)
            ON CONFLICT(name) DO UPDATE SET content=excluded.content,description=excluded.description,
                category=excluded.category,variables_json=excluded.variables_json,modified_time=excluded.modified_time
            WHERE note_templates.is_builtin=1
        )"));
        query.bindValue(QStringLiteral(":name"), it.key());
        query.bindValue(QStringLiteral(":content"), it.value());
        query.bindValue(QStringLiteral(":time"), QDateTime::currentMSecsSinceEpoch());
        query.bindValue(QStringLiteral(":description"), QStringLiteral("MyBase 内置写作模板"));
        query.bindValue(QStringLiteral(":category"), category);
        query.bindValue(QStringLiteral(":variables"), QString::fromUtf8(QJsonDocument(variables).toJson(QJsonDocument::Compact)));
        if (!query.exec()) {
            qWarning() << "Could not seed built-in template" << query.lastError().text();
        }
    }

    QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    ai_.setEndpoint(QUrl(settings.value(QStringLiteral("ai/endpoint")).toString()));
    ai_.setModel(settings.value(QStringLiteral("ai/model")).toString());
    ai_.setStreamingEnabled(settings.value(QStringLiteral("ai/streaming"), true).toBool());
    ai_.setMaxContextChars(settings.value(QStringLiteral("ai/maxContextChars"), 32000).toInt());
    if (!vaultManager_.initialize()) {
        qWarning() << "Vault settings could not be loaded; vault authentication is unavailable";
    }
    initialized_ = true;
    return true;
}
