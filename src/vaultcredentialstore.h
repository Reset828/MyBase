#ifndef VAULTCREDENTIALSTORE_H
#define VAULTCREDENTIALSTORE_H

#include <QByteArray>

struct VaultCredentials {
    QByteArray salt;
    QByteArray hash;
    int version = 0;
};

class IVaultCredentialStore {
public:
    virtual ~IVaultCredentialStore() = default;

    virtual bool load(VaultCredentials& credentials) const = 0;
    virtual bool save(const VaultCredentials& credentials) = 0;
};

class QSettingsVaultCredentialStore final : public IVaultCredentialStore {
public:
    bool load(VaultCredentials& credentials) const override;
    bool save(const VaultCredentials& credentials) override;
};

#endif // VAULTCREDENTIALSTORE_H
