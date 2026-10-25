#include "localfeatures.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QJsonDocument>
#include <QSqlRecord>

LocalFeatures::LocalFeatures(QSqlDatabase& database) : database_(database) {}

QVector<NoteRevision> LocalFeatures::revisions(qint64 noteId) const {
    QVector<NoteRevision> result;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id,title,content,created_time FROM note_revisions WHERE note_id=:id ORDER BY created_time DESC,id DESC"));
    query.bindValue(QStringLiteral(":id"), noteId);
    if (!query.exec()) return result;
    while (query.next()) result.append({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), query.value(3).toLongLong()});
    return result;
}

bool LocalFeatures::addBookmark(qint64 noteId, const QString& label, int position, const QString& anchor, const QString& fingerprint) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO bookmarks(note_id,label,position,anchor,content_fingerprint,created_time) VALUES(:note,:label,:position,:anchor,:fingerprint,:time)"));
    query.bindValue(QStringLiteral(":note"), noteId);
    query.bindValue(QStringLiteral(":label"), label);
    query.bindValue(QStringLiteral(":position"), qMax(0, position));
    query.bindValue(QStringLiteral(":anchor"), anchor);
    query.bindValue(QStringLiteral(":fingerprint"), fingerprint);
    query.bindValue(QStringLiteral(":time"), QDateTime::currentMSecsSinceEpoch());
    return query.exec();
}

QVector<NoteBookmark> LocalFeatures::bookmarks(qint64 noteId) const {
    QVector<NoteBookmark> result;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id,label,position,anchor,content_fingerprint,created_time FROM bookmarks WHERE note_id=:id ORDER BY created_time"));
    query.bindValue(QStringLiteral(":id"), noteId);
    if (!query.exec()) return result;
    while (query.next()) {
        NoteBookmark bookmark;
        bookmark.id = query.value(0).toLongLong(); bookmark.noteId = noteId;
        bookmark.label = query.value(1).toString(); bookmark.position = query.value(2).toInt();
        bookmark.anchor = query.value(3).toString(); bookmark.contentFingerprint = query.value(4).toString();
        bookmark.createdTime = query.value(5).toLongLong(); result.append(bookmark);
    }
    return result;
}

QVector<NoteBookmark> LocalFeatures::allBookmarks() const {
    QVector<NoteBookmark> result;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id,note_id,label,position,anchor,content_fingerprint,created_time FROM bookmarks ORDER BY created_time DESC"))) return result;
    while (query.next()) {
        NoteBookmark bookmark;
        bookmark.id = query.value(0).toLongLong(); bookmark.noteId = query.value(1).toLongLong();
        bookmark.label = query.value(2).toString(); bookmark.position = query.value(3).toInt();
        bookmark.anchor = query.value(4).toString(); bookmark.contentFingerprint = query.value(5).toString();
        bookmark.createdTime = query.value(6).toLongLong(); result.append(bookmark);
    }
    return result;
}

bool LocalFeatures::renameBookmark(qint64 bookmarkId, const QString& label) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE bookmarks SET label=:label WHERE id=:id"));
    query.bindValue(QStringLiteral(":label"), label.trimmed()); query.bindValue(QStringLiteral(":id"), bookmarkId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool LocalFeatures::updateBookmarkPosition(qint64 bookmarkId, int position, const QString& anchor, const QString& fingerprint) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE bookmarks SET position=:position,anchor=:anchor,content_fingerprint=:fingerprint WHERE id=:id"));
    query.bindValue(QStringLiteral(":position"), qMax(0, position)); query.bindValue(QStringLiteral(":anchor"), anchor);
    query.bindValue(QStringLiteral(":fingerprint"), fingerprint); query.bindValue(QStringLiteral(":id"), bookmarkId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool LocalFeatures::removeBookmark(qint64 bookmarkId) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM bookmarks WHERE id=:id"));
    query.bindValue(QStringLiteral(":id"), bookmarkId);
    return query.exec() && query.numRowsAffected() > 0;
}

bool LocalFeatures::saveTemplate(const QString& name, const QString& content, QString* error) {
    NoteTemplate item;
    item.name = name;
    item.content = content;
    return saveTemplate(item, error);
}

bool LocalFeatures::saveTemplate(const NoteTemplate& item, QString* error) {
    if (item.name.trimmed().isEmpty() || item.content.isNull()) {
        if (error) *error = QStringLiteral("模板名称和正文不能为空");
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(database_);
    if (item.id > 0) {
        query.prepare(QStringLiteral("UPDATE note_templates SET name=:name,content=:content,description=:description,category=:category,variables_json=:variables,modified_time=:modified WHERE id=:id AND is_builtin=0"));
        query.bindValue(QStringLiteral(":id"), item.id);
    } else {
        query.prepare(QStringLiteral("INSERT INTO note_templates(name,content,is_builtin,modified_time,description,category,variables_json,created_time) VALUES(:name,:content,0,:modified,:description,:category,:variables,:created)"));
        query.bindValue(QStringLiteral(":created"), now);
    }
    query.bindValue(QStringLiteral(":name"), item.name.trimmed());
    query.bindValue(QStringLiteral(":content"), item.content);
    query.bindValue(QStringLiteral(":description"), item.description);
    query.bindValue(QStringLiteral(":category"), item.category);
    query.bindValue(QStringLiteral(":variables"), QString::fromUtf8(QJsonDocument(item.variables).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":modified"), now);
    if (!query.exec() || (item.id > 0 && query.numRowsAffected() == 0)) {
        if (error) *error = query.lastError().text().isEmpty() ? QStringLiteral("内置模板不可修改") : query.lastError().text();
        return false;
    }
    return true;
}

QVector<NoteTemplate> LocalFeatures::templates() const {
    QVector<NoteTemplate> result;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id,name,content,is_builtin,description,category,variables_json,created_time,modified_time FROM note_templates ORDER BY is_builtin DESC,name"))) return result;
    while (query.next()) {
        const QJsonDocument variables = QJsonDocument::fromJson(query.value(6).toString().toUtf8());
        NoteTemplate item;
        item.id = query.value(0).toLongLong(); item.name = query.value(1).toString();
        item.content = query.value(2).toString(); item.builtIn = query.value(3).toBool();
        item.description = query.value(4).toString(); item.category = query.value(5).toString();
        item.variables = variables.object(); item.createdTime = query.value(7).toLongLong();
        item.modifiedTime = query.value(8).toLongLong();
        result.append(item);
    }
    return result;
}

NoteTemplate LocalFeatures::templateById(qint64 templateId) const {
    for (const NoteTemplate& item : templates()) if (item.id == templateId) return item;
    return {};
}

bool LocalFeatures::duplicateTemplate(qint64 templateId, const QString& newName, QString* error) {
    NoteTemplate item = templateById(templateId);
    if (item.id <= 0) {
        if (error) *error = QStringLiteral("模板不存在");
        return false;
    }
    item.id = -1;
    item.name = newName;
    return saveTemplate(item, error);
}

bool LocalFeatures::importTemplate(const QJsonObject& json, QString* error) {
    if (json.value(QStringLiteral("format")).toString() != QStringLiteral("mybase-template-v1") ||
        !json.value(QStringLiteral("name")).isString() || !json.value(QStringLiteral("content")).isString()) {
        if (error) *error = QStringLiteral("模板文件格式无效");
        return false;
    }
    NoteTemplate item;
    item.name = json.value(QStringLiteral("name")).toString();
    item.content = json.value(QStringLiteral("content")).toString();
    item.description = json.value(QStringLiteral("description")).toString();
    item.category = json.value(QStringLiteral("category")).toString();
    item.variables = json.value(QStringLiteral("variables")).toObject();
    return saveTemplate(item, error);
}

QJsonObject LocalFeatures::exportTemplate(qint64 templateId) const {
    const NoteTemplate item = templateById(templateId);
    if (item.id <= 0) return {};
    return {{QStringLiteral("format"), QStringLiteral("mybase-template-v1")},
            {QStringLiteral("name"), item.name}, {QStringLiteral("content"), item.content},
            {QStringLiteral("description"), item.description}, {QStringLiteral("category"), item.category},
            {QStringLiteral("variables"), item.variables}};
}

bool LocalFeatures::removeTemplate(qint64 templateId) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM note_templates WHERE id=:id AND is_builtin=0"));
    query.bindValue(QStringLiteral(":id"), templateId);
    return query.exec() && query.numRowsAffected() > 0;
}
