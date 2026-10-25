#ifndef PASSWORDKDF_H
#define PASSWORDKDF_H

#include <QByteArray>

class PasswordKdf final {
public:
    static QByteArray derivePbkdf2Sha256(const QByteArray& password,
                                         const QByteArray& salt,
                                         int iterations,
                                         int keyLength);
};

#endif // PASSWORDKDF_H
