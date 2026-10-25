#ifndef DATABASESQLHELPERS_H
#define DATABASESQLHELPERS_H

#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace database_sql {

inline void bindTagParent(QSqlQuery& query, const QString& placeholder, qint64 parentId) {
    query.bindValue(placeholder, parentId < 0 ? QVariant() : QVariant(parentId));
}

inline qint64 tagParentId(const QVariant& value) {
    return value.isNull() ? -1 : value.toLongLong();
}

} // namespace database_sql

#endif // DATABASESQLHELPERS_H
