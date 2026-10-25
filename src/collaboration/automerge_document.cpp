#include "automerge_document.h"

#include <QMutexLocker>
#include <QStringList>

namespace {
AMbyteSpan span(const QByteArray& value) {
    return AMbyteSpan{reinterpret_cast<const uint8_t*>(value.constData()), static_cast<size_t>(value.size())};
}

QString spanString(AMbyteSpan value) {
    return value.src && value.count
        ? QString::fromUtf8(reinterpret_cast<const char*>(value.src), static_cast<qsizetype>(value.count))
        : QString();
}

QStringList scalars(const QString& value) {
    QStringList result;
    const QVector<uint> codepoints = value.toUcs4();
    result.reserve(codepoints.size());
    for (uint codepoint : codepoints) {
        const char32_t scalar = static_cast<char32_t>(codepoint);
        result.append(QString::fromUcs4(&scalar, 1));
    }
    return result;
}

size_t scalarPosition(const QString& value, int utf16Position) {
    const int bounded = qBound(0, utf16Position, value.size());
    return static_cast<size_t>(value.left(bounded).toUcs4().size());
}

int utf16Position(const QString& value, size_t scalarIndex) {
    const QVector<uint> codepoints = value.toUcs4();
    const qsizetype bounded = qMin<qsizetype>(static_cast<qsizetype>(scalarIndex), codepoints.size());
    int result = 0;
    for (qsizetype index = 0; index < bounded; ++index) {
        result += codepoints.at(index) > 0xFFFFU ? 2 : 1;
    }
    return result;
}
}

AutomergeDocument::AutomergeDocument() = default;

AutomergeDocument::~AutomergeDocument() { reset(); }

void AutomergeDocument::reset() {
    QMutexLocker lock(&mutex_);
    if (textObjectOwner_) AMresultFree(textObjectOwner_);
    if (documentOwner_) AMresultFree(documentOwner_);
    textObjectOwner_ = nullptr;
    textObject_ = nullptr;
    documentOwner_ = nullptr;
    document_ = nullptr;
}

QString AutomergeDocument::resultError(AMresult* result) const {
    return result ? spanString(AMresultError(result)) : QStringLiteral("Automerge returned no result");
}

bool AutomergeDocument::resultSucceeded(AMresult* result, QString* error) const {
    if (result && AMresultStatus(result) == AM_STATUS_OK) return true;
    if (error) *error = resultError(result);
    return false;
}

bool AutomergeDocument::initializeFromResult(AMresult* owner, QString* error) {
    if (!resultSucceeded(owner, error)) {
        if (owner) AMresultFree(owner);
        return false;
    }
    AMdoc* doc = nullptr;
    if (!AMitemToDoc(AMresultItem(owner), &doc) || !doc) {
        if (error) *error = QStringLiteral("Automerge 文档句柄无效");
        AMresultFree(owner);
        return false;
    }
    documentOwner_ = owner;
    document_ = doc;
    textObjectOwner_ = AMmapGet(document_, AM_ROOT, AMstr("text"), nullptr);
    if (!resultSucceeded(textObjectOwner_, error)) return false;
    textObject_ = AMitemObjId(AMresultItem(textObjectOwner_));
    if (!textObject_) {
        if (error) *error = QStringLiteral("Automerge 文本对象不存在");
        return false;
    }
    return true;
}

bool AutomergeDocument::initialize(const QString& markdown, QString* error) {
    QMutexLocker lock(&mutex_);
    if (document_) return applyTextDiff(markdown, error);
    AMresult* owner = AMcreate(nullptr);
    if (!resultSucceeded(owner, error)) {
        if (owner) AMresultFree(owner);
        return false;
    }
    AMdoc* doc = nullptr;
    if (!AMitemToDoc(AMresultItem(owner), &doc) || !doc) {
        if (error) *error = QStringLiteral("无法创建 Automerge 文档");
        AMresultFree(owner);
        return false;
    }
    documentOwner_ = owner;
    document_ = doc;
    textObjectOwner_ = AMmapPutObject(document_, AM_ROOT, AMstr("text"), AM_OBJ_TYPE_TEXT);
    if (!resultSucceeded(textObjectOwner_, error)) return false;
    textObject_ = AMitemObjId(AMresultItem(textObjectOwner_));
    if (!textObject_) {
        if (error) *error = QStringLiteral("无法创建 Automerge 文本对象");
        return false;
    }
    return applyTextDiff(markdown, error);
}

bool AutomergeDocument::load(const QByteArray& snapshot, QString* error) {
    QMutexLocker lock(&mutex_);
    if (snapshot.isEmpty()) {
        if (error) *error = QStringLiteral("Automerge 快照为空");
        return false;
    }
    AMresult* owner = AMload(reinterpret_cast<const uint8_t*>(snapshot.constData()), static_cast<size_t>(snapshot.size()));
    if (!resultSucceeded(owner, error)) {
        if (owner) AMresultFree(owner);
        return false;
    }
    AMdoc* doc = nullptr;
    if (!AMitemToDoc(AMresultItem(owner), &doc) || !doc) {
        if (error) *error = QStringLiteral("Automerge 快照格式无效");
        AMresultFree(owner);
        return false;
    }
    if (textObjectOwner_) AMresultFree(textObjectOwner_);
    if (documentOwner_) AMresultFree(documentOwner_);
    documentOwner_ = owner;
    document_ = doc;
    textObjectOwner_ = AMmapGet(document_, AM_ROOT, AMstr("text"), nullptr);
    if (!resultSucceeded(textObjectOwner_, error)) return false;
    textObject_ = AMitemObjId(AMresultItem(textObjectOwner_));
    return textObject_ != nullptr;
}

QString AutomergeDocument::text() const {
    QMutexLocker lock(&mutex_);
    return textUnlocked();
}

QByteArray AutomergeDocument::cursorAt(int position) const {
    QMutexLocker lock(&mutex_);
    if (!document_ || !textObject_) return {};
    const QString currentText = textUnlocked();
    const size_t size = AMobjSize(document_, textObject_, nullptr);
    const size_t bounded = qMin(scalarPosition(currentText, position), size);
    AMresult* result = nullptr;
    if (bounded == size) {
        result = AMcursorFromStr(AMstr("e"));
    } else {
        result = AMgetCursor(document_, textObject_, bounded, nullptr);
    }
    if (!result || AMresultStatus(result) != AM_STATUS_OK) {
        if (result) AMresultFree(result);
        return {};
    }
    const AMcursor* cursor = nullptr;
    if (!AMitemToCursor(AMresultItem(result), &cursor) || !cursor) {
        AMresultFree(result);
        return {};
    }
    const AMbyteSpan bytes = AMcursorBytes(cursor);
    const QByteArray value(reinterpret_cast<const char*>(bytes.src), static_cast<qsizetype>(bytes.count));
    AMresultFree(result);
    return value;
}

int AutomergeDocument::positionOfCursor(const QByteArray& cursorBytes) const {
    QMutexLocker lock(&mutex_);
    if (!document_ || !textObject_ || cursorBytes.isEmpty()) return -1;
    AMresult* cursorResult = AMcursorFromBytes(reinterpret_cast<const uint8_t*>(cursorBytes.constData()),
                                               static_cast<size_t>(cursorBytes.size()));
    if (!cursorResult || AMresultStatus(cursorResult) != AM_STATUS_OK) {
        if (cursorResult) AMresultFree(cursorResult);
        return -1;
    }
    const AMcursor* cursor = nullptr;
    if (!AMitemToCursor(AMresultItem(cursorResult), &cursor) || !cursor) {
        AMresultFree(cursorResult);
        return -1;
    }
    AMresult* positionResult = AMgetCursorPosition(document_, textObject_, cursor, nullptr);
    AMresultFree(cursorResult);
    if (!positionResult || AMresultStatus(positionResult) != AM_STATUS_OK) {
        if (positionResult) AMresultFree(positionResult);
        return -1;
    }
    uint64_t position = 0;
    const bool converted = AMitemToUint(AMresultItem(positionResult), &position);
    AMresultFree(positionResult);
    if (!converted) return -1;
    return utf16Position(textUnlocked(), static_cast<size_t>(position));
}
QString AutomergeDocument::textUnlocked() const {
    if (!document_ || !textObject_) return QString();
    const size_t count = AMobjSize(document_, textObject_, nullptr);
    QString result;
    result.reserve(static_cast<qsizetype>(qMin<size_t>(count, 16 * 1024 * 1024)));
    for (size_t i = 0; i < count; ++i) {
        AMresult* itemOwner = AMlistGet(document_, textObject_, i, nullptr);
        if (!itemOwner || AMresultStatus(itemOwner) != AM_STATUS_OK) {
            if (itemOwner) AMresultFree(itemOwner);
            continue;
        }
        AMbyteSpan value{};
        if (AMitemToStr(AMresultItem(itemOwner), &value)) result.append(spanString(value));
        AMresultFree(itemOwner);
    }
    return result;
}

bool AutomergeDocument::applyTextDiff(const QString& markdown, QString* error) {
    if (!document_ || !textObject_) {
        if (error) *error = QStringLiteral("Automerge 文档尚未初始化");
        return false;
    }
    const QStringList oldScalars = scalars(textUnlocked());
    const QStringList newScalars = scalars(markdown);
    qsizetype prefix = 0;
    while (prefix < oldScalars.size() && prefix < newScalars.size() && oldScalars.at(prefix) == newScalars.at(prefix)) ++prefix;
    qsizetype suffix = 0;
    while (suffix < oldScalars.size() - prefix && suffix < newScalars.size() - prefix &&
           oldScalars.at(oldScalars.size() - 1 - suffix) == newScalars.at(newScalars.size() - 1 - suffix)) ++suffix;

    const qsizetype removeCount = oldScalars.size() - prefix - suffix;
    for (qsizetype i = 0; i < removeCount; ++i) {
        AMresult* result = AMlistDelete(document_, textObject_, static_cast<size_t>(prefix));
        const bool ok = resultSucceeded(result, error);
        if (result) AMresultFree(result);
        if (!ok) return false;
    }
    const qsizetype insertCount = newScalars.size() - prefix - suffix;
    for (qsizetype i = 0; i < insertCount; ++i) {
        const QByteArray encoded = newScalars.at(prefix + i).toUtf8();
        AMresult* result = AMlistPutStr(document_, textObject_, static_cast<size_t>(prefix + i), true, span(encoded));
        const bool ok = resultSucceeded(result, error);
        if (result) AMresultFree(result);
        if (!ok) return false;
    }
    AMresult* commit = AMcommit(document_, AMstr("Edit Markdown"), nullptr);
    const bool ok = resultSucceeded(commit, error);
    if (commit) AMresultFree(commit);
    return ok;
}

bool AutomergeDocument::replaceText(const QString& markdown, QString* error) {
    QMutexLocker lock(&mutex_);
    return applyTextDiff(markdown, error);
}

QByteArray AutomergeDocument::save(QString* error) const {
    QMutexLocker lock(&mutex_);
    if (!document_) {
        if (error) *error = QStringLiteral("Automerge 文档尚未初始化");
        return {};
    }
    AMresult* result = AMsave(document_);
    if (!resultSucceeded(result, error)) {
        if (result) AMresultFree(result);
        return {};
    }
    AMbyteSpan bytes{};
    const bool converted = AMitemToBytes(AMresultItem(result), &bytes);
    QByteArray snapshot = converted
        ? QByteArray(reinterpret_cast<const char*>(bytes.src), static_cast<qsizetype>(bytes.count))
        : QByteArray();
    AMresultFree(result);
    return snapshot;
}

QByteArray AutomergeDocument::generateSyncMessage(AMsyncState* state, QString* error) {
    QMutexLocker lock(&mutex_);
    if (!document_ || !state) {
        if (error) *error = QStringLiteral("协作文档或同步状态无效");
        return {};
    }
    AMresult* generated = AMgenerateSyncMessage(document_, state);
    if (!resultSucceeded(generated, error)) {
        if (generated) AMresultFree(generated);
        return {};
    }
    const AMsyncMessage* message = nullptr;
    const bool hasMessage = AMitemToSyncMessage(AMresultItem(generated), &message);
    if (!hasMessage || !message) {
        AMresultFree(generated);
        return {};
    }
    AMresult* encoded = AMsyncMessageEncode(message);
    AMresultFree(generated);
    if (!resultSucceeded(encoded, error)) {
        if (encoded) AMresultFree(encoded);
        return {};
    }
    AMbyteSpan bytes{};
    const bool converted = AMitemToBytes(AMresultItem(encoded), &bytes);
    const QByteArray payload = converted
        ? QByteArray(reinterpret_cast<const char*>(bytes.src), static_cast<qsizetype>(bytes.count))
        : QByteArray();
    AMresultFree(encoded);
    return payload;
}

bool AutomergeDocument::receiveSyncMessage(AMsyncState* state, const QByteArray& payload, QString* error) {
    QMutexLocker lock(&mutex_);
    if (!document_ || !state || payload.isEmpty()) {
        if (error) *error = QStringLiteral("协作同步消息无效");
        return false;
    }
    AMresult* decoded = AMsyncMessageDecode(reinterpret_cast<const uint8_t*>(payload.constData()), static_cast<size_t>(payload.size()));
    if (!resultSucceeded(decoded, error)) {
        if (decoded) AMresultFree(decoded);
        return false;
    }
    const AMsyncMessage* message = nullptr;
    const bool converted = AMitemToSyncMessage(AMresultItem(decoded), &message);
    if (!converted || !message) {
        if (error) *error = QStringLiteral("Automerge 同步消息解码失败");
        AMresultFree(decoded);
        return false;
    }
    AMresult* received = AMreceiveSyncMessage(document_, state, message);
    const bool ok = resultSucceeded(received, error);
    if (received) AMresultFree(received);
    AMresultFree(decoded);
    return ok;
}
