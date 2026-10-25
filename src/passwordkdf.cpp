#include "passwordkdf.h"

#include <limits>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

QByteArray PasswordKdf::derivePbkdf2Sha256(const QByteArray& password,
                                           const QByteArray& salt,
                                           int iterations,
                                           int keyLength) {
    if (iterations <= 0 || keyLength <= 0 ||
        password.size() > std::numeric_limits<int>::max() ||
        salt.size() > std::numeric_limits<int>::max()) {
        return {};
    }

    QByteArray key(keyLength, Qt::Uninitialized);
#ifdef Q_OS_WIN
    if (password.size() > std::numeric_limits<ULONG>::max() ||
        salt.size() > std::numeric_limits<ULONG>::max()) {
        return {};
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) {
        return {};
    }

    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.constData())),
        static_cast<ULONG>(password.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(salt.constData())),
        static_cast<ULONG>(salt.size()),
        static_cast<ULONGLONG>(iterations),
        reinterpret_cast<PUCHAR>(key.data()),
        static_cast<ULONG>(key.size()),
        0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0 ? key : QByteArray();
#else
    if (password.size() > std::numeric_limits<int>::max() ||
        salt.size() > std::numeric_limits<int>::max() ||
        PKCS5_PBKDF2_HMAC(password.constData(), password.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                          iterations, EVP_sha256(), key.size(),
                          reinterpret_cast<unsigned char*>(key.data())) != 1) {
        return {};
    }
    return key;
#endif
}
