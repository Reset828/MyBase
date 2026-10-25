#include "aicredentials.h"
#include <QByteArray>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

bool AiCredentialStore::saveApiKey(const QString& key) {
#ifdef Q_OS_WIN
    const QByteArray bytes = key.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(L"MyBase.OpenAICompatible.ApiKey");
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0) == TRUE;
#else
    Q_UNUSED(key);
    return false;
#endif
}

QString AiCredentialStore::loadApiKey() {
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(L"MyBase.OpenAICompatible.ApiKey", CRED_TYPE_GENERIC, 0, &credential)) return {};
    const QString key = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                          static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return key;
#else
    return {};
#endif
}
