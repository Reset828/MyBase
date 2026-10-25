#ifndef TAG_H
#define TAG_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>

struct Tag {
    qint64 id;
    QString name;
    qint64 parentId;
    int sortOrder;
    QDateTime createdTime;

    Tag()
        : id(-1), parentId(-1), sortOrder(0) {}

    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = id;
        json["name"] = name;
        json["parent_id"] = parentId;
        json["sort_order"] = sortOrder;
        json["created_time"] = createdTime.toMSecsSinceEpoch();
        return json;
    }

    static Tag fromJson(const QJsonObject& json) {
        Tag tag;
        tag.id = json["id"].toVariant().toLongLong();
        tag.name = json["name"].toString();
        tag.parentId = json["parent_id"].toVariant().toLongLong();
        tag.sortOrder = json["sort_order"].toInt(0);
        tag.createdTime = QDateTime::fromMSecsSinceEpoch(json["created_time"].toVariant().toLongLong());
        return tag;
    }
};

#endif // TAG_H
