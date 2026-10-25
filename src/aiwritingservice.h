#ifndef AIWRITINGSERVICE_H
#define AIWRITINGSERVICE_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QByteArray>
#include <QStringList>

class AiWritingService final : public QObject {
    Q_OBJECT
public:
    explicit AiWritingService(QObject* parent = nullptr);
    void setEndpoint(const QUrl& endpoint);
    QUrl endpoint() const;
    void setModel(const QString& model);
    QString model() const;
    void fetchModels();
    void setTimeoutMs(int timeoutMs);
    void setMaxRetries(int retries);
    void setMaxContextChars(int characters);
    void setStreamingEnabled(bool enabled);
    bool streamingEnabled() const;
    bool configured() const;
    bool request(const QString& operation, const QString& markdown);
    void cancel();
signals:
    void completed(const QString& result);
    void failed(const QString& message);
    void requestStateChanged(bool running);
    void streamChunk(const QString& text);
    void streamReset();
    void modelsFetched(const QStringList& models);
    void modelsFetchFailed(const QString& message);
private:
    void sendAttempt();
    void finishAttempt(QNetworkReply* reply);
    bool scheduleRetry(int httpStatus, QNetworkReply::NetworkError networkError);
    void finishFailure(const QString& message);
    void consumeStreamData(const QByteArray& data, bool finalChunk = false);
    void consumeStreamEvent(const QByteArray& payload);
    void finishModelsRequest(QNetworkReply* reply);
    QUrl modelsEndpoint() const;

    QNetworkAccessManager network_;
    QNetworkReply* activeReply_ = nullptr;
    QTimer timeoutTimer_;
    QTimer retryTimer_;
    QTimer modelsTimeoutTimer_;
    int timeoutMs_ = 90000;
    int maxRetries_ = 2;
    int attempt_ = 0;
    QUrl endpoint_;
    QString model_;
    QByteArray requestBody_;
    QByteArray responseBuffer_;
    QByteArray streamPending_;
    qsizetype streamBufferOffset_ = 0;
    QString streamResult_;
    QString streamError_;
    int maxContextChars_ = 32000;
    bool streamingEnabled_ = true;
    QNetworkReply* modelsReply_ = nullptr;
};
#endif
