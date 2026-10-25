#include "encryptednoteviewdialog.h"
#include <QApplication>

EncryptedNoteViewDialog::EncryptedNoteViewDialog(QWidget* parent)
    : QDialog(parent), parser_(new MarkdownParser(this)) {
    setWindowTitle(tr("查看加密笔记"));
    resize(800, 600);

    QString dialogStyle = R"(
        QDialog {
            background-color: #f5f9f5;
        }
        QWebEngineView {
            background-color: #ffffff;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
        }
        QPushButton {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 7px 18px;
            color: #2e5c2e;
        }
        QPushButton:hover {
            background-color: #d4edda;
            border: 1px solid #a8d8a8;
        }
        QPushButton:pressed {
            background-color: #c8e6c8;
        }
        QLabel {
            color: #2e5c2e;
            font-weight: bold;
            font-size: 16px;
        }
    )";
    setStyleSheet(dialogStyle);

    setupUI();
    setupConnections();
}

void EncryptedNoteViewDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    titleLabel_ = new QLabel();
    titleLabel_->setAlignment(Qt::AlignCenter);

    webPreview_ = new QWebEngineView();
    webPreview_->setHtml("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"></head><body></body></html>");

    closeButton_ = new QPushButton(tr("关闭"));

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton_);

    mainLayout->addWidget(titleLabel_);
    mainLayout->addWidget(webPreview_);
    mainLayout->addLayout(buttonLayout);
}

void EncryptedNoteViewDialog::setupConnections() {
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void EncryptedNoteViewDialog::setNoteContent(const QString& title, const QString& markdownContent) {
    noteTitle_ = title;
    titleLabel_->setText(title);

    QString html = parser_->parseToWebHTML(markdownContent);
    webPreview_->setHtml(html, QUrl("about:blank"));
}
