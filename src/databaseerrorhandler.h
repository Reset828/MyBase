#ifndef DATABASEERRORHANDLER_H
#define DATABASEERRORHANDLER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <functional>

enum class DatabaseErrorType {
    ConnectionError,
    QueryError,
    FileError,
    PermissionError,
    TimeoutError,
    UnknownError
};

struct DatabaseError {
    DatabaseErrorType type;
    QString message;
    QString details;
    QDateTime timestamp;
    int retryCount;

    DatabaseError()
        : type(DatabaseErrorType::UnknownError), retryCount(0) {
        timestamp = QDateTime::currentDateTime();
    }
};

class DatabaseErrorHandler : public QObject {
    Q_OBJECT

public:
    explicit DatabaseErrorHandler(QObject* parent = nullptr);
    explicit DatabaseErrorHandler(const QString& logFilePath, QObject* parent = nullptr);

    bool handleError(const DatabaseError& error);
    bool logError(const DatabaseError& error);
    bool retryOperation(std::function<bool()> operation, int maxRetries = 3);

    void setLogFilePath(const QString& logFilePath);
    QString getLogFilePath() const;

    int getErrorCount() const;
    void clearErrorCount();

    static QString errorTypeToString(DatabaseErrorType type);
    static DatabaseErrorType classifyError(const QString& errorMessage);

signals:
    void errorOccurred(const DatabaseError& error);
    void errorLogged(const DatabaseError& error);
    void retryAttempted(int attempt, int maxRetries);
    void retrySucceeded(int attempt);
    void retryFailed(int maxRetries);

private:
    bool openLogFile();
    void closeLogFile();

    QString logFilePath_;
    QFile* logFile_;
    QMutex logMutex_;
    int errorCount_;
    int maxLogFileSize_;
};

#endif // DATABASEERRORHANDLER_H
