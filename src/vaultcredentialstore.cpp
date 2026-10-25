#include "vaultcredentialstore.h"

#include <QSettings>

bool QSettingsVaultCredentialStore::load(VaultCredentials& credentials) const {
    QSettings settings;
    credentials.salt = QByteArray::fromBase64(
        settings.value(QStringLiteral("vault/passwordSalt")).toByteArray());
    credentials.hash = QByteArray::fromBase64(
        settings.value(QStringLiteral("vault/passwordHash")).toByteArray());
    credentials.version = settings.value(QStringLiteral("vault/passwordVersion"), 1).toInt();
    return settings.status() == QSettings::NoError;
}

bool QSettingsVaultCredentialStore::save(const VaultCredentials& credentials) {
    QSettings settings;
    settings.setValue(QStringLiteral("vault/passwordSalt"), credentials.salt.toBase64());
    settings.setValue(QStringLiteral("vault/passwordHash"), credentials.hash.toBase64());
    settings.setValue(QStringLiteral("vault/passwordVersion"), credentials.version);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
