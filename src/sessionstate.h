#ifndef MYBASE_SESSIONSTATE_H
#define MYBASE_SESSIONSTATE_H

#include <QByteArray>
#include <QString>

namespace SessionState {
QByteArray protectDraft(const QString& content, QString* error = nullptr);
QString unprotectDraft(const QByteArray& protectedContent, bool* ok = nullptr);
}

#endif // MYBASE_SESSIONSTATE_H