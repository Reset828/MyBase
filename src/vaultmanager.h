#ifndef VAULTMANAGER_H
#define VAULTMANAGER_H

#include <QString>
#include <QByteArray>
#include <memory>

#include "vaultcredentialstore.h"

class VaultManager {
public:
    VaultManager();
    explicit VaultManager(std::unique_ptr<IVaultCredentialStore> credentialStore);

    bool initialize();
    bool isPasswordConfigured() const;
    bool setPassword(const QString& password);
    bool verifyPassword(const QString& password);

private:
    static constexpr int SaltSize = 32;
    static constexpr int LegacyHashIterations = 100000;
    static constexpr int Pbkdf2Iterations = 600000;
    static constexpr int LegacyCredentialVersion = 1;
    static constexpr int CurrentCredentialVersion = 2;

    std::unique_ptr<IVaultCredentialStore> credentialStore_;
    QByteArray passwordSalt_;
    QByteArray passwordHash_;
    int passwordVersion_ = 0;
    bool passwordConfigured_ = false;

    static QByteArray hashPassword(const QString& password, const QByteArray& salt);
    static QByteArray hashLegacyPassword(const QString& password, const QByteArray& salt);
    bool matchesPasswordHash(const QByteArray& candidate) const;
};

#endif // VAULTMANAGER_H
