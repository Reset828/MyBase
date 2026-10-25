#ifndef ENCRYPTEDNOTEVIEWDIALOG_H
#define ENCRYPTEDNOTEVIEWDIALOG_H

#include <QDialog>
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "markdownparser.h"

class EncryptedNoteViewDialog : public QDialog {
    Q_OBJECT

public:
    explicit EncryptedNoteViewDialog(QWidget* parent = nullptr);
    void setNoteContent(const QString& title, const QString& markdownContent);

private:
    void setupUI();
    void setupConnections();

    QString noteTitle_;
    QLabel* titleLabel_;
    QWebEngineView* webPreview_;
    QPushButton* closeButton_;
    MarkdownParser* parser_;
};

#endif // ENCRYPTEDNOTEVIEWDIALOG_H
