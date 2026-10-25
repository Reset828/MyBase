#include "mainwindow.h"
#include "passworddialog.h"
#include "encryptednotesdialog.h"
#include "contentdelivery.h"
#include "markdownhtmlrenderer.h"
#include "aicredentials.h"
#include "sessionstate.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QProgressDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <QTextEdit>
#include <QFormLayout>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDate>
#include <QUrl>
#include <QTextEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QSharedPointer>
#include <atomic>
#include <QColor>
#include <QTextCursor>
#include <QTableWidget>
#include <QHeaderView>
#include <QListWidget>
#include <QPointer>
#include <QCheckBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QWidgetAction>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QTimer>
#include <QtConcurrent>
#include <functional>

namespace {
void highlightLineDiff(QTextEdit* oldView, QTextEdit* newView, const QString& oldText, const QString& newText) {
    const QStringList oldLines = oldText.split(QLatin1Char('\n'));
    const QStringList newLines = newText.split(QLatin1Char('\n'));
    QSet<int> removed;
    QSet<int> added;
    const qint64 cells = static_cast<qint64>(oldLines.size() + 1) * (newLines.size() + 1);
    if (cells <= 1000000) {
        const int width = newLines.size() + 1;
        QVector<int> lcs(static_cast<int>(cells), 0);
        for (int i = oldLines.size() - 1; i >= 0; --i) {
            for (int j = newLines.size() - 1; j >= 0; --j) {
                const int index = i * width + j;
                lcs[index] = oldLines.at(i) == newLines.at(j)
                    ? 1 + lcs[(i + 1) * width + j + 1]
                    : qMax(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);
            }
        }
        int i = 0, j = 0;
        while (i < oldLines.size() && j < newLines.size()) {
            if (oldLines.at(i) == newLines.at(j)) { ++i; ++j; }
            else if (lcs[(i + 1) * width + j] >= lcs[i * width + j + 1]) removed.insert(i++);
            else added.insert(j++);
        }
        while (i < oldLines.size()) removed.insert(i++);
        while (j < newLines.size()) added.insert(j++);
    } else {
        const int common = qMin(oldLines.size(), newLines.size());
        for (int i = 0; i < common; ++i) {
            if (oldLines.at(i) != newLines.at(i)) { removed.insert(i); added.insert(i); }
        }
        for (int i = common; i < oldLines.size(); ++i) removed.insert(i);
        for (int i = common; i < newLines.size(); ++i) added.insert(i);
    }
    const auto apply = [](QTextEdit* view, const QSet<int>& lines, const QColor& color) {
        for (int line : lines) {
            QTextBlock block = view->document()->findBlockByNumber(line);
            if (!block.isValid()) continue;
            QTextCursor cursor(block);
            cursor.select(QTextCursor::LineUnderCursor);
            QTextCharFormat format;
            format.setBackground(color);
            cursor.mergeCharFormat(format);
        }
    };
    apply(oldView, removed, QColor(255, 220, 220));
    apply(newView, added, QColor(218, 245, 220));
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), noteManager_(nullptr), tagManager_(nullptr),
      searchEngine_(nullptr), noteTabManager_(nullptr), mainSplitter_(nullptr),
      leftSplitter_(nullptr), leftPanel_(nullptr), leftLayout_(nullptr),
      tagLabel_(nullptr), tagTreeView_(nullptr), knowledgeNavigationTabs_(nullptr),
      outlineView_(nullptr), outlineModel_(nullptr), centerPanel_(nullptr),
      centerLayout_(nullptr), searchLineEdit_(nullptr), tagMatchModeCombo_(nullptr), noteListView_(nullptr),
      searchResultModel_(nullptr), noteListModel_(nullptr),
      menuBar_(nullptr), toolBar_(nullptr), statusBar_(nullptr),
      actionNewNote_(nullptr), actionDeleteNote_(nullptr), actionEncryptNote_(nullptr),
      actionOpenEncryptedNotes_(nullptr), actionOpenTrash_(nullptr), actionExit_(nullptr),
      actionAbout_(nullptr) {
    setWindowTitle("MyBase");
    resize(1200, 800);
    setStyleSheet(getThemeStyleSheet());
}

MainWindow::~MainWindow() {
    if (collaborationClient_) { collaborationClient_->leave(); collaborationClient_ = nullptr; }
    ++searchRequestId_;
    for (QFuture<QVector<SearchResult>>& future : activeSearchFutures_) {
        future.waitForFinished();
    }

    delete noteTabManager_;
    noteTabManager_ = nullptr;
    searchEngine_ = nullptr;
    tagManager_ = nullptr;
    noteManager_ = nullptr;
    applicationContext_.reset();
}

bool MainWindow::initialize(const QString& dbPath) {
    applicationContext_ = std::make_unique<ApplicationContext>(dbPath);
    if (!applicationContext_->initialize()) {
        QMessageBox::critical(this, "Error", "Failed to open database");
        applicationContext_.reset();
        return false;
    }

    noteManager_ = &applicationContext_->notes();
    tagManager_ = &applicationContext_->tags();
    searchEngine_ = &applicationContext_->search();

    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupConnections();
    autoSaveTimer_ = new QTimer(this);
    autoSaveTimer_->setSingleShot(true);
    autoSaveTimer_->setInterval(700);
    connect(autoSaveTimer_, &QTimer::timeout, this, [this]() { flushPendingSaves(); });
    sessionSnapshotTimer_ = new QTimer(this);
    sessionSnapshotTimer_->setInterval(10000);
    connect(sessionSnapshotTimer_, &QTimer::timeout, this, [this]() { saveSessionState(); });
    sessionSnapshotTimer_->start();
    QSettings startupSettings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    startupSettings.setValue(QStringLiteral("session/unclean"), true);
    startupSettings.sync();
    restoreWindowState();
    loadNotes();
    restoreSessionState();
    
    return true;
}

void MainWindow::setupUI() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建主分割器
    mainSplitter_ = new QSplitter(Qt::Horizontal);

    // 最左侧：笔记列表面板（宽度250）
    leftPanel_ = new QWidget();
    leftPanel_->setMinimumWidth(200);
    leftPanel_->setStyleSheet(getPanelStyle());
    leftLayout_ = new QVBoxLayout(leftPanel_);
    leftLayout_->setContentsMargins(8, 8, 8, 8);
    leftLayout_->setSpacing(6);

    QLabel* noteLabel_ = new QLabel("笔记");
    noteLabel_->setStyleSheet(getPanelLabelStyle());
    leftLayout_->addWidget(noteLabel_);

    searchLineEdit_ = new QLineEdit();
    searchLineEdit_->setPlaceholderText("搜索笔记...");
    leftLayout_->addWidget(searchLineEdit_);
    tagMatchModeCombo_ = new QComboBox();
    tagMatchModeCombo_->addItem(QStringLiteral("标签全部匹配 (AND)"), static_cast<int>(TagMatchMode::And));
    tagMatchModeCombo_->addItem(QStringLiteral("标签任一匹配 (OR)"), static_cast<int>(TagMatchMode::Or));
    leftLayout_->addWidget(tagMatchModeCombo_);

    noteListView_ = new QListView();
    noteListView_->setFrameShape(QFrame::NoFrame);
    leftLayout_->addWidget(noteListView_);

    // 中间：标签树面板（宽度200）
    centerPanel_ = new QWidget();
    centerPanel_->setMinimumWidth(150);
    centerPanel_->setStyleSheet(getPanelStyle());
    centerLayout_ = new QVBoxLayout(centerPanel_);
    centerLayout_->setContentsMargins(8, 8, 8, 8);
    centerLayout_->setSpacing(6);

    tagLabel_ = new QLabel("标签");
    tagLabel_->setStyleSheet(getPanelLabelStyle());
    centerLayout_->addWidget(tagLabel_);

    knowledgeNavigationTabs_ = new QTabWidget();
    tagTreeView_ = new TagTreeView();
    tagTreeView_->setFrameShape(QFrame::NoFrame);
    outlineView_ = new QTreeView();
    outlineView_->setFrameShape(QFrame::NoFrame);
    outlineView_->setHeaderHidden(true);
    outlineView_->setExpandsOnDoubleClick(true);
    outlineModel_ = new MarkdownOutlineModel(outlineView_);
    outlineView_->setModel(outlineModel_);
    knowledgeNavigationTabs_->addTab(tagTreeView_, QStringLiteral("标签"));
    knowledgeNavigationTabs_->addTab(outlineView_, QStringLiteral("大纲"));
    centerLayout_->addWidget(knowledgeNavigationTabs_);

    // 右侧：多标签页编辑器面板（占据大部分空间）
    noteTabManager_ = new NoteTabManager();
    noteTabManager_->setMinimumWidth(400);

    // 添加到主分割器
    mainSplitter_->addWidget(leftPanel_);
    mainSplitter_->addWidget(centerPanel_);
    mainSplitter_->addWidget(noteTabManager_);

    // 设置初始宽度
    mainSplitter_->setSizes({250, 200, 750});

    mainLayout->addWidget(mainSplitter_);

    // 设置模型
    searchResultModel_ = new SearchResultModel(this);
    noteListModel_ = new NoteListModel(applicationContext_->database().noteRepository(),
                                       applicationContext_->database().noteTagRepository(), this);
    noteListModel_->refresh();
    noteListView_->setModel(noteListModel_);

    connect(noteManager_, &NoteManager::noteListChanged, noteListModel_, [model = noteListModel_]() {
        model->refresh();
    });
    connect(tagManager_, &TagManager::tagDataChanged, noteListModel_, [model = noteListModel_]() {
        model->refresh();
    });

    if (tagManager_) {
        tagTreeView_->setTagManager(tagManager_);
    }

}

void MainWindow::setupMenuBar() {
    menuBar_ = menuBar();

    QMenu* fileMenu = menuBar_->addMenu("文件(&F)");
    actionNewNote_ = fileMenu->addAction("新建笔记(&N)");
    actionNewNote_->setShortcut(QKeySequence::New);
    actionDeleteNote_ = fileMenu->addAction("删除笔记(&D)");
    actionDeleteNote_->setShortcut(QKeySequence::Delete);
    fileMenu->addSeparator();
    actionEncryptNote_ = fileMenu->addAction("加密笔记(&E)");
    actionEncryptNote_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    actionOpenEncryptedNotes_ = fileMenu->addAction("加密笔记列表(&U)");
    actionOpenEncryptedNotes_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    QMenu* exportMenu = fileMenu->addMenu(QStringLiteral("导出"));
    const auto addExportAction = [this, exportMenu](const QString& label, const QString& filter,
                                                    const QString& suffix, int format) {
        QAction* action = exportMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, filter, suffix, format]() {
            NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
            if (!editor || editor->currentNoteId() <= 0) {
                QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("请先打开一篇笔记"));
                return;
            }
            const qint64 noteId = editor->currentNoteId();
            const Note note = noteManager_->getNote(noteId);
            if (note.isEncrypted || encryptedNotesOpen_.contains(noteId)) {
                QMessageBox::warning(this, QStringLiteral("导出受限"),
                                     QStringLiteral("加密笔记不会通过普通导出流程写入文件。"));
                return;
            }
            const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出笔记"),
                                                               note.title + suffix, filter);
            if (path.isEmpty()) return;
            const bool overwrite = QFileInfo::exists(path);
            if (overwrite && QMessageBox::question(this, QStringLiteral("确认覆盖"),
                    QStringLiteral("目标文件已存在，是否覆盖？\n%1").arg(path),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            const QString markdown = editor->content();
            auto cancelled = QSharedPointer<std::atomic_bool>::create(false);
            auto* progress = new QProgressDialog(QStringLiteral("正在导出…"), QStringLiteral("取消"), 0, 0, this);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->setAutoClose(false);
            progress->setAutoReset(false);
            auto* watcher = new QFutureWatcher<QString>(this);
            connect(progress, &QProgressDialog::canceled, this, [cancelled]() { cancelled->store(true); });
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, progress, path]() {
                const QString error = watcher->result();
                watcher->deleteLater();
                progress->close();
                progress->deleteLater();
                if (!error.isEmpty() && error != QStringLiteral("导出已取消"))
                    QMessageBox::warning(this, QStringLiteral("导出失败"), error);
                else if (error.isEmpty())
                    statusBar_->showMessage(QStringLiteral("导出完成：%1").arg(path), 5000);
                else
                    statusBar_->showMessage(QStringLiteral("导出已取消"), 3000);
            });
            watcher->setFuture(QtConcurrent::run([markdown, path, format, overwrite, cancelled]() -> QString {
                MarkdownHtmlRenderer renderer;
                DocumentExporter exporter(renderer);
                QString error;
                bool succeeded = false;
                if (format == 0) succeeded = exporter.exportMarkdown(markdown, path, &error, overwrite, cancelled.data());
                else if (format == 1) succeeded = exporter.exportHtml(markdown, path, &error, overwrite, cancelled.data());
                else succeeded = exporter.exportPdf(markdown, path, &error, overwrite, cancelled.data());
                return succeeded ? QString() : (error.isEmpty() ? QStringLiteral("导出失败") : error);
            }));
            progress->show();
        });
    };
    addExportAction(QStringLiteral("Markdown…"), QStringLiteral("Markdown (*.md)"), QStringLiteral(".md"), 0);
    addExportAction(QStringLiteral("HTML…"), QStringLiteral("HTML (*.html)"), QStringLiteral(".html"), 1);
    addExportAction(QStringLiteral("PDF…"), QStringLiteral("PDF (*.pdf)"), QStringLiteral(".pdf"), 2);
    fileMenu->addSeparator();
    actionOpenTrash_ = fileMenu->addAction("回收站(&T)");
    fileMenu->addSeparator();
    actionExit_ = fileMenu->addAction("退出(&X)");
    actionExit_->setShortcut(QKeySequence::Quit);

    QMenu* toolsMenu = menuBar_->addMenu(QStringLiteral("工具(&T)"));
    QAction* bookmarkAdd = toolsMenu->addAction(QStringLiteral("添加书签"));
    QAction* bookmarkList = toolsMenu->addAction(QStringLiteral("书签列表 / 删除书签"));
    QAction* revisionsAction = toolsMenu->addAction(QStringLiteral("修订历史"));
    QAction* collaborateAction = toolsMenu->addAction(QStringLiteral("实时协作连接…"));
    QAction* disconnectCollaborationAction = toolsMenu->addAction(QStringLiteral("断开实时协作"));
    disconnectCollaborationAction->setEnabled(false);
    connect(collaborateAction, &QAction::triggered, this, [this, disconnectCollaborationAction]() {
        startCollaboration();
        disconnectCollaborationAction->setEnabled(collaborationClient_ != nullptr);
    });
    connect(disconnectCollaborationAction, &QAction::triggered, this, [this, disconnectCollaborationAction]() {
        const qint64 disconnectedNoteId = collaborationNoteId_;
        if (collaborationClient_) { collaborationClient_->leave(); collaborationClient_ = nullptr; }
        if (noteTabManager_) if (NoteEditor* editor = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(disconnectedNoteId))) { editor->setReadOnly(false); editor->clearRemoteSelections(); }
        remoteCollaborationPeers_.clear();
        collaborationNoteId_ = -1;
        disconnectCollaborationAction->setEnabled(false);
        statusBar_->showMessage(QStringLiteral("已断开实时协作"));
    });
    QMenu* templatesMenu = toolsMenu->addMenu(QStringLiteral("模板"));
    QAction* applyTemplateAction = templatesMenu->addAction(QStringLiteral("插入模板…"));
    QAction* createTemplateAction = templatesMenu->addAction(QStringLiteral("保存当前内容为模板…"));
    QAction* manageTemplateAction = templatesMenu->addAction(QStringLiteral("管理自定义模板…"));
    QAction* importTemplateAction = templatesMenu->addAction(QStringLiteral("导入模板…"));
    QAction* exportTemplateAction = templatesMenu->addAction(QStringLiteral("导出模板…"));
    QAction* newFromTemplateAction = templatesMenu->addAction(QStringLiteral("从模板新建笔记…"));
    connect(bookmarkAdd, &QAction::triggered, this, [this]() {
        NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
        if (!editor || encryptedNotesOpen_.contains(editor->currentNoteId())) return;
        bool ok = false;
        const QString label = QInputDialog::getText(this, QStringLiteral("添加书签"), QStringLiteral("书签名称"), QLineEdit::Normal, QString(), &ok);
        if (!ok || label.trimmed().isEmpty()) return;
        const QString markdown = editor->content();
        const int position = qBound(0, editor->cursorPosition(), markdown.size());
        const int lineStart = markdown.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
        int lineEnd = markdown.indexOf(QLatin1Char('\n'), position);
        if (lineEnd < 0) lineEnd = markdown.size();
        const QString anchor = markdown.mid(lineStart, lineEnd - lineStart).trimmed();
        const QString fingerprint = QString::fromLatin1(QCryptographicHash::hash(anchor.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (applicationContext_->localFeatures().addBookmark(editor->currentNoteId(), label, position, anchor, fingerprint)) statusBar_->showMessage(QStringLiteral("书签已保存"));
    });
    connect(bookmarkList, &QAction::triggered, this, [this]() {
        const auto items = applicationContext_->localFeatures().allBookmarks();
        QStringList labels;
        QVector<NoteBookmark> visibleBookmarks;
        for (const auto& item : items) {
            const Note note = noteManager_->getNote(item.noteId);
            if (note.id > 0 && !note.isEncrypted && !note.isDeleted) {
                visibleBookmarks.append(item);
                labels << QStringLiteral("%1 — %2").arg(note.title, item.label);
            }
        }
        if (labels.isEmpty()) { QMessageBox::information(this, QStringLiteral("书签"), QStringLiteral("没有可用书签")); return; }
        bool ok = false; const QString selected = QInputDialog::getItem(this, QStringLiteral("跨笔记书签"), QStringLiteral("选择书签"), labels, 0, false, &ok);
        if (!ok) return;
        const int row = labels.indexOf(selected);
        if (row < 0 || row >= visibleBookmarks.size()) return;
        const NoteBookmark bookmark = visibleBookmarks.at(row);
        Note note = noteManager_->getNote(bookmark.noteId);
        int tab = noteTabManager_->findTabByNoteId(note.id);
        if (tab < 0) tab = noteTabManager_->openNote(note.id, note.title, note.content);
        NoteEditor* target = noteTabManager_->editorAt(tab);
        noteTabManager_->setCurrentIndex(tab);
        if (!target) return;
        int position = bookmark.position;
        if (!bookmark.anchor.isEmpty()) {
            const int anchoredPosition = target->content().indexOf(bookmark.anchor);
            if (anchoredPosition >= 0) position = anchoredPosition;
        }
        target->scrollToPosition(position);
        const QStringList operations{QStringLiteral("完成"), QStringLiteral("重命名"), QStringLiteral("删除")};
        const QString operation = QInputDialog::getItem(this, QStringLiteral("书签"), QStringLiteral("操作"), operations, 0, false, &ok);
        if (!ok || operation == QStringLiteral("完成")) return;
        if (operation == QStringLiteral("删除")) applicationContext_->localFeatures().removeBookmark(bookmark.id);
        else {
            const QString newLabel = QInputDialog::getText(this, QStringLiteral("重命名书签"), QStringLiteral("名称"), QLineEdit::Normal, bookmark.label, &ok);
            if (ok && !newLabel.trimmed().isEmpty()) applicationContext_->localFeatures().renameBookmark(bookmark.id, newLabel);
        }
    });
    connect(revisionsAction, &QAction::triggered, this, [this]() {
        NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
        if (!editor || encryptedNotesOpen_.contains(editor->currentNoteId())) return;
        const auto items = applicationContext_->localFeatures().revisions(editor->currentNoteId());
        if (items.isEmpty()) { QMessageBox::information(this, QStringLiteral("修订历史"), QStringLiteral("暂无修订记录")); return; }
        const QPointer<NoteEditor> guardedEditor(editor);

        QDialog historyDialog(this);
        historyDialog.setWindowTitle(QStringLiteral("修订历史"));
        historyDialog.resize(640, 420);
        auto* historyLayout = new QVBoxLayout(&historyDialog);
        auto* revisionList = new QListWidget(&historyDialog);
        revisionList->setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (int index = 0; index < items.size(); ++index) {
            const auto& item = items.at(index);
            auto* row = new QListWidgetItem(QDateTime::fromMSecsSinceEpoch(item.createdTime).toString(Qt::ISODate)
                                            + QStringLiteral(" — ") + item.title, revisionList);
            row->setData(Qt::UserRole, index);
        }
        historyLayout->addWidget(revisionList);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &historyDialog);
        QPushButton* compare = buttons->addButton(QStringLiteral("比较所选"), QDialogButtonBox::ActionRole);
        QPushButton* restore = buttons->addButton(QStringLiteral("恢复所选"), QDialogButtonBox::ActionRole);
        historyLayout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &historyDialog, &QDialog::reject);
        QObject::connect(compare, &QPushButton::clicked, &historyDialog, [this, guardedEditor, revisionList, &items]() {
            if (!guardedEditor) return;
            const QList<QListWidgetItem*> selectedRows = revisionList->selectedItems();
            if (selectedRows.isEmpty() || selectedRows.size() > 2) {
                QMessageBox::information(this, QStringLiteral("修订比较"), QStringLiteral("请选择一个版本与当前内容比较，或选择两个历史版本互相比较。"));
                return;
            }
            int leftIndex = selectedRows.first()->data(Qt::UserRole).toInt();
            int rightIndex = -1;
            if (selectedRows.size() == 2) {
                rightIndex = selectedRows.last()->data(Qt::UserRole).toInt();
                if (leftIndex < rightIndex) std::swap(leftIndex, rightIndex);
            }
            const QString leftContent = items.at(leftIndex).content;
            const QString rightContent = rightIndex >= 0 ? items.at(rightIndex).content : guardedEditor->content();
            const QString leftTitle = items.at(leftIndex).title;
            const QString rightTitle = rightIndex >= 0 ? items.at(rightIndex).title : QStringLiteral("当前内容");
            QDialog compareDialog(this);
            compareDialog.setWindowTitle(QStringLiteral("修订比较 — %1 / %2").arg(leftTitle, rightTitle));
            compareDialog.resize(900, 600);
            auto* layout = new QVBoxLayout(&compareDialog);
            auto* columns = new QHBoxLayout();
            auto* leftText = new QTextEdit(&compareDialog);
            auto* rightText = new QTextEdit(&compareDialog);
            leftText->setReadOnly(true);
            rightText->setReadOnly(true);
            leftText->setPlainText(leftContent);
            rightText->setPlainText(rightContent);
            highlightLineDiff(leftText, rightText, leftContent, rightContent);
            columns->addWidget(leftText);
            columns->addWidget(rightText);
            layout->addLayout(columns);
            auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &compareDialog);
            layout->addWidget(close);
            QObject::connect(close, &QDialogButtonBox::rejected, &compareDialog, &QDialog::reject);
            compareDialog.exec();
        });
        QObject::connect(restore, &QPushButton::clicked, &historyDialog, [this, guardedEditor, revisionList, &items, &historyDialog]() {
            if (!guardedEditor) return;
            const QList<QListWidgetItem*> selectedRows = revisionList->selectedItems();
            if (selectedRows.size() != 1) {
                QMessageBox::information(this, QStringLiteral("恢复修订"), QStringLiteral("请选择一个历史版本恢复。"));
                return;
            }
            const auto& revision = items.at(selectedRows.first()->data(Qt::UserRole).toInt());
            if (QMessageBox::question(this, QStringLiteral("恢复修订"), QStringLiteral("以选中版本替换当前内容？")) != QMessageBox::Yes) return;
            if (!noteManager_->updateNote(guardedEditor->currentNoteId(), revision.title, revision.content)) {
                QMessageBox::warning(this, QStringLiteral("恢复失败"), QStringLiteral("无法保存恢复后的笔记。"));
                return;
            }
            guardedEditor->setContent(revision.content);
            historyDialog.accept();
        });
        historyDialog.exec();
    });
    connect(applyTemplateAction, &QAction::triggered, this, [this]() {
        NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
        if (!editor || encryptedNotesOpen_.contains(editor->currentNoteId())) return;
        const auto items = applicationContext_->localFeatures().templates(); QStringList labels; for (const auto& item : items) labels << item.name;
        bool ok = false; const QString name = QInputDialog::getItem(this, QStringLiteral("模板"), QStringLiteral("选择模板"), labels, 0, false, &ok);
        if (!ok) return;
        for (const auto& item : items) {
            if (item.name != name) continue;
            QMap<QString, QString> values;
            values.insert(QStringLiteral("title"), noteManager_->getNote(editor->currentNoteId()).title);
            values.insert(QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate));
            for (auto variable = item.variables.begin(); variable != item.variables.end(); ++variable) {
                if (values.contains(variable.key())) continue;
                const QJsonObject definition = variable.value().toObject();
                bool accepted = false;
                const QString value = QInputDialog::getText(this, QStringLiteral("模板变量"),
                    QStringLiteral("%1%2").arg(variable.key(), definition.value(QStringLiteral("required")).toBool() ? QStringLiteral("（必填）") : QString()),
                    QLineEdit::Normal, definition.value(QStringLiteral("default")).toString(), &accepted);
                if (!accepted || (definition.value(QStringLiteral("required")).toBool() && value.trimmed().isEmpty())) return;
                values.insert(variable.key(), value);
            }
            editor->setContent(TemplateService::applyVariables(item.content, values));
            break;
        }
    });
    connect(createTemplateAction, &QAction::triggered, this, [this]() {
        NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
        if (!editor || encryptedNotesOpen_.contains(editor->currentNoteId())) return;
        bool ok = false; const QString name = QInputDialog::getText(this, QStringLiteral("新建模板"), QStringLiteral("模板名称"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return; QString error;
        if (!applicationContext_->localFeatures().saveTemplate(name, editor->content(), &error)) QMessageBox::warning(this, QStringLiteral("模板保存失败"), error);
    });
    connect(manageTemplateAction, &QAction::triggered, this, [this]() {
        auto& features = applicationContext_->localFeatures();
        const auto items = features.templates();
        QStringList labels;
        for (const auto& item : items) if (!item.builtIn) labels << item.name;
        if (labels.isEmpty()) { QMessageBox::information(this, QStringLiteral("模板"), QStringLiteral("没有自定义模板")); return; }
        bool ok = false;
        const QString name = QInputDialog::getItem(this, QStringLiteral("管理模板"), QStringLiteral("选择模板"), labels, 0, false, &ok);
        if (!ok) return;
        const auto it = std::find_if(items.cbegin(), items.cend(), [&name](const NoteTemplate& item) { return !item.builtIn && item.name == name; });
        if (it == items.cend()) return;
        const QStringList operations{QStringLiteral("编辑"), QStringLiteral("复制"), QStringLiteral("删除")};
        const QString operation = QInputDialog::getItem(this, QStringLiteral("管理模板"), QStringLiteral("操作"), operations, 0, false, &ok);
        if (!ok) return;
        if (operation == QStringLiteral("删除")) {
            if (QMessageBox::question(this, QStringLiteral("删除模板"), QStringLiteral("确认删除“%1”？").arg(name)) == QMessageBox::Yes)
                features.removeTemplate(it->id);
            return;
        }
        if (operation == QStringLiteral("复制")) {
            const QString copiedName = QInputDialog::getText(this, QStringLiteral("复制模板"), QStringLiteral("新模板名称"), QLineEdit::Normal, name + QStringLiteral(" - 副本"), &ok);
            if (ok) { QString error; if (!features.duplicateTemplate(it->id, copiedName, &error)) QMessageBox::warning(this, QStringLiteral("复制失败"), error); }
            return;
        }
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("编辑模板")); dialog.resize(700, 560);
        auto* layout = new QFormLayout(&dialog);
        auto* nameEdit = new QLineEdit(it->name, &dialog);
        auto* categoryEdit = new QLineEdit(it->category, &dialog);
        auto* descriptionEdit = new QLineEdit(it->description, &dialog);
        auto* contentEdit = new QTextEdit(&dialog); contentEdit->setPlainText(it->content);
        auto* variablesTable = new QTableWidget(&dialog);
        variablesTable->setColumnCount(3);
        variablesTable->setHorizontalHeaderLabels({QStringLiteral("变量名"), QStringLiteral("默认值"), QStringLiteral("必填")});
        variablesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        const auto appendVariableRow = [variablesTable](const QString& key, const QString& defaultValue, bool required) {
            const int row = variablesTable->rowCount();
            variablesTable->insertRow(row);
            variablesTable->setItem(row, 0, new QTableWidgetItem(key));
            variablesTable->setItem(row, 1, new QTableWidgetItem(defaultValue));
            auto* requiredCheck = new QCheckBox(variablesTable);
            requiredCheck->setChecked(required);
            variablesTable->setCellWidget(row, 2, requiredCheck);
        };
        for (auto variable = it->variables.begin(); variable != it->variables.end(); ++variable) {
            const QJsonObject definition = variable.value().toObject();
            appendVariableRow(variable.key(), definition.value(QStringLiteral("default")).toString(),
                              definition.value(QStringLiteral("required")).toBool());
        }
        auto* addVariableButton = new QPushButton(QStringLiteral("添加变量"), &dialog);
        auto* removeVariableButton = new QPushButton(QStringLiteral("删除所选变量"), &dialog);
        auto* variableActions = new QWidget(&dialog);
        auto* variableActionsLayout = new QHBoxLayout(variableActions);
        variableActionsLayout->setContentsMargins(0, 0, 0, 0);
        variableActionsLayout->addWidget(addVariableButton);
        variableActionsLayout->addWidget(removeVariableButton);
        QObject::connect(addVariableButton, &QPushButton::clicked, &dialog, [appendVariableRow]() {
            appendVariableRow(QString(), QString(), false);
        });
        QObject::connect(removeVariableButton, &QPushButton::clicked, &dialog, [variablesTable]() {
            const int row = variablesTable->currentRow();
            if (row >= 0) variablesTable->removeRow(row);
        });
        layout->addRow(QStringLiteral("名称"), nameEdit); layout->addRow(QStringLiteral("分类"), categoryEdit);
        layout->addRow(QStringLiteral("描述"), descriptionEdit); layout->addRow(QStringLiteral("正文"), contentEdit);
        layout->addRow(QStringLiteral("模板变量（{{变量名}}）"), variablesTable);
        layout->addRow(QString(), variableActions);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(buttons); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        QJsonObject parsedVariables;
        QSet<QString> variableNames;
        for (int row = 0; row < variablesTable->rowCount(); ++row) {
            const QString key = variablesTable->item(row, 0) ? variablesTable->item(row, 0)->text().trimmed() : QString();
            const QString defaultValue = variablesTable->item(row, 1) ? variablesTable->item(row, 1)->text() : QString();
            const auto* requiredCheck = qobject_cast<QCheckBox*>(variablesTable->cellWidget(row, 2));
            if (key.isEmpty() || variableNames.contains(key) || key.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")))) {
                QMessageBox::warning(this, QStringLiteral("模板变量无效"), QStringLiteral("变量名必须唯一且仅包含字母、数字、下划线、点或连字符。"));
                return;
            }
            variableNames.insert(key);
            QJsonObject definition;
            definition.insert(QStringLiteral("default"), defaultValue);
            definition.insert(QStringLiteral("required"), requiredCheck && requiredCheck->isChecked());
            parsedVariables.insert(key, definition);
        }
        NoteTemplate updated = *it; updated.name = nameEdit->text().trimmed(); updated.category = categoryEdit->text();
        updated.description = descriptionEdit->text(); updated.content = contentEdit->toPlainText();
        updated.variables = parsedVariables;
        QString error; if (!features.saveTemplate(updated, &error)) QMessageBox::warning(this, QStringLiteral("保存失败"), error);
    });
    connect(importTemplateAction, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入模板"), QString(), QStringLiteral("MyBase 模板 (*.json)"));
        if (path.isEmpty()) return;
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString()); return; }
        QJsonParseError parseError; const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        QString error;
        if (parseError.error != QJsonParseError::NoError || !document.isObject() || !applicationContext_->localFeatures().importTemplate(document.object(), &error))
            QMessageBox::warning(this, QStringLiteral("导入失败"), error.isEmpty() ? parseError.errorString() : error);
    });
    connect(exportTemplateAction, &QAction::triggered, this, [this]() {
        const auto items = applicationContext_->localFeatures().templates(); QStringList labels;
        for (const auto& item : items) labels << item.name;
        bool ok = false; const QString name = QInputDialog::getItem(this, QStringLiteral("导出模板"), QStringLiteral("选择模板"), labels, 0, false, &ok);
        if (!ok) return;
        const auto it = std::find_if(items.cbegin(), items.cend(), [&name](const NoteTemplate& item) { return item.name == name; });
        if (it == items.cend()) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出模板"), name + QStringLiteral(".json"), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        if (QFileInfo::exists(path) && QMessageBox::question(this, QStringLiteral("覆盖"), QStringLiteral("文件已存在，覆盖？")) != QMessageBox::Yes) return;
        QSaveFile file(path);
        const QByteArray bytes = QJsonDocument(applicationContext_->localFeatures().exportTemplate(it->id)).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            const QString error = file.errorString(); file.cancelWriting();
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        }
    });
    connect(newFromTemplateAction, &QAction::triggered, this, [this]() {
        const auto items = applicationContext_->localFeatures().templates(); QStringList labels;
        for (const auto& item : items) labels << item.name;
        bool ok = false; const QString name = QInputDialog::getItem(this, QStringLiteral("从模板新建"), QStringLiteral("选择模板"), labels, 0, false, &ok);
        if (!ok) return;
        const auto it = std::find_if(items.cbegin(), items.cend(), [&name](const NoteTemplate& item) { return item.name == name; });
        if (it == items.cend()) return;
        const QString title = QInputDialog::getText(this, QStringLiteral("新建笔记"), QStringLiteral("笔记标题"), QLineEdit::Normal, name, &ok);
        if (!ok || title.trimmed().isEmpty()) return;
        QMap<QString, QString> values{{QStringLiteral("title"), title}, {QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate)}};
        for (auto variable = it->variables.begin(); variable != it->variables.end(); ++variable) {
            if (values.contains(variable.key())) continue;
            const QJsonObject definition = variable.value().toObject();
            const QString value = QInputDialog::getText(this, QStringLiteral("模板变量"), variable.key(), QLineEdit::Normal, definition.value(QStringLiteral("default")).toString(), &ok);
            if (!ok || (definition.value(QStringLiteral("required")).toBool() && value.trimmed().isEmpty())) return;
            values.insert(variable.key(), value);
        }
        const QString content = TemplateService::applyVariables(it->content, values);
        const qint64 id = noteManager_->createNote(title.trimmed(), content);
        if (id > 0) { loadNotes(); Note note = noteManager_->getNote(id); const int tab = noteTabManager_->openNote(id, note.title, note.content); refreshCompletionWords(noteTabManager_->editorAt(tab)); }
    });

    QMenu* aiMenu = menuBar_->addMenu(QStringLiteral("AI 写作"));
    QAction* configureAi = aiMenu->addAction(QStringLiteral("配置兼容接口…"));
    QWidgetAction* modelAction = new QWidgetAction(aiMenu);
    QWidget* modelWidget = new QWidget(aiMenu);
    auto* modelLayout = new QHBoxLayout(modelWidget);
    modelLayout->setContentsMargins(8, 2, 8, 2);
    modelLayout->addWidget(new QLabel(QStringLiteral("模型"), modelWidget));
    aiModelSelector_ = new QComboBox(modelWidget);
    aiModelSelector_->setMinimumWidth(220);
    modelLayout->addWidget(aiModelSelector_);
    modelAction->setDefaultWidget(modelWidget);
    aiMenu->addAction(modelAction);
    QAction* fetchModels = aiMenu->addAction(QStringLiteral("获取模型列表…"));
    aiMenu->addSeparator();

    QSettings aiSettings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    const QString currentModel = aiSettings.value(QStringLiteral("ai/model"), applicationContext_->ai().model()).toString();
    const QStringList savedModels = aiSettings.value(QStringLiteral("ai/models")).toStringList();
    aiModelSelector_->addItems(savedModels);
    if (!currentModel.trimmed().isEmpty() && aiModelSelector_->findText(currentModel) < 0) aiModelSelector_->addItem(currentModel);
    if (aiModelSelector_->count() > 0) {
        const int currentIndex = qMax(0, aiModelSelector_->findText(currentModel));
        aiModelSelector_->setCurrentIndex(currentIndex);
        applicationContext_->ai().setModel(aiModelSelector_->currentText());
    } else {
        aiModelSelector_->addItem(QStringLiteral("请先获取模型列表"));
    }
    connect(aiModelSelector_, &QComboBox::currentTextChanged, this, [this](const QString& model) {
        if (model.trimmed().isEmpty() || model == QStringLiteral("请先获取模型列表")) return;
        applicationContext_->ai().setModel(model);
        QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
        settings.setValue(QStringLiteral("ai/model"), model);
        settings.sync();
    });
    connect(fetchModels, &QAction::triggered, this, [this]() {
        statusBar_->showMessage(QStringLiteral("正在获取 AI 模型列表…"), 5000);
        applicationContext_->ai().fetchModels();
    });
    connect(&applicationContext_->ai(), &AiWritingService::modelsFetched, this, [this](const QStringList& models) {
        if (!aiModelSelector_) return;
        const QString selected = applicationContext_->ai().model();
        aiModelSelector_->blockSignals(true);
        aiModelSelector_->clear();
        aiModelSelector_->addItems(models);
        const int selectedIndex = aiModelSelector_->findText(selected);
        aiModelSelector_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        aiModelSelector_->blockSignals(false);
        const QString model = aiModelSelector_->currentText();
        applicationContext_->ai().setModel(model);
        QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
        settings.setValue(QStringLiteral("ai/models"), models);
        settings.setValue(QStringLiteral("ai/model"), model);
        settings.sync();
        statusBar_->showMessage(QStringLiteral("已获取 %1 个 AI 模型").arg(models.size()), 5000);
    });
    connect(&applicationContext_->ai(), &AiWritingService::modelsFetchFailed, this, [this](const QString& message) {
        statusBar_->showMessage(message, 7000);
        QMessageBox::warning(this, QStringLiteral("获取模型列表失败"), message);
    });
    QAction* streamingAi = aiMenu->addAction(QStringLiteral("流式显示生成内容"));
    streamingAi->setCheckable(true);
    streamingAi->setChecked(applicationContext_->ai().streamingEnabled());
    connect(streamingAi, &QAction::toggled, this, [this](bool enabled) {
        applicationContext_->ai().setStreamingEnabled(enabled);
        QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
        settings.setValue(QStringLiteral("ai/streaming"), enabled);
        settings.sync();
    });
    QAction* contextLimit = aiMenu->addAction(QStringLiteral("上下文字符上限…"));
    connect(contextLimit, &QAction::triggered, this, [this]() {
        QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
        bool ok = false;
        const int characters = QInputDialog::getInt(this, QStringLiteral("AI 上下文限制"),
            QStringLiteral("最大上下文字符数（不是 token 数）"),
            settings.value(QStringLiteral("ai/maxContextChars"), 32000).toInt(), 1000, 1000000, 1000, &ok);
        if (!ok) return;
        applicationContext_->ai().setMaxContextChars(characters);
        settings.setValue(QStringLiteral("ai/maxContextChars"), characters);
        settings.sync();
    });
    QAction* cancelAi = aiMenu->addAction(QStringLiteral("取消当前请求"));
    aiMenu->addSeparator();
    connect(&applicationContext_->ai(), &AiWritingService::streamChunk, this, [this](const QString& chunk) {
        if (aiStreamPreview_) {
            aiStreamPreview_->moveCursor(QTextCursor::End);
            aiStreamPreview_->insertPlainText(chunk);
        }
    });
    connect(&applicationContext_->ai(), &AiWritingService::streamReset, this, [this]() {
        if (aiStreamPreview_) aiStreamPreview_->clear();
    });
    connect(&applicationContext_->ai(), &AiWritingService::requestStateChanged, this, [this](bool running) {
        if (running || !aiStreamDialog_) return;
        if (aiStreamPreview_) {
            aiStreamPreview_->clear();
            aiStreamPreview_->setPlainText(QStringLiteral("AI 请求已取消或失败。"));
        }
        aiStreamDialog_->close();
        aiStreamDialog_.clear();
        aiStreamPreview_.clear();
    });
    const QStringList aiOperations{QStringLiteral("续写"), QStringLiteral("改写"), QStringLiteral("摘要"), QStringLiteral("生成大纲")};
    for (const QString& operation : aiOperations) {
        QAction* action = aiMenu->addAction(operation);
        connect(action, &QAction::triggered, this, [this, operation]() {
            NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
            if (!editor) return;
            const qint64 noteId = editor->currentNoteId();
            const bool encrypted = encryptedNotesOpen_.contains(noteId);
            const QStringList scopes{QStringLiteral("选中文本"), QStringLiteral("当前段落"), QStringLiteral("整篇笔记")};
            bool scopeAccepted = false;
            const QString scope = QInputDialog::getItem(this, QStringLiteral("AI 内容范围"), QStringLiteral("选择发送到 AI 的内容"), scopes, editor->textCursor().hasSelection() ? 0 : 2, false, &scopeAccepted);
            if (!scopeAccepted) return;
            QTextCursor sourceCursor = editor->textCursor();
            if (scope == scopes.at(0) && !sourceCursor.hasSelection()) {
                QMessageBox::information(this, QStringLiteral("AI 内容范围"), QStringLiteral("请先选中要处理的文本。"));
                return;
            }
            QString source;
            int selectionStart = -1;
            int selectionEnd = -1;
            if (scope == scopes.at(0)) {
                source = sourceCursor.selectedText();
                selectionStart = sourceCursor.selectionStart();
                selectionEnd = sourceCursor.selectionEnd();
            } else if (scope == scopes.at(1)) {
                sourceCursor.select(QTextCursor::BlockUnderCursor);
                source = sourceCursor.selectedText();
                selectionStart = sourceCursor.selectionStart();
                selectionEnd = sourceCursor.selectionEnd();
            } else {
                source = editor->content();
            }
            if (encrypted && QMessageBox::warning(this, QStringLiteral("发送加密笔记"),
                    QStringLiteral("加密笔记正文将发送到所配置的第三方 AI 服务。仅在你明确同意后继续。"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            if (!applicationContext_->ai().configured()) {
                QMessageBox::information(this, QStringLiteral("AI 配置"), QStringLiteral("请先配置接口地址和 API 密钥。"));
                return;
            }
            aiTargetEditor_ = editor;
            aiTargetNoteId_ = noteId;
            aiTargetSelectionStart_ = selectionStart;
            aiTargetSelectionEnd_ = selectionEnd;
            aiTargetDocumentSnapshot_ = editor->content();
            if (applicationContext_->ai().streamingEnabled()) {
                auto* streamDialog = new QDialog(this);
                streamDialog->setAttribute(Qt::WA_DeleteOnClose);
                streamDialog->setWindowTitle(QStringLiteral("AI 正在生成"));
                streamDialog->resize(680, 420);
                auto* streamLayout = new QVBoxLayout(streamDialog);
                aiStreamPreview_ = new QTextEdit(streamDialog);
                aiStreamPreview_->setReadOnly(true);
                streamLayout->addWidget(aiStreamPreview_);
                auto* streamButtons = new QDialogButtonBox(streamDialog);
                QPushButton* cancelStream = streamButtons->addButton(QStringLiteral("取消生成"), QDialogButtonBox::RejectRole);
                streamLayout->addWidget(streamButtons);
                connect(cancelStream, &QPushButton::clicked, this, [this, streamDialog]() {
                    applicationContext_->ai().cancel();
                    streamDialog->close();
                });
                aiStreamDialog_ = streamDialog;
                streamDialog->show();
            }
            if (!applicationContext_->ai().request(operation, source)) {
                aiTargetEditor_.clear();
                aiTargetNoteId_ = -1;
                aiTargetSelectionStart_ = aiTargetSelectionEnd_ = -1;
                aiTargetDocumentSnapshot_.clear();
                statusBar_->showMessage(QStringLiteral("AI 请求未启动"));
            }
        });
    }
    connect(configureAi, &QAction::triggered, this, [this]() {
        QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
        bool ok = false;
        const QString endpoint = QInputDialog::getText(this, QStringLiteral("AI 接口"), QStringLiteral("第一步：兼容 Chat Completions 的 HTTPS 请求 URL（例如 /v1/chat/completions）"), QLineEdit::Normal, settings.value(QStringLiteral("ai/endpoint")).toString(), &ok);
        const QUrl endpointUrl(endpoint.trimmed());
        if (!ok || endpointUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || !endpointUrl.isValid() || endpointUrl.host().isEmpty()) {
            if (ok) QMessageBox::warning(this, QStringLiteral("AI 配置"), QStringLiteral("请输入包含主机名的有效 HTTPS 接口地址。"));
            return;
        }
        const QString key = QInputDialog::getText(this, QStringLiteral("AI 密钥"), QStringLiteral("第二步：API Key（使用 Windows 凭据管理器安全保存）"), QLineEdit::Password, QString(), &ok);
        if (!ok || key.isEmpty()) return;
        if (!AiCredentialStore::saveApiKey(key)) {
            QMessageBox::warning(this, QStringLiteral("AI 配置"), QStringLiteral("无法使用系统凭据库保存密钥；密钥未写入普通设置。"));
            return;
        }
        const QString previousEndpoint = settings.value(QStringLiteral("ai/endpoint")).toString();
        if (previousEndpoint != endpoint.trimmed()) {
            settings.remove(QStringLiteral("ai/models"));
            settings.remove(QStringLiteral("ai/model"));
            if (aiModelSelector_) {
                aiModelSelector_->blockSignals(true);
                aiModelSelector_->clear();
                aiModelSelector_->addItem(QStringLiteral("请先获取模型列表"));
                aiModelSelector_->setCurrentIndex(0);
                aiModelSelector_->blockSignals(false);
            }
            applicationContext_->ai().setModel(QString());
        }
        settings.setValue(QStringLiteral("ai/endpoint"), endpoint.trimmed());
        settings.sync();
        applicationContext_->ai().setEndpoint(endpointUrl);
        if (QMessageBox::question(this, QStringLiteral("获取模型列表"), QStringLiteral("第三步：是否立即从该接口获取可用模型列表？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
            statusBar_->showMessage(QStringLiteral("正在获取 AI 模型列表…"), 5000);
            applicationContext_->ai().fetchModels();
        } else {
            QMessageBox::information(this, QStringLiteral("AI 配置"), QStringLiteral("接口和密钥已保存；请从 AI 写作菜单的模型下拉框选择模型，或稍后获取模型列表。"));
        }
    });
    connect(cancelAi, &QAction::triggered, this, [this]() {
        applicationContext_->ai().cancel();
        aiTargetEditor_.clear();
        aiTargetNoteId_ = -1;
        aiTargetSelectionStart_ = aiTargetSelectionEnd_ = -1;
        aiTargetDocumentSnapshot_.clear();
    });
    connect(&applicationContext_->ai(), &AiWritingService::completed, this, [this](const QString& result) {
        QPointer<NoteEditor> editor = aiTargetEditor_;
        if (!editor || editor->currentNoteId() != aiTargetNoteId_) {
            aiTargetEditor_.clear();
            aiTargetNoteId_ = -1;
            aiTargetSelectionStart_ = aiTargetSelectionEnd_ = -1;
            aiTargetDocumentSnapshot_.clear();
            return;
        }
        const bool encryptedTarget = encryptedNotesOpen_.contains(aiTargetNoteId_);
        if (aiStreamDialog_) aiStreamDialog_->close();
        aiStreamDialog_.clear();
        aiStreamPreview_.clear();
        aiTargetEditor_.clear();
        aiTargetNoteId_ = -1;
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("AI 结果预览"));
        dialog.resize(800, 600);
        auto* preview = new QTextEdit(&dialog);
        preview->setReadOnly(true);
        preview->setPlainText(result);
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(encryptedTarget
            ? QStringLiteral("加密笔记的 AI 结果仅供查看；当前版本不会将解密内容重新写回加密存储。")
            : QStringLiteral("检查结果后选择应用方式；编辑器中的文本变更可撤销。"), &dialog));
        layout->addWidget(preview);
        auto* buttons = new QDialogButtonBox(&dialog);
        QPushButton* replaceButton = nullptr;
        QPushButton* appendButton = nullptr;
        QPushButton* copyButton = nullptr;
        if (!encryptedTarget) {
            replaceButton = buttons->addButton(QStringLiteral("替换目标文本"), QDialogButtonBox::AcceptRole);
            appendButton = buttons->addButton(QStringLiteral("追加到文末"), QDialogButtonBox::ActionRole);
            copyButton = buttons->addButton(QStringLiteral("复制结果"), QDialogButtonBox::ActionRole);
        }
        buttons->addButton(QDialogButtonBox::Close);
        layout->addWidget(buttons);
        int applyMode = 0;
        if (replaceButton) QObject::connect(replaceButton, &QPushButton::clicked, &dialog, [&]() { applyMode = 1; dialog.accept(); });
        if (appendButton) QObject::connect(appendButton, &QPushButton::clicked, &dialog, [&]() { applyMode = 2; dialog.accept(); });
        if (copyButton) QObject::connect(copyButton, &QPushButton::clicked, &dialog, [&]() { QApplication::clipboard()->setText(result); statusBar_->showMessage(QStringLiteral("AI 结果已复制"), 3000); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
        if (editor && applyMode == 1) {
            if (editor->content() != aiTargetDocumentSnapshot_) {
                QMessageBox::warning(this, QStringLiteral("AI 结果未应用"), QStringLiteral("请求期间笔记内容已更改。为避免覆盖新编辑，请重新发起 AI 请求。"));
            } else {
                editor->applyAiResult(result, aiTargetSelectionStart_, aiTargetSelectionEnd_);
            }
        } else if (editor && applyMode == 2) {
            if (editor->content() != aiTargetDocumentSnapshot_) {
                QMessageBox::warning(this, QStringLiteral("AI 结果未应用"), QStringLiteral("请求期间笔记内容已更改，未追加旧上下文生成的结果。"));
            } else {
                editor->applyAiResult(result, -1, -1, true);
            }
        }
        aiTargetSelectionStart_ = aiTargetSelectionEnd_ = -1;
        aiTargetDocumentSnapshot_.clear();
    });
    connect(&applicationContext_->ai(), &AiWritingService::failed, this, [this](const QString& message) {
        if (aiStreamDialog_) aiStreamDialog_->close();
        aiStreamDialog_.clear();
        aiStreamPreview_.clear();
        aiTargetEditor_.clear();
        aiTargetNoteId_ = -1;
        aiTargetSelectionStart_ = aiTargetSelectionEnd_ = -1;
        aiTargetDocumentSnapshot_.clear();
        QMessageBox::warning(this, QStringLiteral("AI 请求失败"), message);
    });

    QMenu* helpMenu = menuBar_->addMenu("帮助(&H)");
    actionAbout_ = helpMenu->addAction("关于(&A)");
}

void MainWindow::setupToolBar() {
    toolBar_ = addToolBar("Main Toolbar");
    toolBar_->setMovable(false);
    toolBar_->addAction(actionNewNote_);
    toolBar_->addAction(actionDeleteNote_);
    toolBar_->addSeparator();
    toolBar_->addAction(actionEncryptNote_);
    toolBar_->addAction(actionOpenEncryptedNotes_);
    toolBar_->addSeparator();
    toolBar_->addAction(actionOpenTrash_);
}

void MainWindow::setupStatusBar() {
    statusBar_ = statusBar();
    statusBar_->showMessage("就绪");
}

void MainWindow::setupConnections() {
    connect(actionNewNote_, &QAction::triggered, this, &MainWindow::onNewNote);
    connect(actionDeleteNote_, &QAction::triggered, this, &MainWindow::onDeleteNote);
    connect(actionEncryptNote_, &QAction::triggered, this, &MainWindow::onEncryptNote);
    connect(actionOpenEncryptedNotes_, &QAction::triggered, this, &MainWindow::onOpenEncryptedNotes);
    connect(actionOpenTrash_, &QAction::triggered, this, &MainWindow::onOpenTrash);
    connect(actionExit_, &QAction::triggered, this, &QMainWindow::close);
    connect(actionAbout_, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于 MyBase"),
                           QStringLiteral("MyBase - Markdown 笔记\n版本 %1\n\n简约而不简单")
                               .arg(QCoreApplication::applicationVersion()));
    });

    connect(searchLineEdit_, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(noteListView_, &QListView::clicked, this, &MainWindow::onNoteSelected);
    connect(tagTreeView_, &TagTreeView::tagSelectionChanged,
            this, &MainWindow::onTagSelectionChanged);
    connect(tagMatchModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::runSearch);
    connect(outlineView_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!noteTabManager_ || !index.isValid()) {
            return;
        }
        NoteEditor* editor = noteTabManager_->currentEditor();
        if (editor) {
            editor->scrollToPosition(index.data(MarkdownOutlineModel::PositionRole).toInt());
        }
    });

    if (noteTabManager_) {
        connect(noteTabManager_, &NoteTabManager::currentTabChanged, this, &MainWindow::onCurrentTabChanged);
        connect(noteTabManager_, &NoteTabManager::tabContentChanged, this, &MainWindow::onTabContentChanged);
        connect(noteTabManager_, &NoteTabManager::tabClosed, this, [this](qint64 noteId) {
            if (collaborationClient_ && noteId == collaborationNoteId_) {
                collaborationClient_->leave(); collaborationClient_ = nullptr; collaborationNoteId_ = -1;
                QObject::disconnect(collaborationCursorConnection_);
                statusBar_->showMessage(QStringLiteral("协作笔记标签页已关闭，协作已断开"), 5000);
            }
        });
        }
}

void MainWindow::loadNotes() {
    if (noteListModel_) {
        noteListModel_->refresh();
        statusBar_->showMessage(QString("Loaded %1 notes").arg(noteListModel_->rowCount()));
    }
}

void MainWindow::onNewNote() {
    if (!noteManager_) {
        return;
    }

    bool ok;
    QString title = QInputDialog::getText(this, "New Note", "Note title:", QLineEdit::Normal, "Untitled", &ok);
    if (ok && !title.isEmpty()) {
        qint64 noteId = noteManager_->createNote(title, "");
        if (noteId > 0) {
            statusBar_->showMessage("Note created");
        } else {
            QMessageBox::warning(this, "Error", "Failed to create note");
        }
    }
}

void MainWindow::onDeleteNote() {
    if (!noteManager_) {
        return;
    }

    QModelIndex index = noteListView_->currentIndex();
    if (!index.isValid()) {
        QMessageBox::information(this, "提示", "请先选择一篇要删除的笔记");
        return;
    }

    qint64 noteId = index.data(Qt::UserRole).toLongLong();
    if (noteId <= 0) {
        QMessageBox::information(this, "提示", "无法获取笔记 ID");
        return;
    }

    Note note = noteManager_->getNote(noteId);
    QString confirmTitle = note.id != -1 ? note.title : "此笔记";

    QMessageBox::StandardButton reply = QMessageBox::question(this, "删除笔记",
        QString("确定要将 \"%1\" 移到回收站吗？").arg(confirmTitle),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (noteManager_->moveToTrash(noteId)) {
            statusBar_->showMessage("笔记已移到回收站");
        } else {
            QMessageBox::warning(this, "错误", "删除笔记失败");
        }
    }
}

void MainWindow::onOpenTrash() {
    if (!noteManager_) {
        return;
    }

    TrashDialog* trashDialog = new TrashDialog(noteManager_, this);
    trashDialog->exec();
}

void MainWindow::onEncryptNote() {
    if (!noteManager_) {
        return;
    }

    QModelIndex index = noteListView_->currentIndex();
    if (!index.isValid()) {
        QMessageBox::information(this, "提示", "请先选择一篇要加密的笔记");
        return;
    }

    qint64 noteId = index.data(Qt::UserRole).toLongLong();
    if (noteId <= 0) {
        QMessageBox::information(this, "提示", "无法获取笔记 ID");
        return;
    }

    Note note = noteManager_->getNote(noteId);
    if (note.id == -1) {
        QMessageBox::warning(this, "错误", "无法获取笔记信息");
        return;
    }

    if (note.isEncrypted) {
        QMessageBox::information(this, "提示", "此笔记已经加密");
        return;
    }

    PasswordDialog dialog(PasswordDialog::Encrypt, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString password = dialog.getPassword();
        if (password.isEmpty()) {
            QMessageBox::warning(this, "错误", "密码不能为空");
            return;
        }

        if (noteManager_->encryptNote(noteId, password)) {
            if (noteTabManager_) {
                noteTabManager_->closeNote(noteId);
            }
            statusBar_->showMessage("笔记已加密，已移至加密笔记列表");
        } else {
            QMessageBox::warning(this, "错误", "加密笔记失败");
        }
    }
}

void MainWindow::onOpenEncryptedNotes() {
    if (!noteManager_) {
        return;
    }

    EncryptedNotesDialog* dialog = new EncryptedNotesDialog(noteManager_, this);
    dialog->exec();
}

void MainWindow::onSearchTextChanged(const QString& text) {
    Q_UNUSED(text);
    runSearch();
}

void MainWindow::onTagSelectionChanged(const QVector<qint64>& tagIds) {
    selectedTagIds_.clear();
    for (qint64 tagId : tagIds) {
        if (tagId > 0) {
            selectedTagIds_.insert(tagId);
        }
    }
    runSearch();
}

void MainWindow::runSearch() {
    const QString query = searchLineEdit_ ? searchLineEdit_->text().trimmed() : QString();
    if (!searchEngine_) {
        return;
    }
    if (query.isEmpty() && selectedTagIds_.isEmpty()) {
        ++searchRequestId_;
        if (noteListModel_) {
            noteListView_->setModel(noteListModel_);
            noteListModel_->refresh();
        }
        return;
    }

    const quint64 requestId = ++searchRequestId_;
    for (auto it = activeSearchFutures_.begin(); it != activeSearchFutures_.end();) {
        if (it->isFinished()) {
            it = activeSearchFutures_.erase(it);
        } else {
            ++it;
        }
    }

    const TagMatchMode matchMode = static_cast<TagMatchMode>(
        tagMatchModeCombo_->currentData().toInt());
    QFuture<QVector<SearchResult>> future = searchEngine_->combinedSearchAsync(
        query, selectedTagIds_, matchMode);
    activeSearchFutures_.append(future);

    auto* watcher = new QFutureWatcher<QVector<SearchResult>>(this);
    connect(watcher, &QFutureWatcher<QVector<SearchResult>>::finished, this, [this, watcher, requestId]() {
        const QVector<SearchResult> results = watcher->result();
        watcher->deleteLater();
        if (requestId != searchRequestId_) {
            return;
        }
        searchResultModel_->setResults(results);
        noteListView_->setModel(searchResultModel_);
        statusBar_->showMessage(QStringLiteral("找到 %1 条笔记").arg(results.size()));
    });
    watcher->setFuture(future);
}

void MainWindow::onNoteSelected(const QModelIndex& index) {
    if (!noteManager_ || !noteTabManager_) {
        return;
    }

    qint64 noteId = 0;
    if (noteListView_->model() == searchResultModel_) {
        noteId = index.data(SearchResultModel::NoteIdRole).toLongLong();
    } else {
        noteId = index.data(Qt::UserRole).toLongLong();
    }
    
    Note note = noteManager_->getNote(noteId);

    if (note.id != -1) {
        QString displayContent = note.content;
        
        if (note.isEncrypted) {
            PasswordDialog dialog(PasswordDialog::Decrypt, this);
            dialog.setConfirmMode(false);
            if (dialog.exec() == QDialog::Accepted) {
                QString password = dialog.getPassword();
                QByteArray decryptedContent = noteManager_->decryptNoteContent(noteId, password);
                if (decryptedContent.isEmpty()) {
                    QMessageBox::warning(this, "错误", "解密失败，请检查密码是否正确");
                    return;
                }
                displayContent = QString::fromUtf8(decryptedContent);
            } else {
                return;
            }
        }
        
        const int tabIndex = noteTabManager_->openNote(note.id, note.title, displayContent);
        refreshCompletionWords(noteTabManager_->editorAt(tabIndex));
        if (note.isEncrypted) {
            encryptedNotesOpen_.insert(note.id);
        } else {
            encryptedNotesOpen_.remove(note.id);
        }
        setWindowTitle(QString("MyBase - %1").arg(note.title));
        
        refreshOutline(displayContent);
    }
}

void MainWindow::refreshCompletionWords(NoteEditor* editor) {
    if (!editor || !noteManager_ || !tagManager_) return;

    QStringList tagNames;
    for (const Tag& tag : tagManager_->allTags()) tagNames.append(tag.name);
    editor->setCompletionWords(tagNames);

    QStringList noteTitles;
    for (const Note& note : noteManager_->getAllNotes()) {
        if (!note.isEncrypted && !note.isDeleted && !note.title.trimmed().isEmpty()) {
            noteTitles.append(note.title);
        }
    }
    editor->setNoteTitleCompletionWords(noteTitles);
}

void MainWindow::onCurrentTabChanged(int index) {
    if (!pendingNoteContent_.isEmpty()) flushPendingSaves();
    if (!noteTabManager_ || !tagManager_) {
        return;
    }

    NoteEditor* editor = noteTabManager_->editorAt(index);
    refreshCompletionWords(editor);
    if (editor && editor->currentNoteId() > 0) {
        qint64 noteId = editor->currentNoteId();
        Note note = noteManager_->getNote(noteId);
        if (note.id != -1) {
            setWindowTitle(QString("MyBase - %1").arg(note.title));
            refreshOutline(editor->content());
        }
    }
}

void MainWindow::startCollaboration() {
    NoteEditor* editor = noteTabManager_ ? noteTabManager_->currentEditor() : nullptr;
    if (!editor || editor->currentNoteId() <= 0) {
        QMessageBox::information(this, QStringLiteral("实时协作"), QStringLiteral("请先打开一篇笔记。"));
        return;
    }
    const qint64 noteId = editor->currentNoteId();
    const Note note = noteManager_->getNote(noteId);
    if (note.isEncrypted || encryptedNotesOpen_.contains(noteId)) {
        QMessageBox::warning(this, QStringLiteral("实时协作受限"), QStringLiteral("加密笔记禁止参与实时协作。"));
        return;
    }
    if (collaborationClient_) {
        QMessageBox::information(this, QStringLiteral("实时协作"), QStringLiteral("请先断开当前协作会话。"));
        return;
    }
    const QString address = QInputDialog::getText(this, QStringLiteral("实时协作"), QStringLiteral("WebSocket 服务地址（ws:// 或 wss://）"), QLineEdit::Normal, QStringLiteral("ws://127.0.0.1:8765"));
    if (address.isEmpty()) return;
    const QString room = QInputDialog::getText(this, QStringLiteral("实时协作"), QStringLiteral("房间 ID"));
    if (room.isEmpty()) return;
    const QString token = QInputDialog::getText(this, QStringLiteral("实时协作"), QStringLiteral("一次性/可撤销邀请令牌"), QLineEdit::Password);
    if (token.isEmpty()) return;
    const QString name = QInputDialog::getText(this, QStringLiteral("实时协作"), QStringLiteral("显示名称"), QLineEdit::Normal, QCoreApplication::applicationName());
    if (name.isEmpty()) return;
    const auto confirm = QMessageBox::warning(this, QStringLiteral("确认同步笔记内容"),
        QStringLiteral("笔记内容及后续编辑将同步至配置的协作服务器，并由服务端持久化。请确认该服务器可信。加密笔记不允许协作。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;
    auto* client = &applicationContext_->collaboration();
    if (!collaborationSignalsConnected_) {
    connect(client, &CollaborationClient::statusMessage, this, [this](const QString& message) { statusBar_->showMessage(message, 7000); });
    connect(client, &CollaborationClient::connected, this, [this] { statusBar_->showMessage(QStringLiteral("实时协作已连接")); });
    connect(client, &CollaborationClient::disconnected, this, [this] {
        statusBar_->showMessage(QStringLiteral("协作连接已断开，正在等待重连"));
        if (noteTabManager_ && collaborationNoteId_ > 0) {
            if (NoteEditor* target = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(collaborationNoteId_))) {
                target->setReadOnly(false);
                target->clearRemoteSelections();
            }
        }
        remoteCollaborationPeers_.clear();
    });
    connect(client, &CollaborationClient::roleChanged, this, [this](bool readOnly) {
        if (noteTabManager_) if (NoteEditor* target = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(collaborationNoteId_))) target->setReadOnly(readOnly);
    });
    connect(client, &CollaborationClient::membersChanged, this, [this](const QStringList& members) {
        QSet<QString> current;
        for (const QString& member : members) current.insert(member);
        if (noteTabManager_) if (NoteEditor* target = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(collaborationNoteId_))) {
            for (const QString& peer : remoteCollaborationPeers_) if (!current.contains(peer)) target->removeRemoteSelection(peer);
        }
        remoteCollaborationPeers_ = current;
        statusBar_->showMessage(QStringLiteral("协作在线成员：%1").arg(members.size()), 5000);
    });
    connect(client, &CollaborationClient::remoteCursor, this, [this](const QString& peer, const QString& name, int anchor, int position) {
        if (noteTabManager_) if (NoteEditor* target = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(collaborationNoteId_))) target->setRemoteSelection(peer, name, anchor, position);
    });
    connect(client, &CollaborationClient::markdownChanged, this, [this](const QString& markdown) {
        if (!noteTabManager_) return;
        NoteEditor* target = noteTabManager_->editorAt(noteTabManager_->findTabByNoteId(collaborationNoteId_));
        if (!target || target->content() == markdown) return;
        applyingRemoteCollaborationEdit_ = true;
        target->setContent(markdown);
        applyingRemoteCollaborationEdit_ = false;
    });
    collaborationSignalsConnected_ = true;
    }
    collaborationNoteId_ = noteId;
    collaborationClient_ = client;
    QString error;
    if (!client->join(QUrl(address), room, token, name, editor->content(), note.isEncrypted, true, &error)) {
        collaborationClient_ = nullptr;
        collaborationNoteId_ = -1;
        QMessageBox::warning(this, QStringLiteral("无法加入协作"), error);
        return;
    }
    QObject::disconnect(collaborationCursorConnection_);
    collaborationCursorConnection_ = connect(editor, &NoteEditor::cursorSelectionChanged, client, [client](int anchor, int position) { client->sendCursor(anchor, position); });
}

void MainWindow::onTabContentChanged(qint64 noteId, const QString& content) {
    if (collaborationClient_ && noteId == collaborationNoteId_ && !applyingRemoteCollaborationEdit_) {
        QString collaborationError;
        if (!collaborationClient_->setLocalMarkdown(content, &collaborationError)) statusBar_->showMessage(collaborationError, 5000);
    }
    if (!noteManager_ || noteId <= 0) {
        return;
    }
    if (encryptedNotesOpen_.contains(noteId)) {
        statusBar_->showMessage(QStringLiteral("加密笔记仅在内存中编辑；请在加密笔记管理中显式保存"));
        return;
    }
    pendingNoteContent_.insert(noteId, content);
    if (autoSaveTimer_) {
        autoSaveTimer_->start();
    }
    statusBar_->showMessage(QStringLiteral("有未保存的更改…"));
    if (tagManager_ && noteTabManager_) {
        NoteEditor* editor = noteTabManager_->currentEditor();
        if (editor && editor->currentNoteId() == noteId) {
            refreshOutline(content);
        }
    }
}

void MainWindow::onHeadingCreated(const QString& headingText) {
    if (!tagManager_ || !noteTabManager_) {
        return;
    }

    NoteEditor* editor = noteTabManager_->currentEditor();
    if (!editor) {
        return;
    }

    qint64 noteId = editor->currentNoteId();
    if (noteId <= 0) {
        return;
    }

    // 移除笔记的所有现有标签
    QVector<Tag> currentTags = tagManager_->getNoteTags(noteId);
    for (const Tag& tag : currentTags) {
        tagManager_->removeTagFromNote(noteId, tag.id);
    }

    // 检查标题是否已作为标签存在
    qint64 tagId = tagManager_->findTagIdByName(headingText);
    if (tagId == -1) {
        // 创建新标签
        tagId = tagManager_->createTag(headingText);
    }

    if (tagId > 0) {
        // 将标签添加到笔记
        tagManager_->addTagToNote(noteId, tagId);
        
    }
}

void MainWindow::onTagSelected(const QModelIndex& index) {
    if (!tagManager_ || !noteManager_) {
        return;
    }

    qint64 tagId = index.data(Qt::UserRole + 1).toLongLong();
    QVector<qint64> noteIds = tagManager_->filterNotesByTag(tagId);

    statusBar_->showMessage(QString("Found %1 notes with selected tag").arg(noteIds.size()));
}

void MainWindow::onSearchResultsReceived(const QList<SearchResult>& results) {
    statusBar_->showMessage(QString("Found %1 search results").arg(results.size()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    ++searchRequestId_;
    for (QFuture<QVector<SearchResult>>& future : activeSearchFutures_) {
        future.waitForFinished();
    }
    const bool saved = flushPendingSaves();
    if (!saved) {
        const auto choice = QMessageBox::warning(
            this, QStringLiteral("保存失败"),
            QStringLiteral("部分更改未能保存。取消关闭以保留当前会话，或选择“放弃更改”继续退出。"),
            QMessageBox::Cancel | QMessageBox::Discard, QMessageBox::Cancel);
        if (choice != QMessageBox::Discard) {
            event->ignore();
            return;
        }
        pendingNoteContent_.clear();
    }
    saveSessionState();
    saveWindowState();
    QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    settings.setValue(QStringLiteral("session/unclean"), false);
    settings.sync();
    event->accept();
}

void MainWindow::saveWindowState() {
    QSettings settings("MyBase", "MyBase");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("splitterState", mainSplitter_->saveState());
    settings.sync();
}

void MainWindow::restoreWindowState() {
    QSettings settings("MyBase", "MyBase");
    QByteArray geometry = settings.value("geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    QByteArray windowState = settings.value("windowState").toByteArray();
    if (!windowState.isEmpty()) {
        restoreState(windowState);
    }

    QByteArray splitterState = settings.value("splitterState").toByteArray();
    if (!splitterState.isEmpty() && mainSplitter_) {
        mainSplitter_->restoreState(splitterState);
    }
}

void MainWindow::refreshOutline(const QString& markdown) {
    if (!outlineModel_) {
        return;
    }
    outlineModel_->setMarkdown(markdown);
    if (outlineView_) {
        outlineView_->expandAll();
    }
}

bool MainWindow::flushPendingSaves() {
    if (autoSaveTimer_) {
        autoSaveTimer_->stop();
    }
    bool success = true;
    const auto pending = pendingNoteContent_;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        Note note = noteManager_ ? noteManager_->getNote(it.key()) : Note();
        if (encryptedNotesOpen_.contains(it.key())) {
            pendingNoteContent_.remove(it.key());
            continue;
        }
        if (note.id == -1 || (note.content != it.value() &&
                              !noteManager_->updateNote(it.key(), note.title, it.value()))) {
            success = false;
            continue;
        }
        if (pendingNoteContent_.value(it.key()) == it.value()) {
            pendingNoteContent_.remove(it.key());
        }
    }
    if (success && pendingNoteContent_.isEmpty() && statusBar_) {
        statusBar_->showMessage(QStringLiteral("已保存"));
    } else if (!success && statusBar_) {
        statusBar_->showMessage(QStringLiteral("保存失败：更改仍保留在当前会话中"));
    }
    return success;
}

void MainWindow::saveSessionState() {
    QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    settings.setValue(QStringLiteral("session/query"), searchLineEdit_->text());
    settings.setValue(QStringLiteral("session/tagMatchMode"), tagMatchModeCombo_->currentIndex());
    settings.beginWriteArray(QStringLiteral("session/selectedTags"));
    int tagIndex = 0;
    for (qint64 tagId : selectedTagIds_) {
        settings.setArrayIndex(tagIndex++);
        settings.setValue(QStringLiteral("id"), tagId);
    }
    settings.endArray();
    settings.beginWriteArray(QStringLiteral("session/openNotes"));
    int savedIndex = 0;
    qint64 activeNoteId = -1;
    for (int i = 0; i < noteTabManager_->tabCount(); ++i) {
        const qint64 noteId = noteTabManager_->noteIdAt(i);
        if (noteId <= 0 || encryptedNotesOpen_.contains(noteId)) continue;
        NoteEditor* editor = noteTabManager_->editorAt(i);
        if (!editor) continue;
        settings.setArrayIndex(savedIndex++);
        settings.setValue(QStringLiteral("id"), noteId);
        settings.setValue(QStringLiteral("cursor"), editor->cursorPosition());
        settings.setValue(QStringLiteral("scroll"), editor->verticalScrollPosition());
        settings.setValue(QStringLiteral("selectionStart"), editor->selectionStart());
        settings.setValue(QStringLiteral("selectionEnd"), editor->selectionEnd());
        settings.setValue(QStringLiteral("mode"), editor->editorModeValue());
        if (i == noteTabManager_->currentIndex()) activeNoteId = noteId;
    }
    settings.endArray();
    settings.setValue(QStringLiteral("session/activeNote"), activeNoteId);
    settings.beginWriteArray(QStringLiteral("session/pendingDrafts"));
    int draftIndex = 0;
    for (auto it = pendingNoteContent_.cbegin(); it != pendingNoteContent_.cend(); ++it) {
        if (encryptedNotesOpen_.contains(it.key())) continue;
        const Note note = noteManager_->getNote(it.key());
        if (note.id <= 0 || note.isEncrypted || note.isDeleted) continue;
        settings.setArrayIndex(draftIndex++);
        settings.remove(QStringLiteral("content"));
        settings.remove(QStringLiteral("contentProtected"));
        settings.setValue(QStringLiteral("id"), it.key());
        QString protectionError;
        const QByteArray protectedDraft = SessionState::protectDraft(it.value(), &protectionError);
        if (!protectedDraft.isEmpty()) {
            settings.setValue(QStringLiteral("contentProtected"), protectedDraft);
        } else if (statusBar_) {
            statusBar_->showMessage(QStringLiteral("未保存草稿无法安全持久化：%1").arg(protectionError), 8000);
        }
    }
    settings.endArray();
    settings.sync();
}

void MainWindow::restoreSessionState() {
    QSettings settings(QStringLiteral("MyBase"), QStringLiteral("MyBase"));
    const bool recoverUncleanSession = settings.value(QStringLiteral("session/unclean"), false).toBool();
    if (recoverUncleanSession) {
        const auto choice = QMessageBox::question(this, QStringLiteral("恢复上次会话"),
            QStringLiteral("检测到上次 MyBase 未正常退出。是否恢复上次打开的笔记、位置、筛选及未保存草稿？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice == QMessageBox::No) {
            settings.remove(QStringLiteral("session/openNotes"));
            settings.remove(QStringLiteral("session/pendingDrafts"));
            settings.remove(QStringLiteral("session/activeNote"));
            settings.remove(QStringLiteral("session/query"));
            settings.remove(QStringLiteral("session/selectedTags"));
            settings.remove(QStringLiteral("session/tagMatchMode"));
        }
    }
    searchLineEdit_->setText(settings.value(QStringLiteral("session/query")).toString());
    tagMatchModeCombo_->setCurrentIndex(settings.value(QStringLiteral("session/tagMatchMode"), 0).toInt());
    const int selectedTagCount = settings.beginReadArray(QStringLiteral("session/selectedTags"));
    QSet<qint64> restoredTagIds;
    for (int i = 0; i < selectedTagCount; ++i) {
        settings.setArrayIndex(i);
        restoredTagIds.insert(settings.value(QStringLiteral("id"), -1).toLongLong());
    }
    settings.endArray();
    std::function<void(const QModelIndex&)> restoreTagSelection = [&](const QModelIndex& parent) {
        QAbstractItemModel* model = tagTreeView_->model();
        for (int row = 0; model && row < model->rowCount(parent); ++row) {
            const QModelIndex index = model->index(row, 0, parent);
            const qint64 tagId = index.data(Qt::UserRole + 1).toLongLong();
            if (restoredTagIds.contains(tagId)) {
                tagTreeView_->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            }
            restoreTagSelection(index);
        }
    };
    restoreTagSelection(QModelIndex());
    QMap<qint64, QString> recoveryDrafts;
    if (recoverUncleanSession) {
        const int draftCount = settings.beginReadArray(QStringLiteral("session/pendingDrafts"));
        for (int i = 0; i < draftCount; ++i) {
            settings.setArrayIndex(i);
            const qint64 noteId = settings.value(QStringLiteral("id"), -1).toLongLong();
            bool draftOk = false;
            const QString content = SessionState::unprotectDraft(settings.value(QStringLiteral("contentProtected")).toByteArray(), &draftOk);
            if (noteId > 0 && draftOk) recoveryDrafts.insert(noteId, content);
        }
        settings.endArray();
    }
    const qint64 activeNoteId = settings.value(QStringLiteral("session/activeNote"), -1).toLongLong();
    const int count = settings.beginReadArray(QStringLiteral("session/openNotes"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const qint64 noteId = settings.value(QStringLiteral("id"), -1).toLongLong();
        Note note = noteManager_->getNote(noteId);
        if (note.id <= 0 || note.isDeleted || note.isEncrypted) continue;
        const int tabIndex = noteTabManager_->openNote(note.id, note.title, note.content);
        if (NoteEditor* editor = noteTabManager_->editorAt(tabIndex)) {
            if (recoveryDrafts.contains(note.id)) {
                editor->setContent(recoveryDrafts.value(note.id));
                pendingNoteContent_.insert(note.id, recoveryDrafts.value(note.id));
            }
            editor->restoreNavigationState(settings.value(QStringLiteral("cursor")).toInt(),
                settings.value(QStringLiteral("scroll")).toInt(),
                settings.value(QStringLiteral("selectionStart"), -1).toInt(),
                settings.value(QStringLiteral("selectionEnd"), -1).toInt(),
                settings.value(QStringLiteral("mode"), 0).toInt());
        }
        if (note.id == activeNoteId) noteTabManager_->setCurrentIndex(tabIndex);
    }
    settings.endArray();
    if (!pendingNoteContent_.isEmpty() && autoSaveTimer_) autoSaveTimer_->start();
    runSearch();
}

