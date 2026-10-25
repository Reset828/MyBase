#include "databaseerrorhandler.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QThread>

DatabaseErrorHandler::DatabaseErrorHandler(QObject* parent)
    : QObject(parent), logFile_(nullptr), errorCount_(0), maxLogFileSize_(10 * 1024 * 1024) {
}

DatabaseErrorHandler::DatabaseErrorHandler(const QString& logFilePath, QObject* parent)
    : QObject(parent), logFilePath_(logFilePath), logFile_(nullptr), errorCount_(0), maxLogFileSize_(10 * 1024 * 1024) {
}

bool DatabaseErrorHandler::handleError(const DatabaseError& error) {
    errorCount_++;
    emit errorOccurred(error);

    bool logged = logError(error);

    if (error.type == DatabaseErrorType::ConnectionError ||
        error.type == DatabaseErrorType::TimeoutError) {
        return true;
    }

    return logged;
}

bool DatabaseErrorHandler::logError(const DatabaseError& error) {
    QMutexLocker locker(&logMutex_);

    if (!openLogFile()) {
        qCritical() << "Failed to open log file:" << logFilePath_;
        return false;
    }

    QTextStream stream(logFile_);

    stream << "[" << error.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz") << "] ";
    stream << "[" << errorTypeToString(error.type) << "] ";
    stream << error.message;
    if (!error.details.isEmpty()) {
        stream << " - " << error.details;
    }
    stream << " (retries: " << error.retryCount << ")" << Qt::endl;

    logFile_->flush();

    if (logFile_->size() > maxLogFileSize_) {
        closeLogFile();
        QString backupPath = logFilePath_ + ".bak";
        QFile::remove(backupPath);
        QFile::rename(logFilePath_, backupPath);
    }

    emit errorLogged(error);
    return true;
}

bool DatabaseErrorHandler::retryOperation(std::function<bool()> operation, int maxRetries) {
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        emit retryAttempted(attempt, maxRetries);

        try {
            if (operation()) {
                emit retrySucceeded(attempt);
                return true;
            }
        } catch (...) {
            qWarning() << "Exception caught during retry attempt" << attempt;
        }

        if (attempt < maxRetries) {
            QThread::sleep(1);
        }
    }

    emit retryFailed(maxRetries);
    return false;
}

void DatabaseErrorHandler::setLogFilePath(const QString& logFilePath) {
    logFilePath_ = logFilePath;
}

QString DatabaseErrorHandler::getLogFilePath() const {
    return logFilePath_;
}

int DatabaseErrorHandler::getErrorCount() const {
    return errorCount_;
}

void DatabaseErrorHandler::clearErrorCount() {
    errorCount_ = 0;
}

QString DatabaseErrorHandler::errorTypeToString(DatabaseErrorType type) {
    switch (type) {
        case DatabaseErrorType::ConnectionError:
            return "ConnectionError";
        case DatabaseErrorType::QueryError:
            return "QueryError";
        case DatabaseErrorType::FileError:
            return "FileError";
        case DatabaseErrorType::PermissionError:
            return "PermissionError";
        case DatabaseErrorType::TimeoutError:
            return "TimeoutError";
        default:
            return "UnknownError";
    }
}

DatabaseErrorType DatabaseErrorHandler::classifyError(const QString& errorMessage) {
    QString lowerMsg = errorMessage.toLower();

    if (lowerMsg.contains("connection") || lowerMsg.contains("open")) {
        return DatabaseErrorType::ConnectionError;
    } else if (lowerMsg.contains("permission") || lowerMsg.contains("access denied") || lowerMsg.contains("denied")) {
        return DatabaseErrorType::PermissionError;
    } else if (lowerMsg.contains("query") || lowerMsg.contains("sql") || lowerMsg.contains("syntax")) {
        return DatabaseErrorType::QueryError;
    } else if (lowerMsg.contains("file") || lowerMsg.contains("disk") || lowerMsg.contains("i/o")) {
        return DatabaseErrorType::FileError;
    } else if (lowerMsg.contains("timeout") || lowerMsg.contains("timed out")) {
        return DatabaseErrorType::TimeoutError;
    }

    return DatabaseErrorType::UnknownError;
}

bool DatabaseErrorHandler::openLogFile() {
    if (logFile_ && logFile_->isOpen()) {
        return true;
    }

    if (logFilePath_.isEmpty()) {
        return false;
    }

    QDir dir = QFileInfo(logFilePath_).absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return false;
        }
    }

    logFile_ = new QFile(logFilePath_);
    if (!logFile_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete logFile_;
        logFile_ = nullptr;
        return false;
    }

    return true;
}

void DatabaseErrorHandler::closeLogFile() {
    if (logFile_ && logFile_->isOpen()) {
        logFile_->close();
    }
    delete logFile_;
    logFile_ = nullptr;
}
