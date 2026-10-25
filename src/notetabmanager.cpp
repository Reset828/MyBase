#include "notetabmanager.h"
#include <QMessageBox>

NoteTabManager::NoteTabManager(QWidget* parent)
    : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    setupConnections();
}

int NoteTabManager::openNote(qint64 noteId, const QString& title, const QString& content) {
    int existingIndex = findTabByNoteId(noteId);
    if (existingIndex >= 0) {
        setCurrentIndex(existingIndex);
        return existingIndex;
    }

    NoteEditor* editor = new NoteEditor(this);
    editor->setNoteId(noteId);
    editor->setContent(content);

    int index = addTab(editor, title);
    setCurrentIndex(index);

    tabIndexToNoteId_[index] = noteId;
    noteIdToTabIndex_[noteId] = index;

    connect(editor, &NoteEditor::contentChanged, this, [this, noteId](qint64 id, const QString& content) {
        emit tabContentChanged(noteId, content);
    });

    connect(editor, &NoteEditor::headingCreated, this, &NoteTabManager::headingCreated);

    emit tabOpened(noteId);
    return index;
}

void NoteTabManager::closeTab(int index) {
    if (index < 0 || index >= count()) {
        return;
    }

    qint64 noteId = tabIndexToNoteId_.value(index, -1);
    if (noteId != -1) {
        noteIdToTabIndex_.remove(noteId);
        emit tabClosed(noteId);
    }

    QWidget* widget = this->widget(index);
    removeTab(index);
    delete widget;

    tabIndexToNoteId_.clear();
    for (int i = 0; i < count(); ++i) {
        NoteEditor* editor = qobject_cast<NoteEditor*>(this->widget(i));
        if (editor) {
            qint64 nid = editor->currentNoteId();
            if (nid != -1) {
                tabIndexToNoteId_[i] = nid;
                noteIdToTabIndex_[nid] = i;
            }
        }
    }
}

void NoteTabManager::closeAllTabs() {
    while (count() > 0) {
        closeTab(0);
    }
}

void NoteTabManager::closeNote(qint64 noteId) {
    int index = findTabByNoteId(noteId);
    if (index >= 0) {
        closeTab(index);
    }
}

qint64 NoteTabManager::noteIdAt(int index) const {
    return tabIndexToNoteId_.value(index, -1);
}

int NoteTabManager::findTabByNoteId(qint64 noteId) const {
    return noteIdToTabIndex_.value(noteId, -1);
}

NoteEditor* NoteTabManager::editorAt(int index) const {
    return qobject_cast<NoteEditor*>(widget(index));
}

NoteEditor* NoteTabManager::currentEditor() const {
    return editorAt(currentIndex());
}

int NoteTabManager::tabCount() const {
    return count();
}

bool NoteTabManager::isNoteOpen(qint64 noteId) const {
    return noteIdToTabIndex_.contains(noteId);
}

void NoteTabManager::onTabChanged(int index) {
    emit currentTabChanged(index);
}

void NoteTabManager::onCurrentEditorContentChanged(qint64 noteId, const QString& content) {
    emit tabContentChanged(noteId, content);
}

void NoteTabManager::onTabCloseRequested(int index) {
    closeTab(index);
}

void NoteTabManager::setupConnections() {
    connect(this, &QTabWidget::currentChanged, this, &NoteTabManager::onTabChanged);
    connect(this, &QTabWidget::tabCloseRequested, this, &NoteTabManager::onTabCloseRequested);
}
