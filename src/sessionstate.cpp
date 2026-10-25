#include "sessionstate.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace SessionState {

QByteArray protectDraft(const QString& content, QString* error) {
#ifdef Q_OS_WIN
    const QByteArray plain = content.toUtf8();
    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plain.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"MyBase session draft", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = QStringLiteral("无法使用 Windows 数据保护加密会话草稿");
        return {};
    }
    const QByteArray result(reinterpret_cast<const char*>(output.pbData),
                            static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    Q_UNUSED(content);
    if (error) *error = QStringLiteral("当前平台不提供会话草稿安全存储");
    return {};
#endif
}

QString unprotectDraft(const QByteArray& protectedContent, bool* ok) {
    if (ok) *ok = false;
#ifdef Q_OS_WIN
    if (protectedContent.isEmpty()) return {};
    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(protectedContent.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedContent.constData()));
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    const QString result = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData),
                                              static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    if (ok) *ok = true;
    return result;
#else
    Q_UNUSED(protectedContent);
    return {};
#endif
}

} // namespace SessionState