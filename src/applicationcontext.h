#ifndef APPLICATIONCONTEXT_H
#define APPLICATIONCONTEXT_H

#include <QString>

#include "databasemanager.h"
#include "notemanager.h"
#include "searchengine.h"
#include "tagmanager.h"
#include "vaultmanager.h"
#include "localfeatures.h"
#include "aiwritingservice.h"
#include "collaboration/collaboration_client.h"

class ApplicationContext final {
public:
    explicit ApplicationContext(const QString& databasePath);

    bool initialize();

    DatabaseManager& database() { return database_; }
    NoteManager& notes() { return noteManager_; }
    TagManager& tags() { return tagManager_; }
    SearchEngine& search() { return searchEngine_; }
    VaultManager& vault() { return vaultManager_; }
    LocalFeatures& localFeatures() { return localFeatures_; }
    AiWritingService& ai() { return ai_; }
    CollaborationClient& collaboration() { return collaboration_; }

private:
    QString databasePath_;
    DatabaseManager database_;
    NoteManager noteManager_;
    TagManager tagManager_;
    SearchEngine searchEngine_;
    VaultManager vaultManager_;
    LocalFeatures localFeatures_;
    AiWritingService ai_;
    CollaborationClient collaboration_;
    bool initialized_ = false;
};

#endif // APPLICATIONCONTEXT_H
