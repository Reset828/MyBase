#include "passworddialog.h"
#include <QHBoxLayout>

PasswordDialog::PasswordDialog(Mode mode, QWidget* parent)
    : QDialog(parent), mode_(mode), confirmMode_(mode == Encrypt), passwordEdit_(nullptr), confirmPasswordEdit_(nullptr), confirmLabel_(nullptr), okButton_(nullptr), toggleButton_(nullptr) {
    setupUi();
}

QString PasswordDialog::getPassword() const {
    return passwordEdit_->text();
}

void PasswordDialog::setConfirmMode(bool confirm) {
    confirmMode_ = confirm;
    if (confirmLabel_) {
        confirmLabel_->setVisible(confirm);
    }
    if (confirmPasswordEdit_) {
        confirmPasswordEdit_->setVisible(confirm);
    }
    adjustSize();
}

void PasswordDialog::setupUi() {
    setWindowTitle(mode_ == Encrypt ? QStringLiteral("加密笔记") : QStringLiteral("解密笔记"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QString inputStyle = R"(
        QLineEdit {
            border: 1px solid #c8e6c8;
            border-radius: 8px;
            padding: 8px 12px;
            background-color: #ffffff;
            color: #2e5c2e;
            font-size: 13px;
        }
        QLineEdit:focus {
            border: 1px solid #7bc47b;
        }
    )";

    QString buttonStyle = R"(
        QPushButton {
            background-color: #eaf4ea;
            border: 1px solid #c8e6c8;
            border-radius: 6px;
            padding: 6px 16px;
            color: #2e5c2e;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #d4edda;
            border: 1px solid #a8d8a8;
        }
        QPushButton:pressed {
            background-color: #c8e6c8;
        }
    )";

    QString primaryButtonStyle = R"(
        QPushButton {
            background-color: #7bc47b;
            border: 1px solid #5aa85a;
            border-radius: 6px;
            padding: 6px 16px;
            color: #ffffff;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #5aa85a;
        }
        QPushButton:pressed {
            background-color: #4a984a;
        }
    )";

    QString dialogStyle = R"(
        QDialog {
            background-color: #f5f9f5;
        }
        QLabel {
            color: #2e5c2e;
        }
    )";

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QLabel* titleLabel = new QLabel(mode_ == Encrypt ? QStringLiteral("请输入加密密码：") : QStringLiteral("请输入解密密码："), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 14px; color: #2d5a3d;"));
    mainLayout->addWidget(titleLabel);

    QHBoxLayout* passwordLayout = new QHBoxLayout();
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("输入密码"));
    passwordEdit_->setMinimumWidth(250);
    passwordEdit_->setStyleSheet(inputStyle);
    passwordLayout->addWidget(passwordEdit_);

    toggleButton_ = new QPushButton(QStringLiteral("显示"), this);
    toggleButton_->setFixedSize(60, 30);
    toggleButton_->setStyleSheet(buttonStyle);
    connect(toggleButton_, &QPushButton::clicked, this, &PasswordDialog::onTogglePasswordVisibility);
    passwordLayout->addWidget(toggleButton_);

    mainLayout->addLayout(passwordLayout);

    confirmLabel_ = new QLabel(QStringLiteral("请再次输入密码确认："), this);
    confirmLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #2d5a3d;"));
    confirmLabel_->setVisible(confirmMode_);
    mainLayout->addWidget(confirmLabel_);

    confirmPasswordEdit_ = new QLineEdit(this);
    confirmPasswordEdit_->setEchoMode(QLineEdit::Password);
    confirmPasswordEdit_->setPlaceholderText(QStringLiteral("确认密码"));
    confirmPasswordEdit_->setMinimumWidth(250);
    confirmPasswordEdit_->setStyleSheet(inputStyle);
    confirmPasswordEdit_->setVisible(confirmMode_);
    mainLayout->addWidget(confirmPasswordEdit_);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    cancelButton->setFixedSize(80, 32);
    cancelButton->setStyleSheet(buttonStyle);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);

    okButton_ = new QPushButton(QStringLiteral("确定"), this);
    okButton_->setFixedSize(80, 32);
    okButton_->setStyleSheet(primaryButtonStyle);
    connect(okButton_, &QPushButton::clicked, this, &PasswordDialog::onOkClicked);
    buttonLayout->addWidget(okButton_);

    mainLayout->addLayout(buttonLayout);

    setStyleSheet(dialogStyle);
    adjustSize();
}

void PasswordDialog::onOkClicked() {
    QString password = passwordEdit_->text();
    
    if (password.isEmpty()) {
        passwordEdit_->setFocus();
        return;
    }

    if (confirmMode_ && mode_ == Encrypt) {
        QString confirmPassword = confirmPasswordEdit_->text();
        if (password != confirmPassword) {
            confirmPasswordEdit_->setFocus();
            confirmPasswordEdit_->selectAll();
            return;
        }
    }

    accept();
}

void PasswordDialog::onTogglePasswordVisibility() {
    if (passwordEdit_->echoMode() == QLineEdit::Password) {
        passwordEdit_->setEchoMode(QLineEdit::Normal);
        toggleButton_->setText(QStringLiteral("隐藏"));
    } else {
        passwordEdit_->setEchoMode(QLineEdit::Password);
        toggleButton_->setText(QStringLiteral("显示"));
    }
}
