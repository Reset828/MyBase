#ifndef NOTE_H
#define NOTE_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>

struct Note {
    qint64 id;
    QString title;
    QString content;
    QDateTime createdTime;
    QDateTime modifiedTime;
    bool isDeleted;
    QDateTime deletedTime;
    bool isEncrypted;
    QString encryptionIv;

    Note()
        : id(-1), isDeleted(false), isEncrypted(false) {}

    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = id;
        json["title"] = title;
        json["content"] = content;
        json["created_time"] = createdTime.toMSecsSinceEpoch();
        json["modified_time"] = modifiedTime.toMSecsSinceEpoch();
        json["is_deleted"] = isDeleted;
        json["deleted_time"] = deletedTime.toMSecsSinceEpoch();
        json["is_encrypted"] = isEncrypted;
        json["encryption_iv"] = encryptionIv;
        return json;
    }

    static Note fromJson(const QJsonObject& json) {
        Note note;
        note.id = json["id"].toVariant().toLongLong();
        note.title = json["title"].toString();
        note.content = json["content"].toString();
        note.createdTime = QDateTime::fromMSecsSinceEpoch(json["created_time"].toVariant().toLongLong());
        note.modifiedTime = QDateTime::fromMSecsSinceEpoch(json["modified_time"].toVariant().toLongLong());
        note.isDeleted = json["is_deleted"].toBool();
        note.deletedTime = QDateTime::fromMSecsSinceEpoch(json["deleted_time"].toVariant().toLongLong());
        note.isEncrypted = json["is_encrypted"].toBool(false);
        note.encryptionIv = json["encryption_iv"].toString();
        return note;
    }
};

#endif // NOTE_H
