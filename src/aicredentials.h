#ifndef AICREDENTIALS_H
#define AICREDENTIALS_H
#include <QString>
class AiCredentialStore {
public:
    static bool saveApiKey(const QString& key);
    static QString loadApiKey();
};
#endif
