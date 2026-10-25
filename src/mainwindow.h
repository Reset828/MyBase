#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QListView>
#include <QLineEdit>
#include <QComboBox>
#include <QSet>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QLabel>
#include <QCloseEvent>
#include <QSettings>
#include <QFuture>
#include <QVector>
#include <memory>
#include "applicationcontext.h"
#include "notemanager.h"
#include "tagmanager.h"
#include "searchengine.h"
#include "noteeditor.h"
#include "tagtreeview.h"
#include "markdownoutlinemodel.h"
#include "searchresultmodel.h"
#include "notelistmodel.h"
#include "notetabmanager.h"
#include "trashdialog.h"
#include "themestylesheet.h"
#include "collaboration/collaboration_client.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    bool initialize(const QString& dbPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewNote();
    void onDeleteNote();
    void onEncryptNote();
    void onOpenEncryptedNotes();
    void onOpenTrash();
    void onSearchTextChanged(const QString& text);
    void onNoteSelected(const QModelIndex& index);
    void onHeadingCreated(const QString& headingText);
    void onTagSelected(const QModelIndex& index);
    void onSearchResultsReceived(const QList<SearchResult>& results);
    void onTagSelectionChanged(const QVector<qint64>& tagIds);
    void runSearch();
    void onCurrentTabChanged(int index);
    void onTabContentChanged(qint64 noteId, const QString& content);

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupConnections();
    void refreshCompletionWords(NoteEditor* editor);
    void loadNotes();
    void saveWindowState();
    void restoreWindowState();
    void saveSessionState();
    void restoreSessionState();
    void refreshOutline(const QString& markdown);

    std::unique_ptr<ApplicationContext> applicationContext_;
    NoteManager* noteManager_;
    TagManager* tagManager_;
    SearchEngine* searchEngine_;
    NoteTabManager* noteTabManager_;

    QSplitter* mainSplitter_;
    QSplitter* leftSplitter_;

    QWidget* leftPanel_;
    QVBoxLayout* leftLayout_;
    QLabel* tagLabel_;
    TagTreeView* tagTreeView_;
    QTabWidget* knowledgeNavigationTabs_;
    QTreeView* outlineView_;
    MarkdownOutlineModel* outlineModel_;

    QWidget* centerPanel_;
    QVBoxLayout* centerLayout_;
    QLineEdit* searchLineEdit_;
    QComboBox* tagMatchModeCombo_;
    QSet<qint64> selectedTagIds_;
    QListView* noteListView_;
    SearchResultModel* searchResultModel_;
    NoteListModel* noteListModel_;

    QMenuBar* menuBar_;
    QToolBar* toolBar_;
    QStatusBar* statusBar_;

    QAction* actionNewNote_;
    QAction* actionDeleteNote_;
    QAction* actionEncryptNote_;
    QAction* actionOpenEncryptedNotes_;
    QAction* actionOpenTrash_;
    QAction* actionExit_;
    QAction* actionAbout_;

    QVector<QFuture<QVector<SearchResult>>> activeSearchFutures_;
    quint64 searchRequestId_ = 0;
    QTimer* autoSaveTimer_ = nullptr;
    QTimer* sessionSnapshotTimer_ = nullptr;
    QMap<qint64, QString> pendingNoteContent_;
    QSet<qint64> encryptedNotesOpen_;
    QPointer<NoteEditor> aiTargetEditor_;
    QPointer<QDialog> aiStreamDialog_;
    QPointer<QTextEdit> aiStreamPreview_;
    QComboBox* aiModelSelector_ = nullptr;
    CollaborationClient* collaborationClient_ = nullptr;
    qint64 collaborationNoteId_ = -1;
    bool applyingRemoteCollaborationEdit_ = false;
    bool collaborationSignalsConnected_ = false;
    QSet<QString> remoteCollaborationPeers_;
    QMetaObject::Connection collaborationCursorConnection_;
    qint64 aiTargetNoteId_ = -1;
    int aiTargetSelectionStart_ = -1;
    int aiTargetSelectionEnd_ = -1;
    QString aiTargetDocumentSnapshot_;
    bool flushPendingSaves();
    void startCollaboration();
};

#endif // MAINWINDOW_H
