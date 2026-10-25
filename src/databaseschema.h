#ifndef DATABASESCHEMA_H
#define DATABASESCHEMA_H

#include <QSqlDatabase>

class DatabaseSchema final {
public:
    static constexpr int CurrentVersion = 5;

    static bool migrationRequired(QSqlDatabase& database, bool& required);
    static bool migrate(QSqlDatabase& database);

private:
    static bool createVersionOne(QSqlDatabase& database);
    static bool migrateToVersionTwo(QSqlDatabase& database);
    static bool migrateToVersionThree(QSqlDatabase& database);
    static bool migrateToVersionFour(QSqlDatabase& database);
    static bool migrateToVersionFive(QSqlDatabase& database);
    static bool ensureColumn(QSqlDatabase& database,
                             const QString& table,
                             const QString& column,
                             const QString& declaration);
};

#endif // DATABASESCHEMA_H
