#ifndef AESCRYPTO_H
#define AESCRYPTO_H

#include <QString>
#include <QByteArray>
#include <QCryptographicHash>

class AesCrypto {
public:
    static QByteArray encrypt(const QByteArray& plaintext, const QString& password, QByteArray& iv);
    static QByteArray decrypt(const QByteArray& ciphertext, const QString& password, const QByteArray& iv);
    static QByteArray generateIv();

private:
    static constexpr int KEY_SIZE = 32;
    static constexpr int BLOCK_SIZE = 16;
    static constexpr int ROUNDS = 14;

    static QByteArray deriveKey(const QString& password, const QByteArray& salt);
    static void keyExpansion(const QByteArray& key, uint8_t* roundKeys);
    static void decryptBlock(uint8_t* block, const uint8_t* roundKeys);
    static void addRoundKey(uint8_t* block, const uint8_t* roundKey);
    static void invSubBytes(uint8_t* block);
    static void invShiftRows(uint8_t* block);
    static void invMixColumns(uint8_t* block);
    static QByteArray pkcs7Unpad(const QByteArray& data);

    static const uint8_t sbox[256];
    static const uint8_t invSbox[256];
    static const uint8_t rcon[11];
};

#endif // AESCRYPTO_H
