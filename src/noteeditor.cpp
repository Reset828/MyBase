#include "noteeditor.h"
#include "themestylesheet.h"
#include <QHBoxLayout>
#include <QTextCursor>
#include <QTextBlock>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QListView>
#include <QRegularExpression>
#include <QFileInfo>
#include <QHash>
#include <QColor>

MarkdownTextEdit::MarkdownTextEdit(QWidget* parent)
    : QTextEdit(parent) {
    completionModel_ = new QStringListModel(this);
    completer_ = new QCompleter(completionModel_, this);
    completer_->setWidget(this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    if (auto* popup = qobject_cast<QListView*>(completer_->popup())) popup->setUniformItemSizes(true);
    connect(completer_, QOverload<const QString&>::of(&QCompleter::activated), this,
            [this](const QString& completion) {
        QString insertion = completion;
        if (completionKind_ == QStringLiteral("wiki-link")) {
            insertion += QStringLiteral("]]" );
        } else if (completionKind_ == QStringLiteral("command")) {
            static const QHash<QString, QString> snippets = {
                {QStringLiteral("h1"), QStringLiteral("# ")},
                {QStringLiteral("h2"), QStringLiteral("## ")},
                {QStringLiteral("h3"), QStringLiteral("### ")},
                {QStringLiteral("h4"), QStringLiteral("#### ")},
                {QStringLiteral("h5"), QStringLiteral("##### ")},
                {QStringLiteral("h6"), QStringLiteral("###### ")},
                {QStringLiteral("code"), QStringLiteral("```text\n\n```")},
                {QStringLiteral("table"), QStringLiteral("| 列 1 | 列 2 |\n| --- | --- |\n| 内容 | 内容 |")},
                {QStringLiteral("quote"), QStringLiteral("> ")},
                {QStringLiteral("ul"), QStringLiteral("- ")},
                {QStringLiteral("ol"), QStringLiteral("1. ")},
                {QStringLiteral("link"), QStringLiteral("[链接文本]()")},
                {QStringLiteral("image"), QStringLiteral("![图片说明]()")}
            };
            insertion = snippets.value(completion, completion);
        }
        QTextCursor cursor = textCursor();
        const int end = cursor.position();
        const int start = completionStart_ >= 0 ? completionStart_ : qMax(0, end - completionPrefix_.size());
        cursor.beginEditBlock();
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.insertText(insertion);
        cursor.endEditBlock();
        setTextCursor(cursor);
        completer_->popup()->hide();
        completionStart_ = -1;
        completionKind_.clear();
    });
}

void MarkdownTextEdit::keyPressEvent(QKeyEvent* event) {
    const QString text = event->text();
    const int key = event->key();

    if (key == Qt::Key_Space && event->modifiers().testFlag(Qt::ControlModifier)) {
        showContextCompletion();
        return;
    }
    if (key == Qt::Key_Escape && completer_->popup()->isVisible()) {
        completer_->popup()->hide();
        return;
    }
    if (completer_->popup()->isVisible() &&
        (key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Tab)) {
        event->ignore();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        QTextEdit::keyPressEvent(event);
        QTextCharFormat normalFormat;
        normalFormat.setFontPointSize(10);
        normalFormat.setFontWeight(QFont::Normal);
        QTextCursor cursor = textCursor();
        cursor.setBlockCharFormat(normalFormat);
        detectCurrentLineHeading();
        return;
    }

    QTextEdit::keyPressEvent(event);
    if (!text.isEmpty() && !event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::AltModifier)) {
        showContextCompletion();
    }
}

void MarkdownTextEdit::setCompletionWords(const QStringList& words) {
    completionWords_ = words;
    completionWords_.removeDuplicates();
}

void MarkdownTextEdit::setNoteTitleCompletionWords(const QStringList& titles) {
    noteTitleCompletionWords_ = titles;
    noteTitleCompletionWords_.removeDuplicates();
}

void MarkdownTextEdit::showContextCompletion() {
    const QTextCursor cursor = textCursor();
    const QString blockText = cursor.block().text();
    const QString beforeCursor = blockText.left(cursor.positionInBlock());
    QString contextPrefix;
    QStringList candidates;
    QString kind;
    int replacementStart = cursor.position();

    const int tokenStart = qMax(beforeCursor.lastIndexOf(QLatin1Char(' ')),
                                beforeCursor.lastIndexOf(QLatin1Char('\t'))) + 1;
    if (beforeCursor.mid(tokenStart).startsWith(QLatin1Char('/'))) {
        contextPrefix = beforeCursor.mid(tokenStart + 1);
        replacementStart = cursor.position() - contextPrefix.size() - 1;
        candidates = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
                      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("code"),
                      QStringLiteral("table"), QStringLiteral("quote"), QStringLiteral("ul"),
                      QStringLiteral("ol"), QStringLiteral("link"), QStringLiteral("image")};
        kind = QStringLiteral("command");
    } else {
        const int wikiIndex = beforeCursor.lastIndexOf(QStringLiteral("[["));
        if (wikiIndex >= 0 && beforeCursor.indexOf(QStringLiteral("]]"), wikiIndex) < 0) {
            contextPrefix = beforeCursor.mid(wikiIndex + 2);
            replacementStart = cursor.position() - contextPrefix.size();
            candidates = noteTitleCompletionWords_;
            kind = QStringLiteral("wiki-link");
        } else {
            const int fenceIndex = beforeCursor.lastIndexOf(QStringLiteral("```"));
            const int closingFenceIndex = beforeCursor.indexOf(QStringLiteral("```"), fenceIndex + 3);
            if (fenceIndex >= 0 && closingFenceIndex < 0) {
                contextPrefix = beforeCursor.mid(fenceIndex + 3);
                replacementStart = cursor.position() - contextPrefix.size();
                candidates = {QStringLiteral("text"), QStringLiteral("cpp"), QStringLiteral("python"),
                              QStringLiteral("javascript"), QStringLiteral("json"), QStringLiteral("bash"),
                              QStringLiteral("sql"), QStringLiteral("html"), QStringLiteral("css")};
                kind = QStringLiteral("language");
            } else {
                const int hashIndex = beforeCursor.lastIndexOf(QLatin1Char('#'));
                const QRegularExpressionMatch headingMatch = QRegularExpression(QStringLiteral("^\\s*#+")).match(blockText);
                const bool headingMarker = headingMatch.hasMatch() && hashIndex >= headingMatch.capturedStart() &&
                                           hashIndex < headingMatch.capturedEnd();
                if (hashIndex >= 0 && !headingMarker && hashIndex > beforeCursor.lastIndexOf(QLatin1Char(']'))) {
                    contextPrefix = beforeCursor.mid(hashIndex + 1);
                    replacementStart = cursor.position() - contextPrefix.size();
                    candidates = completionWords_;
                    kind = QStringLiteral("tag");
                } else if (beforeCursor.endsWith(QLatin1Char('['))) {
                    contextPrefix.clear();
                    replacementStart = cursor.position();
                    candidates = {QStringLiteral("链接文本")};
                    kind = QStringLiteral("link");
                }
            }
        }
    }

    if (candidates.isEmpty()) {
        if (completer_->popup()->isVisible()) completer_->popup()->hide();
        return;
    }
    completionPrefix_ = contextPrefix;
    completionKind_ = kind;
    completionStart_ = replacementStart;
    completionModel_->setStringList(candidates);
    completer_->setCompletionPrefix(contextPrefix);
    QRect rect = cursorRect();
    rect.setWidth(completer_->popup()->sizeHintForColumn(0) +
                  completer_->popup()->verticalScrollBar()->sizeHint().width());
    completer_->complete(rect);
}

void MarkdownTextEdit::focusOutEvent(QFocusEvent* event) {
    detectCurrentLineHeading();
    QTextEdit::focusOutEvent(event);
}

void MarkdownTextEdit::detectCurrentLineHeading() {
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    QTextCharFormat format = block.charFormat();
    
    int fontSize = format.fontPointSize();
    bool isBold = format.fontWeight() > QFont::Normal;
    
    QString text = block.text().trimmed();
    
    int headingLevel = 0;
    if (isBold) {
        if (fontSize >= 16) headingLevel = 1;
        else if (fontSize >= 13) headingLevel = 2;
        else if (fontSize >= 11) headingLevel = 3;
    }
    
    emit currentLineHeadingChanged(headingLevel, text);
}

void MarkdownTextEdit::detectAllHeadings() {
    QTextDocument* doc = document();
    QTextBlock block = doc->begin();
    
    
    while (block.isValid()) {
        QString text = block.text().trimmed();
        
        if (!text.isEmpty()) {
            QTextBlock::iterator it;
            for (it = block.begin(); !(it.atEnd()); ++it) {
                QTextFragment fragment = it.fragment();
                if (fragment.isValid()) {
                    QTextCharFormat format = fragment.charFormat();
                    int fontSize = format.fontPointSize();
                    bool isBold = format.fontWeight() > QFont::Normal;
                    
                    
                    if (isBold && fontSize >= 11) {
                        int headingLevel = 0;
                        if (fontSize >= 16) headingLevel = 1;
                        else if (fontSize >= 13) headingLevel = 2;
                        else if (fontSize >= 11) headingLevel = 3;
                        
                        
                        if (headingLevel > 0) {
                            emit currentLineHeadingChanged(headingLevel, text);
                            return;
                        }
                    }
                    break;
                }
            }
        }
        
        block = block.next();
    }
    
    emit currentLineHeadingChanged(0, QString());
}

NoteEditor::NoteEditor(QWidget* parent)
    : QWidget(parent), currentNoteId_(-1), currentMode_(EditorMode::Edit),
      isSettingContent_(false), toolBar_(nullptr), splitter_(nullptr),
      editorContainer_(nullptr), previewContainer_(nullptr), webPreviewContainer_(nullptr),
      welcomeWidget_(nullptr), formatMenu_(nullptr), headingDetectTimer_(nullptr),
      webPreviewDebounceTimer_(nullptr), previewDebounceTimer_(nullptr), previewFutureWatcher_(nullptr),
      webPreviewFutureWatcher_(nullptr) {
    editor_ = new MarkdownTextEdit();
    preview_ = new QTextBrowser();
    webPreview_ = new QWebEngineView();
    webPreview_->setHtml("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"></head><body></body></html>");
    highlighter_ = new MarkdownHighlighter(editor_->document());
    parser_ = new MarkdownParser();
    headingDetectTimer_ = new QTimer(this);
    headingDetectTimer_->setSingleShot(true);
    headingDetectTimer_->setInterval(1000);

    webPreviewDebounceTimer_ = new QTimer(this);
    webPreviewDebounceTimer_->setSingleShot(true);
    webPreviewDebounceTimer_->setInterval(300);
    connect(webPreviewDebounceTimer_, &QTimer::timeout, this, [this]() {
        doUpdateWebPreview();
    });
    previewDebounceTimer_ = new QTimer(this);
    previewDebounceTimer_->setSingleShot(true);
    previewDebounceTimer_->setInterval(250);
    connect(previewDebounceTimer_, &QTimer::timeout, this, &NoteEditor::updatePreview);

    previewFutureWatcher_ = new QFutureWatcher<QString>(this);
    connect(previewFutureWatcher_, &QFutureWatcher<QString>::finished, this, [this]() {
        if (previewFutureWatcher_->isFinished()) {
            QString html = previewFutureWatcher_->result();
            onPreviewReady(html);
        }
    });

    webPreviewFutureWatcher_ = new QFutureWatcher<QString>(this);
    connect(webPreviewFutureWatcher_, &QFutureWatcher<QString>::finished, this, [this]() {
        if (webPreviewFutureWatcher_->isFinished()) {
            QString html = webPreviewFutureWatcher_->result();
            onWebPreviewReady(html);
        }
    });

    setupToolBar();
    setupUI();
    setupConnections();
}

NoteEditor::~NoteEditor() {
    if (previewDebounceTimer_) previewDebounceTimer_->stop();
    if (webPreviewDebounceTimer_) webPreviewDebounceTimer_->stop();
    if (previewFutureWatcher_ && previewFutureWatcher_->isRunning()) {
        previewFutureWatcher_->cancel();
        previewFutureWatcher_->waitForFinished();
    }
    if (webPreviewFutureWatcher_ && webPreviewFutureWatcher_->isRunning()) {
        webPreviewFutureWatcher_->cancel();
        webPreviewFutureWatcher_->waitForFinished();
    }
}

void NoteEditor::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(toolBar_);

    splitter_ = new QSplitter(Qt::Horizontal);

    editorContainer_ = new QWidget();
    QVBoxLayout* editorLayout = new QVBoxLayout(editorContainer_);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->addWidget(editor_);

    previewContainer_ = new QWidget();
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer_);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(preview_);

    webPreviewContainer_ = new QWidget();
    QVBoxLayout* webPreviewLayout = new QVBoxLayout(webPreviewContainer_);
    webPreviewLayout->setContentsMargins(0, 0, 0, 0);
    webPreviewLayout->addWidget(webPreview_);

    splitter_->addWidget(editorContainer_);
    splitter_->addWidget(previewContainer_);
    splitter_->addWidget(webPreviewContainer_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 1);

    mainLayout->addWidget(splitter_);

    showEditOnly();
}

void NoteEditor::setupToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setStyleSheet(getEditorToolBarStyle());
    toolBar_->setMovable(false);

    actionEdit_ = toolBar_->addAction("编辑");
    actionEdit_->setCheckable(true);
    actionEdit_->setChecked(true);

    actionPreview_ = toolBar_->addAction("预览");
    actionPreview_->setCheckable(true);

    actionWebPreview_ = toolBar_->addAction("Web预览");
    actionWebPreview_->setCheckable(true);

    actionSplit_ = toolBar_->addAction("分屏");
    actionSplit_->setCheckable(true);

    toolBar_->addSeparator();

    QAction* actionHeadingFormat = toolBar_->addAction("标题格式");
    connect(actionHeadingFormat, &QAction::triggered, this, &NoteEditor::showFormatMenu);

    actionCodeBlock_ = toolBar_->addAction("代码");
    actionTable_ = toolBar_->addAction("表格");
    actionLink_ = toolBar_->addAction("链接");
    actionImage_ = toolBar_->addAction("图片");
    actionList_ = toolBar_->addAction("列表");
    actionOrderedList_ = toolBar_->addAction("序号");
    actionBlockQuote_ = toolBar_->addAction("引用");

    connect(actionEdit_, &QAction::triggered, this, [this]() {
        if (actionEdit_->isChecked()) {
            setMode(EditorMode::Edit);
            actionPreview_->setChecked(false);
            actionWebPreview_->setChecked(false);
            actionSplit_->setChecked(false);
        }
    });

    connect(actionPreview_, &QAction::triggered, this, [this]() {
        if (actionPreview_->isChecked()) {
            setMode(EditorMode::Preview);
            actionEdit_->setChecked(false);
            actionWebPreview_->setChecked(false);
            actionSplit_->setChecked(false);
        }
    });

    connect(actionWebPreview_, &QAction::triggered, this, [this]() {
        if (actionWebPreview_->isChecked()) {
            setMode(EditorMode::WebPreview);
            actionEdit_->setChecked(false);
            actionPreview_->setChecked(false);
            actionSplit_->setChecked(false);
        }
    });

    connect(actionSplit_, &QAction::triggered, this, [this]() {
        if (actionSplit_->isChecked()) {
            setMode(EditorMode::Split);
            actionEdit_->setChecked(false);
            actionPreview_->setChecked(false);
            actionWebPreview_->setChecked(false);
        }
    });

    connect(actionCodeBlock_, &QAction::triggered, this, &NoteEditor::insertCodeBlock);
    connect(actionTable_, &QAction::triggered, this, &NoteEditor::insertTable);
    connect(actionLink_, &QAction::triggered, this, &NoteEditor::insertLink);
    connect(actionImage_, &QAction::triggered, this, &NoteEditor::insertImage);
    connect(actionList_, &QAction::triggered, this, &NoteEditor::insertList);
    connect(actionOrderedList_, &QAction::triggered, this, &NoteEditor::insertOrderedList);
    connect(actionBlockQuote_, &QAction::triggered, this, &NoteEditor::insertBlockQuote);
}

void NoteEditor::setupConnections() {
    connect(editor_, &QTextEdit::textChanged, this, &NoteEditor::onContentChanged);
    connect(editor_, &QTextEdit::cursorPositionChanged, this, [this]() {
        const QTextCursor cursor = editor_->textCursor();
        emit cursorSelectionChanged(cursor.anchor(), cursor.position());
    });
    connect(editor_, &MarkdownTextEdit::currentLineHeadingChanged, this, &NoteEditor::onHeadingDetected);
    connect(headingDetectTimer_, &QTimer::timeout, this, [this]() {
        editor_->detectAllHeadings();
    });
}

void NoteEditor::setupFormatMenu() {
    formatMenu_ = new QMenu(this);

    actionPlainText_ = formatMenu_->addAction("文本");
    actionPlainText_->setIcon(QIcon(":/icons/text.png"));
    QFont textFont = actionPlainText_->font();
    textFont.setPointSize(10);
    actionPlainText_->setFont(textFont);

    formatMenu_->addSeparator();

    actionH3_ = formatMenu_->addAction("三级标题");
    QFont h3Font = actionH3_->font();
    h3Font.setPointSize(11);
    h3Font.setBold(true);
    actionH3_->setFont(h3Font);

    actionH2_ = formatMenu_->addAction("二级标题");
    QFont h2Font = actionH2_->font();
    h2Font.setPointSize(13);
    h2Font.setBold(true);
    actionH2_->setFont(h2Font);

    actionH1_ = formatMenu_->addAction("一级标题");
    QFont h1Font = actionH1_->font();
    h1Font.setPointSize(16);
    h1Font.setBold(true);
    actionH1_->setFont(h1Font);

    connect(actionPlainText_, &QAction::triggered, this, &NoteEditor::onInsertPlainText);
    connect(actionH1_, &QAction::triggered, this, &NoteEditor::onInsertH1);
    connect(actionH2_, &QAction::triggered, this, &NoteEditor::onInsertH2);
    connect(actionH3_, &QAction::triggered, this, &NoteEditor::onInsertH3);
}

void NoteEditor::showFormatMenu() {
    if (!formatMenu_) {
        setupFormatMenu();
    }

    QRect cursorRect = editor_->cursorRect();
    QPoint globalPos = editor_->mapToGlobal(cursorRect.bottomLeft());
    
    formatMenu_->move(globalPos);
    formatMenu_->show();
}

void NoteEditor::applyHeadingFormat(int level) {
    QTextCursor cursor = editor_->textCursor();
    QTextBlock currentBlock = cursor.block();
    
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    
    switch (level) {
        case 1:
            format.setFontPointSize(16);
            break;
        case 2:
            format.setFontPointSize(13);
            break;
        case 3:
            format.setFontPointSize(11);
            break;
        default:
            format.setFontPointSize(10);
            format.setFontWeight(QFont::Normal);
            break;
    }
    
    // 保存当前光标位置
    int originalPosition = cursor.position();
    
    // 选中当前块的所有内容
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    
    // 应用格式到已有文字
    cursor.mergeCharFormat(format);
    
    // 设置光标的字符格式，新输入的文字会继承这个格式
    cursor.setCharFormat(format);
    
    // 恢复光标位置
    cursor.setPosition(originalPosition);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::onInsertPlainText() {
    applyHeadingFormat(0);
}

void NoteEditor::onInsertH1() {
    applyHeadingFormat(1);
}

void NoteEditor::onInsertH2() {
    applyHeadingFormat(2);
}

void NoteEditor::onInsertH3() {
    applyHeadingFormat(3);
}

void NoteEditor::setNoteId(qint64 noteId) {
    if (currentNoteId_ == noteId) return;
    currentNoteId_ = noteId;
    lastWebPreviewMarkdown_.clear();
    lastWebPreviewNoteId_ = -1;
    if (currentMode_ == EditorMode::WebPreview || currentMode_ == EditorMode::Split) updateWebPreview();
    emit noteOpened(noteId);
}

qint64 NoteEditor::currentNoteId() const {
    return currentNoteId_;
}

void NoteEditor::setMode(EditorMode mode) {
    EditorMode prevMode = currentMode_;
    currentMode_ = mode;

    // if (previewFutureWatcher_->isRunning()) {
    //     previewFutureWatcher_->cancel();
    // }
    switch (mode) {
        case EditorMode::Edit:
            showEditOnly();
            break;
        case EditorMode::Preview:
            showPreviewOnly();
            updatePreview();
            break;
        case EditorMode::WebPreview:
            if (prevMode != EditorMode::WebPreview && prevMode != EditorMode::Split) {
                clearWebPreview();
            }
            showWebPreviewOnly();
            updateWebPreview();
            break;
        case EditorMode::Split:
            if (prevMode != EditorMode::WebPreview && prevMode != EditorMode::Split) {
                clearWebPreview();
            }
            showSplitView();
            updateWebPreview();
            break;
    }

    emit modeChanged(mode);
}

NoteEditor::EditorMode NoteEditor::mode() const {
    return currentMode_;
}

void NoteEditor::hideToolBar() {
    if (toolBar_) {
        toolBar_->hide();
    }
}

void NoteEditor::showToolBar() {
    if (toolBar_) {
        toolBar_->show();
    }
}

void NoteEditor::setReadOnly(bool readOnly) {
    if (editor_) editor_->setReadOnly(readOnly);
}

void NoteEditor::setRemoteSelection(const QString& peerId, const QString& name, int anchor, int position) {
    if (peerId.isEmpty()) return;
    remoteSelections_.insert(peerId, RemoteSelection{name, anchor, position});
    refreshRemoteSelections();
}

void NoteEditor::removeRemoteSelection(const QString& peerId) {
    remoteSelections_.remove(peerId);
    refreshRemoteSelections();
}

void NoteEditor::clearRemoteSelections() {
    remoteSelections_.clear();
    if (editor_) editor_->setExtraSelections({});
}

void NoteEditor::refreshRemoteSelections() {
    if (!editor_) return;
    QList<QTextEdit::ExtraSelection> selections;
    const int textLength = editor_->toPlainText().size();
    for (auto it = remoteSelections_.cbegin(); it != remoteSelections_.cend(); ++it) {
        QTextEdit::ExtraSelection selection;
        QTextCursor cursor(editor_->document());
        const int start = qBound(0, qMin(it->anchor, it->position), textLength);
        const int end = qBound(start, qMax(it->anchor, it->position), textLength);
        cursor.setPosition(start);
        if (end > start) cursor.setPosition(end, QTextCursor::KeepAnchor);
        else if (start < textLength) cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
        selection.cursor = cursor;
        const uint hue = qHash(it.key()) % 360;
        QColor color = QColor::fromHsv(static_cast<int>(hue), 120, 255, 90);
        selection.format.setBackground(color);
        selection.format.setToolTip(it->name);
        selections.append(selection);
    }
    editor_->setExtraSelections(selections);
}

void NoteEditor::setCompletionWords(const QStringList& words) {
    if (editor_) editor_->setCompletionWords(words);
}

void NoteEditor::setNoteTitleCompletionWords(const QStringList& titles) {
    if (editor_) editor_->setNoteTitleCompletionWords(titles);
}

QTextCursor NoteEditor::textCursor() const {
    return editor_ ? editor_->textCursor() : QTextCursor();
}
QString NoteEditor::content() const {
    QString result = editor_->toPlainText();
    return result;
}

void NoteEditor::setContent(const QString& content) {
    isSettingContent_ = true;
    editor_->setPlainText(content);
    isSettingContent_ = false;
    if (currentNoteId_ > 0) {
        emit contentChanged(currentNoteId_, editor_->toPlainText());
    }
}

void NoteEditor::insertCodeBlock() {
    QTextCursor cursor = editor_->textCursor();
    cursor.insertText("\n```\n\n```\n");
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertTable() {
    QTextCursor cursor = editor_->textCursor();
    QString table = "\n| 列1 | 列2 | 列3 |\n| --- | --- | --- |\n|  |  |  |\n";
    cursor.insertText(table);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertLink() {
    QTextCursor cursor = editor_->textCursor();
    QString templateText = "[链接文本](https://)";
    cursor.insertText(templateText);
    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, templateText.length());
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertImage() {
    QTextCursor cursor = editor_->textCursor();
    QString templateText = "![图片描述](https://)";
    cursor.insertText(templateText);
    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, templateText.length());
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertList() {
    QTextCursor cursor = editor_->textCursor();
    cursor.insertText("\n- 列表项1\n- 列表项2\n- 列表项3\n");
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertOrderedList() {
    QTextCursor cursor = editor_->textCursor();
    cursor.insertText("\n1. 列表项1\n2. 列表项2\n3. 列表项3\n");
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::insertBlockQuote() {
    QTextCursor cursor = editor_->textCursor();
    cursor.insertText("\n> 引用文本\n");
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void NoteEditor::scrollToPosition(int position) {
    QTextCursor cursor(editor_->document());
    cursor.setPosition(qBound(0, position, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(cursor);
    editor_->ensureCursorVisible();
}

void NoteEditor::applyAiResult(const QString& markdown, int selectionStart, int selectionEnd, bool append) {
    QTextCursor cursor(editor_->document());
    if (append) {
        cursor.movePosition(QTextCursor::End);
    } else if (selectionStart >= 0 && selectionEnd >= selectionStart) {
        cursor.setPosition(qBound(0, selectionStart, editor_->document()->characterCount() - 1));
        cursor.setPosition(qBound(0, selectionEnd, editor_->document()->characterCount() - 1), QTextCursor::KeepAnchor);
    } else {
        cursor.select(QTextCursor::Document);
    }
    cursor.insertText(markdown);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

int NoteEditor::cursorPosition() const {
    return editor_->textCursor().position();
}

int NoteEditor::verticalScrollPosition() const {
    return editor_->verticalScrollBar()->value();
}

int NoteEditor::selectionStart() const {
    return editor_->textCursor().selectionStart();
}

int NoteEditor::selectionEnd() const {
    return editor_->textCursor().selectionEnd();
}

int NoteEditor::editorModeValue() const {
    return static_cast<int>(currentMode_);
}

void NoteEditor::restoreNavigationState(int cursorPosition, int scrollPosition,
                                       int selectionStart, int selectionEnd, int mode) {
    QTextCursor cursor(editor_->document());
    const int maximum = qMax(0, editor_->document()->characterCount() - 1);
    cursor.setPosition(qBound(0, cursorPosition, maximum));
    if (selectionStart >= 0 && selectionEnd >= selectionStart) {
        cursor.setPosition(qBound(0, selectionStart, maximum));
        cursor.setPosition(qBound(0, selectionEnd, maximum), QTextCursor::KeepAnchor);
    }
    editor_->setTextCursor(cursor);
    editor_->verticalScrollBar()->setValue(scrollPosition);
    if (mode >= static_cast<int>(EditorMode::Edit) && mode <= static_cast<int>(EditorMode::Split)) {
        setMode(static_cast<EditorMode>(mode));
    }
}

void NoteEditor::scrollToHeading(const QString& headingText) {
    
    QTextDocument* doc = editor_->document();
    QTextBlock block = doc->begin();
    int blockNum = 0;
    
    while (block.isValid()) {
        QString text = block.text().trimmed();
        
        if (!text.isEmpty()) {
            QTextBlock::iterator it;
            bool isHeading = false;
            int headingLevel = 0;
            QString plainText;
            
            for (it = block.begin(); !(it.atEnd()); ++it) {
                QTextFragment fragment = it.fragment();
                if (fragment.isValid()) {
                    QTextCharFormat format = fragment.charFormat();
                    int fontSize = format.fontPointSize();
                    bool isBold = format.fontWeight() > QFont::Normal;
                    
                    if (isBold && fontSize >= 11 && !isHeading) {
                        if (fontSize >= 16) headingLevel = 1;
                        else if (fontSize >= 13) headingLevel = 2;
                        else if (fontSize >= 11) headingLevel = 3;
                        
                        if (headingLevel > 0) {
                            isHeading = true;
                        }
                    }
                    
                    plainText += fragment.text();
                }
            }
            
            QString trimmedPlainText = plainText.trimmed();
            
            QString cleanText = trimmedPlainText;
            if (cleanText.startsWith("# ")) cleanText = cleanText.mid(2);
            else if (cleanText.startsWith("## ")) cleanText = cleanText.mid(3);
            else if (cleanText.startsWith("### ")) cleanText = cleanText.mid(4);
            else if (cleanText.startsWith("#### ")) cleanText = cleanText.mid(5);
            else if (cleanText.startsWith("##### ")) cleanText = cleanText.mid(6);
            else if (cleanText.startsWith("###### ")) cleanText = cleanText.mid(7);
            cleanText = cleanText.trimmed();
            
            
            if (isHeading && headingLevel > 0) {
                if (cleanText == headingText) {
                    QTextCursor cursor(block);
                    editor_->setTextCursor(cursor);
                    editor_->ensureCursorVisible();
                    
                    QRect cursorRect = editor_->cursorRect();
                    int cursorYInViewport = cursorRect.top();
                    int currentScrollValue = editor_->verticalScrollBar()->value();
                    int targetScrollValue = currentScrollValue + cursorYInViewport;
                    int maxScrollValue = editor_->verticalScrollBar()->maximum();
                    editor_->verticalScrollBar()->setValue(qMin(targetScrollValue, maxScrollValue));
                    return;
                } else if (cleanText.contains(headingText)) {
                    QTextCursor cursor(block);
                    editor_->setTextCursor(cursor);
                    editor_->ensureCursorVisible();
                    
                    QRect cursorRect = editor_->cursorRect();
                    int cursorYInViewport = cursorRect.top();
                    int currentScrollValue = editor_->verticalScrollBar()->value();
                    int targetScrollValue = currentScrollValue + cursorYInViewport;
                    int maxScrollValue = editor_->verticalScrollBar()->maximum();
                    editor_->verticalScrollBar()->setValue(qMin(targetScrollValue, maxScrollValue));
                    return;
                }
            }
        }
        
        block = block.next();
        blockNum++;
    }
    
}

MarkdownHighlighter* NoteEditor::highlighter() const {
    return highlighter_;
}

void NoteEditor::onContentChanged() {
    refreshRemoteSelections();
    if (isSettingContent_) {
            return;
    }


    if (currentMode_ == EditorMode::Split || currentMode_ == EditorMode::WebPreview) updateWebPreview();
    if (currentMode_ == EditorMode::Preview && previewDebounceTimer_) previewDebounceTimer_->start();

    if (currentNoteId_ != -1) {
        QString markdown = editor_->toPlainText();
        emit contentChanged(currentNoteId_, markdown);
    }

    headingDetectTimer_->start();
}

QString NoteEditor::richTextToMarkdown() const {
    QTextDocument* doc = editor_->document();
    QTextBlock block = doc->begin();
    QStringList markdownLines;
    
    while (block.isValid()) {
        QString text = block.text();
        
        if (text.isEmpty()) {
            markdownLines.append(QString());
            block = block.next();
            continue;
        }
        
        QTextBlock::iterator it;
        bool isHeading = false;
        int headingLevel = 0;
        QString plainText;
        
        for (it = block.begin(); !(it.atEnd()); ++it) {
            QTextFragment fragment = it.fragment();
            if (fragment.isValid()) {
                QTextCharFormat format = fragment.charFormat();
                int fontSize = format.fontPointSize();
                bool isBold = format.fontWeight() > QFont::Normal;
                
                if (isBold && fontSize >= 11 && !isHeading) {
                    if (fontSize >= 16) headingLevel = 1;
                    else if (fontSize >= 13) headingLevel = 2;
                    else if (fontSize >= 11) headingLevel = 3;
                    
                    if (headingLevel > 0) {
                        isHeading = true;
                    }
                }
                
                plainText += fragment.text();
            }
        }
        
        if (isHeading && headingLevel > 0) {
            QString prefix;
            for (int i = 0; i < headingLevel; ++i) {
                prefix += "#";
            }
            markdownLines.append(prefix + " " + plainText.trimmed());
        } else {
            markdownLines.append(plainText);
        }
        
        block = block.next();
    }
    
    return markdownLines.join("\n");
}

void NoteEditor::onToggleMode() {
    setMode(currentMode_ == EditorMode::Edit ? EditorMode::Preview : EditorMode::Edit);
}

void NoteEditor::updatePreview() {
    if (previewDebounceTimer_) previewDebounceTimer_->stop();
    const QString markdown = editor_->toPlainText();

    // Keep one render in flight; stale completion schedules the latest snapshot.
    if (previewFutureWatcher_->isRunning()) return;
    lastPreviewMarkdown_ = markdown;

    QFuture<QString> future = parser_->parseToHTMLAsync(markdown);
    previewFutureWatcher_->setFuture(future);
}

void NoteEditor::onPreviewReady(const QString& html) {
    if (editor_->toPlainText() != lastPreviewMarkdown_) {
        if (currentMode_ == EditorMode::Preview && previewDebounceTimer_) {
            previewDebounceTimer_->start(editor_->document()->characterCount() > 64000 ? 900 : 250);
        }
        return;
    }
    if (currentMode_ == EditorMode::Preview) {
        preview_->setHtml(html);
    }
}

void NoteEditor::updateWebPreview() {
    if (!webPreviewDebounceTimer_) return;
    const int documentSize = editor_->document()->characterCount();
    webPreviewDebounceTimer_->start(documentSize > 64000 ? 900 : 300);
}

void NoteEditor::doUpdateWebPreview() {
    const QString markdown = editor_->toPlainText();
    if (markdown == lastWebPreviewMarkdown_ && lastWebPreviewNoteId_ == currentNoteId_) {
        return;
    }

    // Do not queue full-document renders faster than the worker can consume them.
    // The completion handler rechecks content/note identity and schedules the newest snapshot.
    if (webPreviewFutureWatcher_->isRunning()) return;
    lastWebPreviewMarkdown_ = markdown;
    lastWebPreviewNoteId_ = currentNoteId_;

    QFuture<QString> future = parser_->parseToWebHTMLAsync(currentNoteId_, markdown);
    webPreviewFutureWatcher_->setFuture(future);
}

void NoteEditor::onWebPreviewReady(const QString& html) {
    const QString currentMarkdown = editor_->toPlainText();
    if (currentMarkdown != lastWebPreviewMarkdown_ || currentNoteId_ != lastWebPreviewNoteId_) {
        updateWebPreview();
        return;
    }
    if (currentNoteId_ > 0) parser_->updateCache(currentNoteId_, currentMarkdown, html);
    if (currentMode_ == EditorMode::WebPreview || currentMode_ == EditorMode::Split) {
        webPreview_->setHtml(html, QUrl("about:blank"));
    }
}

void NoteEditor::showEditOnly() {
    editorContainer_->setVisible(true);
    previewContainer_->setVisible(false);
    webPreviewContainer_->setVisible(false);
}

void NoteEditor::showPreviewOnly() {
    editorContainer_->setVisible(false);
    previewContainer_->setVisible(true);
    webPreviewContainer_->setVisible(false);
}

void NoteEditor::showWebPreviewOnly() {
    editorContainer_->setVisible(false);
    previewContainer_->setVisible(false);
    webPreviewContainer_->setVisible(true);
}

void NoteEditor::clearWebPreview() {
    if (webPreview_) {
        webPreview_->setHtml("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"></head><body></body></html>");
    }
    lastWebPreviewMarkdown_.clear();
    lastWebPreviewNoteId_ = -1;
}

void NoteEditor::forceUpdateWebPreview() {
    lastWebPreviewMarkdown_.clear();
    lastWebPreviewNoteId_ = -1;
    if (!editor_->toPlainText().isEmpty() && webPreviewDebounceTimer_) {
        webPreviewDebounceTimer_->start(0);
    }
}

void NoteEditor::showSplitView() {
    editorContainer_->setVisible(true);
    previewContainer_->setVisible(false);
    webPreviewContainer_->setVisible(true);
}

void NoteEditor::onHeadingDetected(int level, const QString& text) {
    if (level > 0 && !text.isEmpty() && currentNoteId_ > 0) {
        emit headingCreated(text);
    }
}
