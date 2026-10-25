#include "vaultmanager.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <memory>
#include <utility>
#include "passwordkdf.h"

VaultManager::VaultManager()
    : VaultManager(std::make_unique<QSettingsVaultCredentialStore>()) {
}

VaultManager::VaultManager(std::unique_ptr<IVaultCredentialStore> credentialStore)
    : credentialStore_(std::move(credentialStore)) {
    if (!credentialStore_) {
        credentialStore_ = std::make_unique<QSettingsVaultCredentialStore>();
    }
}

bool VaultManager::initialize() {
    VaultCredentials credentials;
    if (!credentialStore_->load(credentials)) {
        return false;
    }

    passwordSalt_ = credentials.salt;
    passwordHash_ = credentials.hash;
    passwordVersion_ = credentials.version;
    passwordConfigured_ = passwordSalt_.size() == SaltSize &&
                          passwordHash_.size() == QCryptographicHash::hashLength(QCryptographicHash::Sha256) &&
                          (passwordVersion_ == LegacyCredentialVersion ||
                           passwordVersion_ == CurrentCredentialVersion);
    return true;
}

bool VaultManager::isPasswordConfigured() const {
    return passwordConfigured_;
}

bool VaultManager::setPassword(const QString& password) {
    if (password.size() < 8) {
        return false;
    }

    QByteArray salt(SaltSize, Qt::Uninitialized);
    for (char& byte : salt) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    const QByteArray hash = hashPassword(password, salt);
    if (hash.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256)) {
        return false;
    }

    if (!credentialStore_->save({salt, hash, CurrentCredentialVersion})) {
        return false;
    }

    passwordSalt_ = salt;
    passwordHash_ = hash;
    passwordVersion_ = CurrentCredentialVersion;
    passwordConfigured_ = true;
    return true;
}

bool VaultManager::verifyPassword(const QString& password) {
    if (!passwordConfigured_ || password.isEmpty()) {
        return false;
    }

    const QByteArray candidate = passwordVersion_ == LegacyCredentialVersion
                                     ? hashLegacyPassword(password, passwordSalt_)
                                     : hashPassword(password, passwordSalt_);
    if (!matchesPasswordHash(candidate)) {
        return false;
    }

    if (passwordVersion_ == LegacyCredentialVersion) {
        setPassword(password);
    }
    return true;
}

bool VaultManager::matchesPasswordHash(const QByteArray& candidate) const {
    if (candidate.size() != passwordHash_.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (int i = 0; i < candidate.size(); ++i) {
        difference |= static_cast<unsigned char>(candidate.at(i) ^ passwordHash_.at(i));
    }
    return difference == 0;
}

QByteArray VaultManager::hashPassword(const QString& password, const QByteArray& salt) {
    return PasswordKdf::derivePbkdf2Sha256(password.toUtf8(), salt, Pbkdf2Iterations,
                                           QCryptographicHash::hashLength(QCryptographicHash::Sha256));
}

QByteArray VaultManager::hashLegacyPassword(const QString& password, const QByteArray& salt) {
    QByteArray hash = QCryptographicHash::hash(password.toUtf8() + salt, QCryptographicHash::Sha256);
    for (int iteration = 1; iteration < LegacyHashIterations; ++iteration) {
        hash = QCryptographicHash::hash(hash + password.toUtf8() + salt, QCryptographicHash::Sha256);
    }
    return hash;
}
