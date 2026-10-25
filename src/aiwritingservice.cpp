#include "aiwritingservice.h"
#include "aicredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QNetworkRequest>

namespace {

QString textFromJsonValue(const QJsonValue& value) {
    if (value.isString()) return value.toString();
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString& key : {QStringLiteral("text"), QStringLiteral("content"),
                                   QStringLiteral("output_text"), QStringLiteral("response"),
                                   QStringLiteral("answer"), QStringLiteral("generated_text")}) {
            const QString nested = textFromJsonValue(object.value(key));
            if (!nested.isEmpty()) return nested;
        }
        return {};
    }
    if (!value.isArray()) return {};

    QString result;
    for (const QJsonValue& part : value.toArray()) {
        if (part.isString()) {
            result += part.toString();
            continue;
        }
        if (!part.isObject()) continue;
        const QJsonObject object = part.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type.isEmpty() || type == QStringLiteral("text") || type == QStringLiteral("output_text") ||
            type == QStringLiteral("text_delta")) result += textFromJsonValue(object);
    }
    return result;
}

QString completionText(const QJsonObject& root) {
    const QString outputText = textFromJsonValue(root.value(QStringLiteral("output_text")));
    if (!outputText.isEmpty()) return outputText;

    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    for (const QJsonValue& item : output) {
        if (!item.isObject()) continue;
        const QString text = textFromJsonValue(item.toObject().value(QStringLiteral("content")));
        if (!text.isEmpty()) return text;
    }

    for (const QString& key : {QStringLiteral("response"), QStringLiteral("answer"),
                               QStringLiteral("generated_text"), QStringLiteral("completion"),
                               QStringLiteral("result"), QStringLiteral("content"),
                               QStringLiteral("text"), QStringLiteral("message"), QStringLiteral("data")}) {
        const QString text = textFromJsonValue(root.value(key));
        if (!text.isEmpty()) return text;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return {};
    const QJsonObject choice = choices.first().toObject();
    const QString delta = textFromJsonValue(choice.value(QStringLiteral("delta")));
    if (!delta.isEmpty()) return delta;
    const QString message = textFromJsonValue(choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")));
    if (!message.isEmpty()) return message;
    return choice.value(QStringLiteral("text")).toString();
}

QString streamChunkText(const QJsonObject& event) {
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("response.output_text.delta")) return event.value(QStringLiteral("delta")).toString();
    if (type == QStringLiteral("response.output_text.done")) return {};
    if (type.startsWith(QStringLiteral("response."))) return {};

    const QJsonObject deltaObject = event.value(QStringLiteral("delta")).toObject();
    if (type == QStringLiteral("content_block_delta") || type == QStringLiteral("text_delta")) {
        const QString text = textFromJsonValue(deltaObject);
        if (!text.isEmpty()) return text;
    }

    const QJsonArray choices = event.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject choice = choices.first().toObject();
        const QString content = textFromJsonValue(choice.value(QStringLiteral("delta")));
        if (!content.isEmpty()) return content;
        const QString message = textFromJsonValue(choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")));
        if (!message.isEmpty()) return message;
        return choice.value(QStringLiteral("text")).toString();
    }
    const QString message = textFromJsonValue(event.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")));
    if (!message.isEmpty()) return message;
    const QString response = textFromJsonValue(event.value(QStringLiteral("response")));
    if (!response.isEmpty()) return response;
    const QString content = textFromJsonValue(event.value(QStringLiteral("content")));
    if (!content.isEmpty()) return content;
    return textFromJsonValue(event.value(QStringLiteral("delta")));
}

QString apiErrorText(const QJsonObject& root) {
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isString()) return errorValue.toString();
    const QJsonObject error = errorValue.toObject();
    if (error.isEmpty()) return {};
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString code = error.value(QStringLiteral("code")).toString();
    if (!message.isEmpty()) return code.isEmpty() ? message : QStringLiteral("%1（%2）").arg(message, code);
    return code;
}

QStringList modelsFromJson(const QJsonDocument& document) {
    QJsonArray values;
    if (document.isArray()) {
        values = document.array();
    } else if (document.isObject()) {
        const QJsonObject root = document.object();
        values = root.value(QStringLiteral("data")).toArray();
        if (values.isEmpty()) values = root.value(QStringLiteral("models")).toArray();
        if (values.isEmpty()) values = root.value(QStringLiteral("result")).toObject().value(QStringLiteral("data")).toArray();
    }

    QStringList models;
    for (const QJsonValue& value : values) {
        QString id;
        if (value.isString()) {
            id = value.toString();
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            id = object.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) id = object.value(QStringLiteral("name")).toString();
            if (id.isEmpty()) id = object.value(QStringLiteral("model")).toString();
        }
        if (!id.trimmed().isEmpty() && !models.contains(id.trimmed())) models.append(id.trimmed());
    }
    models.sort(Qt::CaseInsensitive);
    return models;
}

QString httpErrorMessage(int status, QNetworkReply::NetworkError networkError, const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QString serviceMessage = document.isObject() ? apiErrorText(document.object()) : QString();
    if (status == 401 || status == 403) return QStringLiteral("AI 认证失败：请检查 API Key 和接口权限");
    if (status == 429) return QStringLiteral("AI 服务请求过于频繁或额度不足（HTTP 429）");
    if (status >= 500) return QStringLiteral("AI 服务暂时不可用（HTTP %1）").arg(status);
    if (networkError != QNetworkReply::NoError) return QStringLiteral("AI 网络请求失败（错误 %1）").arg(static_cast<int>(networkError));
    if (!serviceMessage.isEmpty()) return QStringLiteral("AI 服务返回错误：%1").arg(serviceMessage);
    return QStringLiteral("AI 服务请求失败（HTTP %1）").arg(status);
}

bool isEventStream(const QString& contentType, const QByteArray& body) {
    const QByteArray trimmed = body.trimmed();
    return contentType.contains(QStringLiteral("text/event-stream"), Qt::CaseInsensitive) ||
           trimmed.startsWith(QByteArrayLiteral("data:")) ||
           trimmed.startsWith(QByteArrayLiteral("event:")) ||
           trimmed.contains(QByteArrayLiteral("\ndata:"));
}

bool isJsonLinesContentType(const QString& contentType) {
    return contentType.contains(QStringLiteral("ndjson"), Qt::CaseInsensitive) ||
           contentType.contains(QStringLiteral("jsonl"), Qt::CaseInsensitive) ||
           contentType.contains(QStringLiteral("stream+json"), Qt::CaseInsensitive);
}

bool isJsonLinesPayload(const QByteArray& body) {
    int objectLines = 0;
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        const QJsonDocument document = QJsonDocument::fromJson(trimmed);
        if (!document.isObject()) return false;
        ++objectLines;
    }
    return objectLines > 0;
}

QString responseFormatDetails(const QByteArray& body, const QString& contentType) {
    QStringList details;
    if (!contentType.trimmed().isEmpty()) details.append(QStringLiteral("Content-Type: %1").arg(contentType));
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        QStringList keys = document.object().keys();
        keys.sort(Qt::CaseInsensitive);
        if (!keys.isEmpty()) details.append(QStringLiteral("JSON 字段: %1").arg(keys.join(QStringLiteral(", "))));
        const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
        details.append(QStringLiteral("choices 数量: %1").arg(choices.size()));
    } else if (document.isArray()) {
        details.append(QStringLiteral("JSON 根节点为数组"));
    } else {
        details.append(QStringLiteral("响应不是完整 JSON 对象"));
    }
    return details.join(QStringLiteral("；"));
}

} // namespace

AiWritingService::AiWritingService(QObject* parent) : QObject(parent) {
    timeoutTimer_.setSingleShot(true);
    retryTimer_.setSingleShot(true);
    modelsTimeoutTimer_.setSingleShot(true);

    connect(&timeoutTimer_, &QTimer::timeout, this, [this]() {
        if (!activeReply_) return;
        QNetworkReply* reply = activeReply_;
        activeReply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
        if (!scheduleRetry(0, QNetworkReply::TimeoutError)) finishFailure(QStringLiteral("AI 请求超时；请检查网络并重试"));
    });
    connect(&retryTimer_, &QTimer::timeout, this, [this]() { sendAttempt(); });
    connect(&modelsTimeoutTimer_, &QTimer::timeout, this, [this]() {
        if (!modelsReply_) return;
        QNetworkReply* reply = modelsReply_;
        modelsReply_ = nullptr;
        reply->abort();
        reply->deleteLater();
        emit modelsFetchFailed(QStringLiteral("获取模型列表超时；请检查接口地址和网络"));
    });
}

void AiWritingService::setEndpoint(const QUrl& endpoint) { endpoint_ = endpoint; }
QUrl AiWritingService::endpoint() const { return endpoint_; }
void AiWritingService::setModel(const QString& model) { model_ = model.trimmed(); }
QString AiWritingService::model() const { return model_; }
void AiWritingService::setTimeoutMs(int timeoutMs) { timeoutMs_ = qBound(1000, timeoutMs, 10 * 60 * 1000); }
void AiWritingService::setMaxRetries(int retries) { maxRetries_ = qBound(0, retries, 5); }
void AiWritingService::setMaxContextChars(int characters) { maxContextChars_ = qBound(1000, characters, 1000000); }
void AiWritingService::setStreamingEnabled(bool enabled) { streamingEnabled_ = enabled; }
bool AiWritingService::streamingEnabled() const { return streamingEnabled_; }
bool AiWritingService::configured() const {
    return endpoint_.isValid() && !endpoint_.isEmpty() &&
           endpoint_.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           !endpoint_.host().isEmpty() &&
           !model_.trimmed().isEmpty() && !AiCredentialStore::loadApiKey().isEmpty();
}

QUrl AiWritingService::modelsEndpoint() const {
    QUrl url = endpoint_;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && !path.isEmpty()) path.chop(1);
    const QString completionsSuffix = QStringLiteral("/chat/completions");
    if (path.endsWith(completionsSuffix, Qt::CaseInsensitive)) {
        path.chop(completionsSuffix.size());
    } else if (path.endsWith(QStringLiteral("/completions"), Qt::CaseInsensitive)) {
        path.chop(QStringLiteral("/completions").size());
    } else if (path.endsWith(QStringLiteral("/models"), Qt::CaseInsensitive)) {
        return url;
    }
    if (!path.endsWith(QLatin1Char('/'))) path.append(QLatin1Char('/'));
    path.append(QStringLiteral("models"));
    url.setPath(path);
    return url;
}

void AiWritingService::fetchModels() {
    if (modelsReply_) return;
    const QString apiKey = AiCredentialStore::loadApiKey();
    if (apiKey.isEmpty() || !endpoint_.isValid() || endpoint_.host().isEmpty() || endpoint_.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        emit modelsFetchFailed(QStringLiteral("请先配置有效 HTTPS 接口地址和 API 密钥"));
        return;
    }

    QNetworkRequest request(modelsEndpoint());
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    modelsReply_ = network_.get(request);
    QNetworkReply* const reply = modelsReply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishModelsRequest(reply); });
    modelsTimeoutTimer_.start(qMin(timeoutMs_, 60000));
}

void AiWritingService::finishModelsRequest(QNetworkReply* reply) {
    if (reply != modelsReply_) {
        reply->deleteLater();
        return;
    }
    modelsTimeoutTimer_.stop();
    modelsReply_ = nullptr;
    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        emit modelsFetchFailed(httpErrorMessage(status, networkError, body));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject() && !apiErrorText(document.object()).isEmpty()) {
        emit modelsFetchFailed(QStringLiteral("获取模型列表失败：%1").arg(apiErrorText(document.object())));
        return;
    }
    const QStringList models = modelsFromJson(document);
    if (models.isEmpty()) {
        emit modelsFetchFailed(QStringLiteral("接口返回成功，但未找到可用模型列表"));
        return;
    }
    emit modelsFetched(models);
}

bool AiWritingService::request(const QString& operation, const QString& markdown) {
    if (activeReply_ || retryTimer_.isActive()) return false;
    const QString apiKey = AiCredentialStore::loadApiKey();
    if (apiKey.isEmpty() || !endpoint_.isValid() || endpoint_.host().isEmpty() || endpoint_.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || model_.trimmed().isEmpty()) {
        emit failed(QStringLiteral("请先配置有效 HTTPS 接口、API 密钥和模型"));
        return false;
    }
    QString boundedContext = markdown;
    if (boundedContext.size() > maxContextChars_) {
        const QString omission = QStringLiteral("\n[中间内容已省略]\n");
        const int contentBudget = qMax(0, maxContextChars_ - omission.size());
        const int headSize = contentBudget / 2;
        boundedContext = boundedContext.left(headSize) + omission + boundedContext.right(contentBudget - headSize);
    }
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), QStringLiteral("仅执行所请求的写作操作，返回可直接替换原文的 Markdown，不要添加解释。操作：%1\n\n原文：\n%2").arg(operation, boundedContext));
    QJsonArray messages;
    messages.append(user);
    QJsonObject body;
    body.insert(QStringLiteral("model"), model_);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), streamingEnabled_);
    responseBuffer_.clear();
    streamPending_.clear();
    streamBufferOffset_ = 0;
    streamResult_.clear();
    streamError_.clear();
    emit streamReset();
    requestBody_ = QJsonDocument(body).toJson(QJsonDocument::Compact);
    attempt_ = 0;
    emit requestStateChanged(true);
    sendAttempt();
    return true;
}

void AiWritingService::sendAttempt() {
    const QString apiKey = AiCredentialStore::loadApiKey();
    if (apiKey.isEmpty()) {
        finishFailure(QStringLiteral("系统凭据库中的 API 密钥不可用"));
        return;
    }
    QNetworkRequest request(endpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", QByteArrayLiteral("text/event-stream, application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    activeReply_ = network_.post(request, requestBody_);
    QNetworkReply* const reply = activeReply_;
    connect(reply, &QIODevice::readyRead, this, [this, reply]() {
        if (reply != activeReply_) return;
        const QByteArray chunk = reply->readAll();
        responseBuffer_.append(chunk);
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (streamingEnabled_ && (isEventStream(contentType, responseBuffer_) ||
                                  isJsonLinesContentType(contentType) || isJsonLinesPayload(responseBuffer_))) {
            const QByteArray unprocessed = responseBuffer_.mid(streamBufferOffset_);
            streamBufferOffset_ = responseBuffer_.size();
            consumeStreamData(unprocessed);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishAttempt(reply); });
    timeoutTimer_.start(timeoutMs_);
}

bool AiWritingService::scheduleRetry(int httpStatus, QNetworkReply::NetworkError networkError) {
    const bool transientHttp = httpStatus == 408 || httpStatus == 429 || httpStatus >= 500;
    const bool transientNetwork = networkError == QNetworkReply::TimeoutError ||
                                  networkError == QNetworkReply::TemporaryNetworkFailureError ||
                                  networkError == QNetworkReply::RemoteHostClosedError ||
                                  networkError == QNetworkReply::NetworkSessionFailedError;
    if ((!transientHttp && !transientNetwork) || attempt_ >= maxRetries_) return false;
    ++attempt_;
    streamPending_.clear();
    streamResult_.clear();
    streamError_.clear();
    responseBuffer_.clear();
    streamBufferOffset_ = 0;
    emit streamReset();
    retryTimer_.start(qMin(8000, 500 * (1 << (attempt_ - 1))));
    return true;
}

void AiWritingService::finishAttempt(QNetworkReply* reply) {
    if (reply != activeReply_) {
        reply->deleteLater();
        return;
    }
    timeoutTimer_.stop();
    activeReply_ = nullptr;
    const QNetworkReply::NetworkError networkError = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray response = reply->readAll();
    responseBuffer_.append(response);
    const QByteArray completeResponse = responseBuffer_;
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const bool eventStream = isEventStream(contentType, completeResponse);
    const bool jsonLines = isJsonLinesContentType(contentType) || isJsonLinesPayload(completeResponse);
    if (networkError == QNetworkReply::NoError && (eventStream || jsonLines)) {
        const QByteArray unprocessed = completeResponse.mid(streamBufferOffset_);
        streamBufferOffset_ = completeResponse.size();
        consumeStreamData(unprocessed, true);
    }
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
        if (scheduleRetry(httpStatus, networkError)) return;
        finishFailure(httpErrorMessage(httpStatus, networkError, completeResponse));
        return;
    }
    if (!streamError_.isEmpty()) {
        finishFailure(QStringLiteral("AI 服务返回错误：%1").arg(streamError_));
        return;
    }

    QString result = streamResult_;
    const QJsonDocument document = QJsonDocument::fromJson(completeResponse);
    if (result.trimmed().isEmpty() && document.isObject()) {
        const QString apiError = apiErrorText(document.object());
        if (!apiError.isEmpty()) {
            finishFailure(QStringLiteral("AI 服务返回错误：%1").arg(apiError));
            return;
        }
        result = completionText(document.object());
    }
    if (result.trimmed().isEmpty() && contentType.startsWith(QStringLiteral("text/"), Qt::CaseInsensitive) &&
        !contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive) && !eventStream) {
        const QString plainText = QString::fromUtf8(completeResponse).trimmed();
        if (!plainText.startsWith(QLatin1Char('{')) && !plainText.startsWith(QLatin1Char('['))) result = plainText;
    }
    if (result.trimmed().isEmpty()) {
        finishFailure(QStringLiteral("AI 服务响应成功，但没有可识别的文本内容。%1。请确认 URL 是完整的 Chat Completions 接口，且模型返回文本内容。")
                          .arg(responseFormatDetails(completeResponse, contentType)));
        return;
    }

    if (streamingEnabled_ && streamResult_.isEmpty()) emit streamChunk(result);
    requestBody_.clear();
    streamPending_.clear();
    streamResult_.clear();
    streamError_.clear();
    responseBuffer_.clear();
    streamBufferOffset_ = 0;
    emit requestStateChanged(false);
    emit completed(result);
}

void AiWritingService::consumeStreamData(const QByteArray& data, bool finalChunk) {
    streamPending_.append(data);
    while (true) {
        const qsizetype newline = streamPending_.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = streamPending_.left(newline).trimmed();
        streamPending_.remove(0, newline + 1);
        if (line.isEmpty() || line.startsWith(QByteArrayLiteral("event:")) ||
            line.startsWith(QByteArrayLiteral("id:")) || line.startsWith(QByteArrayLiteral("retry:")) ||
            line.startsWith(':')) continue;
        const QByteArray payload = line.startsWith(QByteArrayLiteral("data:")) ? line.mid(5).trimmed() : line;
        consumeStreamEvent(payload);
    }
    if (finalChunk && !streamPending_.trimmed().isEmpty()) {
        QByteArray tail = streamPending_.trimmed();
        streamPending_.clear();
        if (tail.startsWith(QByteArrayLiteral("data:"))) tail = tail.mid(5).trimmed();
        consumeStreamEvent(tail);
    }
}

void AiWritingService::consumeStreamEvent(const QByteArray& payload) {
    if (payload.isEmpty() || payload == QByteArrayLiteral("[DONE]")) return;
    const QJsonDocument event = QJsonDocument::fromJson(payload);
    if (!event.isObject()) return;
    const QJsonObject object = event.object();
    const QString error = apiErrorText(object);
    if (!error.isEmpty()) {
        streamError_ = error;
        return;
    }

    QString chunk = streamChunkText(object);
    const QString type = object.value(QStringLiteral("type")).toString();
    if (chunk.isEmpty() && streamResult_.isEmpty() && type == QStringLiteral("response.output_text.done")) {
        chunk = textFromJsonValue(object.value(QStringLiteral("text")));
    } else if (chunk.isEmpty() && streamResult_.isEmpty() && type == QStringLiteral("response.completed")) {
        chunk = completionText(object.value(QStringLiteral("response")).toObject());
    } else if (chunk.isEmpty() && streamResult_.isEmpty() && type.isEmpty()) {
        chunk = completionText(object);
    }
    if (!chunk.isEmpty()) {
        streamResult_.append(chunk);
        emit streamChunk(chunk);
    }
}

void AiWritingService::finishFailure(const QString& message) {
    timeoutTimer_.stop();
    retryTimer_.stop();
    requestBody_.clear();
    streamPending_.clear();
    streamResult_.clear();
    streamError_.clear();
    responseBuffer_.clear();
    streamBufferOffset_ = 0;
    emit streamReset();
    emit requestStateChanged(false);
    emit failed(message);
}

void AiWritingService::cancel() {
    const bool running = activeReply_ || retryTimer_.isActive();
    timeoutTimer_.stop();
    retryTimer_.stop();
    if (activeReply_) {
        QNetworkReply* reply = activeReply_;
        activeReply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    requestBody_.clear();
    streamPending_.clear();
    streamResult_.clear();
    streamError_.clear();
    responseBuffer_.clear();
    streamBufferOffset_ = 0;
    emit streamReset();
    if (running) emit requestStateChanged(false);
}
