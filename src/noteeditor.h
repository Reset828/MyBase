#ifndef NOTEEDITOR_H
#define NOTEEDITOR_H

#include <QSplitter>
#include <QTextEdit>
#include <QTextBrowser>
#include <QWebEngineView>
#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QKeyEvent>
#include <QTimer>
#include <QFutureWatcher>
#include <QCompleter>
#include <QMap>
#include <QStringListModel>
#include "markdownhighlighter.h"
#include "markdownparser.h"

class MarkdownTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit MarkdownTextEdit(QWidget* parent = nullptr);

    QTextBlock formatTargetBlock() const { return formatTargetBlock_; }
    void setFormatTargetBlock(const QTextBlock& block) { formatTargetBlock_ = block; }

signals:
    void currentLineHeadingChanged(int headingLevel, const QString& text);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

public:
    void detectAllHeadings();
    void setCompletionWords(const QStringList& words);
    void setNoteTitleCompletionWords(const QStringList& titles);
    void showContextCompletion();

private:
    void detectCurrentLineHeading();
    
    QTextBlock formatTargetBlock_;
    QCompleter* completer_ = nullptr;
    QStringListModel* completionModel_ = nullptr;
    QStringList completionWords_;
    QStringList noteTitleCompletionWords_;
    QString completionPrefix_;
    QString completionKind_;
    int completionStart_ = -1;
};

class NoteEditor : public QWidget {
    Q_OBJECT

public:
    enum class EditorMode {
        Edit,
        Preview,
        WebPreview,
        Split
    };

    explicit NoteEditor(QWidget* parent = nullptr);
    ~NoteEditor() override;

    void setNoteId(qint64 noteId);
    qint64 currentNoteId() const;

    void setMode(EditorMode mode);
    EditorMode mode() const;

    void hideToolBar();
    void showToolBar();

    QString content() const;
    QTextCursor textCursor() const;
    void setContent(const QString& content);
    void setReadOnly(bool readOnly);
    void setRemoteSelection(const QString& peerId, const QString& name, int anchor, int position);
    void removeRemoteSelection(const QString& peerId);
    void clearRemoteSelections();
    void setCompletionWords(const QStringList& words);
    void setNoteTitleCompletionWords(const QStringList& titles);
    void clearWebPreview();
    void forceUpdateWebPreview();

    void insertCodeBlock();
    void insertTable();
    void insertLink();
    void insertImage();
    void insertList();
    void insertOrderedList();
    void insertBlockQuote();

    void scrollToHeading(const QString& headingText);
    void scrollToPosition(int position);
    int cursorPosition() const;
    int verticalScrollPosition() const;
    int selectionStart() const;
    int selectionEnd() const;
    int editorModeValue() const;
    void restoreNavigationState(int cursorPosition, int scrollPosition, int selectionStart = -1, int selectionEnd = -1, int mode = 0);
    void applyAiResult(const QString& markdown, int selectionStart = -1, int selectionEnd = -1, bool append = false);

    MarkdownHighlighter* highlighter() const;

signals:
    void contentChanged(qint64 noteId, const QString& content);
    void noteOpened(qint64 noteId);
    void modeChanged(EditorMode mode);
    void headingCreated(const QString& headingText);
    void cursorSelectionChanged(int anchor, int position);

private slots:
    void onContentChanged();
    void onToggleMode();
    void onInsertPlainText();
    void onInsertH1();
    void onInsertH2();
    void onInsertH3();

private:
    void setupUI();
    void setupToolBar();
    void setupConnections();
    void setupFormatMenu();
    void updatePreview();
    void updateWebPreview();
    void doUpdateWebPreview();
    void showEditOnly();
    void showPreviewOnly();
    void showWebPreviewOnly();
    void showSplitView();
    void showFormatMenu();
    void applyHeadingFormat(int level);
    void onHeadingDetected(int level, const QString& text);
    void onTextChanged();
    QString richTextToMarkdown() const;
    void onPreviewReady(const QString& html);
    void onWebPreviewReady(const QString& html);

    qint64 currentNoteId_;
    EditorMode currentMode_;

    MarkdownTextEdit* editor_;
    QTextBrowser* preview_;
    QWebEngineView* webPreview_;
    MarkdownHighlighter* highlighter_;
    MarkdownParser* parser_;
    QTimer* headingDetectTimer_;
    QTimer* webPreviewDebounceTimer_;
    QTimer* previewDebounceTimer_;
    QFutureWatcher<QString>* previewFutureWatcher_;
    QFutureWatcher<QString>* webPreviewFutureWatcher_;

    QToolBar* toolBar_;
    QAction* actionEdit_;
    QAction* actionPreview_;
    QAction* actionWebPreview_;
    QAction* actionSplit_;
    QAction* actionCodeBlock_;
    QAction* actionTable_;
    QAction* actionLink_;
    QAction* actionImage_;
    QAction* actionList_;
    QAction* actionOrderedList_;
    QAction* actionBlockQuote_;

    QSplitter* splitter_;
    QWidget* editorContainer_;
    QWidget* previewContainer_;
    QWidget* webPreviewContainer_;
    QWidget* welcomeWidget_;

    QMenu* formatMenu_;
    QAction* actionPlainText_;
    QAction* actionH1_;
    QAction* actionH2_;
    QAction* actionH3_;

    bool isSettingContent_;
    QString lastWebPreviewMarkdown_;
    qint64 lastWebPreviewNoteId_ = -1;
    QString lastPreviewMarkdown_;
    struct RemoteSelection { QString name; int anchor = 0; int position = 0; };
    QMap<QString, RemoteSelection> remoteSelections_;
    void refreshRemoteSelections();
};

#endif // NOTEEDITOR_H
