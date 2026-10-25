#ifndef NOTETABMANAGER_H
#define NOTETABMANAGER_H

#include <QTabWidget>
#include <QMap>
#include "noteeditor.h"
#include "note.h"

class NoteTabManager : public QTabWidget {
    Q_OBJECT

public:
    explicit NoteTabManager(QWidget* parent = nullptr);

    int openNote(qint64 noteId, const QString& title, const QString& content);
    void closeTab(int index);
    void closeNote(qint64 noteId);
    void closeAllTabs();

    qint64 noteIdAt(int index) const;
    int findTabByNoteId(qint64 noteId) const;
    NoteEditor* editorAt(int index) const;
    NoteEditor* currentEditor() const;

    int tabCount() const;
    bool isNoteOpen(qint64 noteId) const;

signals:
    void tabOpened(qint64 noteId);
    void tabClosed(qint64 noteId);
    void currentTabChanged(int index);
    void tabContentChanged(qint64 noteId, const QString& content);
    void headingCreated(const QString& headingText);

private slots:
    void onTabChanged(int index);
    void onCurrentEditorContentChanged(qint64 noteId, const QString& content);
    void onTabCloseRequested(int index);

private:
    void setupConnections();

    QMap<int, qint64> tabIndexToNoteId_;
    QMap<qint64, int> noteIdToTabIndex_;
};

#endif // NOTETABMANAGER_H
