#ifndef LOCALFEATURES_H
#define LOCALFEATURES_H

#include <QSqlDatabase>
#include <QVector>
#include <QString>
#include <QMap>
#include <QJsonObject>

struct NoteRevision { qint64 id = -1; QString title; QString content; qint64 createdTime = 0; };
struct NoteBookmark {
    qint64 id = -1;
    qint64 noteId = -1;
    QString label;
    int position = 0;
    QString anchor;
    QString contentFingerprint;
    qint64 createdTime = 0;
};
struct NoteTemplate {
    qint64 id = -1;
    QString name;
    QString content;
    bool builtIn = false;
    QString description;
    QString category;
    QJsonObject variables;
    qint64 createdTime = 0;
    qint64 modifiedTime = 0;
};

class LocalFeatures final {
public:
    explicit LocalFeatures(QSqlDatabase& database);
    QVector<NoteRevision> revisions(qint64 noteId) const;
    bool addBookmark(qint64 noteId, const QString& label, int position,
                     const QString& anchor = QString(), const QString& fingerprint = QString());
    bool renameBookmark(qint64 bookmarkId, const QString& label);
    bool updateBookmarkPosition(qint64 bookmarkId, int position, const QString& anchor, const QString& fingerprint);
    QVector<NoteBookmark> bookmarks(qint64 noteId) const;
    QVector<NoteBookmark> allBookmarks() const;
    bool removeBookmark(qint64 bookmarkId);
    bool saveTemplate(const QString& name, const QString& content, QString* error = nullptr);
    bool saveTemplate(const NoteTemplate& item, QString* error = nullptr);
    bool duplicateTemplate(qint64 templateId, const QString& newName, QString* error = nullptr);
    NoteTemplate templateById(qint64 templateId) const;
    bool importTemplate(const QJsonObject& json, QString* error = nullptr);
    QJsonObject exportTemplate(qint64 templateId) const;
    QVector<NoteTemplate> templates() const;
    bool removeTemplate(qint64 templateId);
private:
    QSqlDatabase& database_;
};

#endif
