#include "aescrypto.h"
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include "passwordkdf.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <memory>
#endif

namespace {

constexpr int AuthenticatedSaltSize = 16;
constexpr int AuthenticatedNonceSize = 12;
constexpr int AuthenticationTagSize = 16;
constexpr int EncryptionKdfIterations = 600000;
const QByteArray AuthenticatedFormatMarker("MBG2", 4);

QByteArray secureRandomBytes(int length) {
    QByteArray bytes(length, Qt::Uninitialized);
    QRandomGenerator* random = QRandomGenerator::system();
    for (char& byte : bytes) {
        byte = static_cast<char>(random->bounded(256));
    }
    return bytes;
}

#ifdef Q_OS_WIN
QByteArray aesGcmEncrypt(const QByteArray& plaintext,
                         const QByteArray& key,
                         const QByteArray& nonce) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }

    const wchar_t chainingMode[] = BCRYPT_CHAIN_MODE_GCM;
    if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainingMode)),
                          sizeof(chainingMode), 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    ULONG keyObjectLength = 0;
    ULONG resultLength = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&keyObjectLength), sizeof(keyObjectLength),
                          &resultLength, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    QByteArray keyObject(static_cast<qsizetype>(keyObjectLength), Qt::Uninitialized);
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    const NTSTATUS keyStatus = BCryptGenerateSymmetricKey(
        algorithm, &keyHandle, reinterpret_cast<PUCHAR>(keyObject.data()), keyObjectLength,
        reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
        static_cast<ULONG>(key.size()), 0);
    if (keyStatus < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    QByteArray ciphertext(plaintext.size(), Qt::Uninitialized);
    QByteArray tag(AuthenticationTagSize, Qt::Uninitialized);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    ULONG ciphertextLength = 0;
    const NTSTATUS status = BCryptEncrypt(
        keyHandle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.constData())),
        static_cast<ULONG>(plaintext.size()), &authInfo, nullptr, 0,
        reinterpret_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
        &ciphertextLength, 0);
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || ciphertextLength != static_cast<ULONG>(ciphertext.size())) {
        return {};
    }
    return ciphertext + tag;
}

QByteArray aesGcmDecrypt(const QByteArray& ciphertextAndTag,
                         const QByteArray& key,
                         const QByteArray& nonce) {
    const QByteArray ciphertext = ciphertextAndTag.left(ciphertextAndTag.size() - AuthenticationTagSize);
    QByteArray tag = ciphertextAndTag.right(AuthenticationTagSize);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }

    const wchar_t chainingMode[] = BCRYPT_CHAIN_MODE_GCM;
    if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainingMode)),
                          sizeof(chainingMode), 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    ULONG keyObjectLength = 0;
    ULONG resultLength = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&keyObjectLength), sizeof(keyObjectLength),
                          &resultLength, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    QByteArray keyObject(static_cast<qsizetype>(keyObjectLength), Qt::Uninitialized);
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    const NTSTATUS keyStatus = BCryptGenerateSymmetricKey(
        algorithm, &keyHandle, reinterpret_cast<PUCHAR>(keyObject.data()), keyObjectLength,
        reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
        static_cast<ULONG>(key.size()), 0);
    if (keyStatus < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    QByteArray plaintext(ciphertext.size(), Qt::Uninitialized);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    ULONG plaintextLength = 0;
    const NTSTATUS status = BCryptDecrypt(
        keyHandle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext.constData())),
        static_cast<ULONG>(ciphertext.size()), &authInfo, nullptr, 0,
        reinterpret_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
        &plaintextLength, 0);
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || plaintextLength != static_cast<ULONG>(plaintext.size())) {
        return {};
    }
    return plaintext;
}
#else
QByteArray aesGcmEncrypt(const QByteArray& plaintext,
                         const QByteArray& key,
                         const QByteArray& nonce) {
    using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1 ||
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
        return {};
    }

    QByteArray ciphertext(plaintext.size() + 16, Qt::Uninitialized);
    int produced = 0;
    int finalProduced = 0;
    if (EVP_EncryptUpdate(context.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &produced,
                          reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          plaintext.size()) != 1 ||
        EVP_EncryptFinal_ex(context.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + produced,
                            &finalProduced) != 1) {
        return {};
    }
    ciphertext.resize(produced + finalProduced);
    QByteArray tag(AuthenticationTagSize, Qt::Uninitialized);
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) != 1) {
        return {};
    }
    return ciphertext + tag;
}

QByteArray aesGcmDecrypt(const QByteArray& ciphertextAndTag,
                         const QByteArray& key,
                         const QByteArray& nonce) {
    const QByteArray ciphertext = ciphertextAndTag.left(ciphertextAndTag.size() - AuthenticationTagSize);
    const QByteArray tag = ciphertextAndTag.right(AuthenticationTagSize);
    using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1 ||
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
        return {};
    }

    QByteArray plaintext(ciphertext.size() + 16, Qt::Uninitialized);
    int produced = 0;
    int finalProduced = 0;
    if (EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char*>(plaintext.data()), &produced,
                          reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                          ciphertext.size()) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                            const_cast<char*>(tag.constData())) != 1 ||
        EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char*>(plaintext.data()) + produced,
                            &finalProduced) != 1) {
        return {};
    }
    plaintext.resize(produced + finalProduced);
    return plaintext;
}
#endif

}

const uint8_t AesCrypto::sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

const uint8_t AesCrypto::invSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

const uint8_t AesCrypto::rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

QByteArray AesCrypto::generateIv() {
    QByteArray iv(BLOCK_SIZE, 0);
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        iv[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return iv;
}

QByteArray AesCrypto::deriveKey(const QString& password, const QByteArray& salt) {
    QByteArray passwordBytes = password.toUtf8();
    QByteArray data = passwordBytes + salt;
    
    QByteArray key(KEY_SIZE, 0);
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    key = hash;
    
    for (int i = 0; i < 10000; ++i) {
        hash = QCryptographicHash::hash(hash + passwordBytes + salt, QCryptographicHash::Sha256);
        for (int j = 0; j < KEY_SIZE && j < hash.size(); ++j) {
            key[j] = key[j] ^ hash[j];
        }
    }
    
    return key;
}

QByteArray AesCrypto::encrypt(const QByteArray& plaintext, const QString& password, QByteArray& iv) {
    if (plaintext.isEmpty()) {
        iv.clear();
        return QByteArray();
    }

    const QByteArray salt = secureRandomBytes(AuthenticatedSaltSize);
    const QByteArray nonce = secureRandomBytes(AuthenticatedNonceSize);
    const QByteArray key = PasswordKdf::derivePbkdf2Sha256(
        password.toUtf8(), salt, EncryptionKdfIterations, KEY_SIZE);
    if (key.size() != KEY_SIZE) {
        iv.clear();
        return {};
    }

    const QByteArray ciphertextAndTag = aesGcmEncrypt(plaintext, key, nonce);
    if (ciphertextAndTag.size() != plaintext.size() + AuthenticationTagSize) {
        iv.clear();
        return {};
    }
    iv = AuthenticatedFormatMarker + salt + nonce;
    return ciphertextAndTag;
}

QByteArray AesCrypto::decrypt(const QByteArray& ciphertext, const QString& password, const QByteArray& iv) {
    if (ciphertext.isEmpty()) {
        return QByteArray();
    }

    if (iv.startsWith(AuthenticatedFormatMarker)) {
        if (iv.size() != AuthenticatedFormatMarker.size() + AuthenticatedSaltSize + AuthenticatedNonceSize ||
            ciphertext.size() < AuthenticationTagSize) {
            return {};
        }
        const QByteArray salt = iv.mid(AuthenticatedFormatMarker.size(), AuthenticatedSaltSize);
        const QByteArray nonce = iv.mid(AuthenticatedFormatMarker.size() + AuthenticatedSaltSize,
                                        AuthenticatedNonceSize);
        const QByteArray key = PasswordKdf::derivePbkdf2Sha256(
            password.toUtf8(), salt, EncryptionKdfIterations, KEY_SIZE);
        if (key.size() != KEY_SIZE) {
            return {};
        }
        return aesGcmDecrypt(ciphertext, key, nonce);
    }
    
    if (iv.size() != BLOCK_SIZE) {
        return QByteArray();
    }
    
    if (ciphertext.size() % BLOCK_SIZE != 0) {
        return QByteArray();
    }
    
    QByteArray key = deriveKey(password, iv);
    
    uint8_t roundKeys[240];
    keyExpansion(key, roundKeys);
    
    QByteArray result;
    QByteArray prevBlock = iv;
    
    for (int i = 0; i < ciphertext.size(); i += BLOCK_SIZE) {
        uint8_t block[BLOCK_SIZE];
        for (int j = 0; j < BLOCK_SIZE; ++j) {
            block[j] = static_cast<uint8_t>(ciphertext[i + j]);
        }
        
        QByteArray currentBlock(BLOCK_SIZE, 0);
        for (int j = 0; j < BLOCK_SIZE; ++j) {
            currentBlock[j] = static_cast<char>(block[j]);
        }
        
        decryptBlock(block, roundKeys);
        
        for (int j = 0; j < BLOCK_SIZE; ++j) {
            result.append(static_cast<char>(block[j] ^ static_cast<uint8_t>(prevBlock[j])));
        }
        
        prevBlock = currentBlock;
    }
    
    return pkcs7Unpad(result);
}

void AesCrypto::keyExpansion(const QByteArray& key, uint8_t* roundKeys) {
    for (int i = 0; i < KEY_SIZE; ++i) {
        roundKeys[i] = static_cast<uint8_t>(key[i]);
    }
    
    uint8_t temp[4];
    int bytesGenerated = KEY_SIZE;
    int rconIteration = 1;
    
    while (bytesGenerated < 240) {
        for (int i = 0; i < 4; ++i) {
            temp[i] = roundKeys[bytesGenerated - 4 + i];
        }
        
        if (bytesGenerated % KEY_SIZE == 0) {
            uint8_t t = temp[0];
            temp[0] = sbox[temp[1]];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
            
            temp[0] ^= rcon[rconIteration++];
        }
        
        for (int i = 0; i < 4; ++i) {
            roundKeys[bytesGenerated] = roundKeys[bytesGenerated - KEY_SIZE] ^ temp[i];
            bytesGenerated++;
        }
    }
}

void AesCrypto::decryptBlock(uint8_t* block, const uint8_t* roundKeys) {
    addRoundKey(block, roundKeys + ROUNDS * BLOCK_SIZE);
    
    for (int round = ROUNDS - 1; round > 0; --round) {
        invShiftRows(block);
        invSubBytes(block);
        addRoundKey(block, roundKeys + round * BLOCK_SIZE);
        invMixColumns(block);
    }
    
    invShiftRows(block);
    invSubBytes(block);
    addRoundKey(block, roundKeys);
}

void AesCrypto::addRoundKey(uint8_t* block, const uint8_t* roundKey) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        block[i] ^= roundKey[i];
    }
}

void AesCrypto::invSubBytes(uint8_t* block) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        block[i] = invSbox[block[i]];
    }
}

void AesCrypto::invShiftRows(uint8_t* block) {
    uint8_t temp;
    
    temp = block[13];
    block[13] = block[9];
    block[9] = block[5];
    block[5] = block[1];
    block[1] = temp;
    
    temp = block[2];
    block[2] = block[10];
    block[10] = temp;
    temp = block[6];
    block[6] = block[14];
    block[14] = temp;
    
    temp = block[3];
    block[3] = block[7];
    block[7] = block[11];
    block[11] = block[15];
    block[15] = temp;
}

static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) {
            p ^= a;
        }
        uint8_t hiBitSet = a & 0x80;
        a <<= 1;
        if (hiBitSet) {
            a ^= 0x1b;
        }
        b >>= 1;
    }
    return p;
}

void AesCrypto::invMixColumns(uint8_t* block) {
    for (int i = 0; i < 4; ++i) {
        int idx = i * 4;
        uint8_t a[4];
        
        for (int j = 0; j < 4; ++j) {
            a[j] = block[idx + j];
        }
        
        block[idx + 0] = gmul(a[0], 14) ^ gmul(a[1], 11) ^ gmul(a[2], 13) ^ gmul(a[3], 9);
        block[idx + 1] = gmul(a[0], 9) ^ gmul(a[1], 14) ^ gmul(a[2], 11) ^ gmul(a[3], 13);
        block[idx + 2] = gmul(a[0], 13) ^ gmul(a[1], 9) ^ gmul(a[2], 14) ^ gmul(a[3], 11);
        block[idx + 3] = gmul(a[0], 11) ^ gmul(a[1], 13) ^ gmul(a[2], 9) ^ gmul(a[3], 14);
    }
}

QByteArray AesCrypto::pkcs7Unpad(const QByteArray& data) {
    if (data.isEmpty()) {
        return data;
    }
    
    int padLen = static_cast<uint8_t>(data[data.size() - 1]);
    if (padLen > BLOCK_SIZE || padLen > data.size()) {
        return data;
    }
    
    for (int i = data.size() - padLen; i < data.size(); ++i) {
        if (static_cast<uint8_t>(data[i]) != padLen) {
            return data;
        }
    }
    
    return data.left(data.size() - padLen);
}
