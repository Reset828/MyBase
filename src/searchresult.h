#ifndef SEARCHRESULT_H
#define SEARCHRESULT_H

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QJsonObject>
#include <QJsonDocument>

struct SearchResult {
    qint64 noteId;
    QString title;
    QString snippet;
    double relevanceScore;
    QDateTime modifiedTime;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["noteId"] = noteId;
        obj["title"] = title;
        obj["snippet"] = snippet;
        obj["relevanceScore"] = relevanceScore;
        obj["modifiedTime"] = modifiedTime.toString(Qt::ISODate);
        return obj;
    }

    static SearchResult fromJson(const QJsonObject& obj) {
        SearchResult result;
        result.noteId = obj["noteId"].toInteger();
        result.title = obj["title"].toString();
        result.snippet = obj["snippet"].toString();
        result.relevanceScore = obj["relevanceScore"].toDouble();
        result.modifiedTime = QDateTime::fromString(obj["modifiedTime"].toString(), Qt::ISODate);
        return result;
    }
};

#endif // SEARCHRESULT_H
