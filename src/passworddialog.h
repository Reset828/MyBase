#ifndef PASSWORDDIALOG_H
#define PASSWORDDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class PasswordDialog : public QDialog {
    Q_OBJECT

public:
    enum Mode {
        Encrypt,
        Decrypt
    };

    explicit PasswordDialog(Mode mode, QWidget* parent = nullptr);

    QString getPassword() const;
    void setConfirmMode(bool confirm);

private slots:
    void onOkClicked();
    void onTogglePasswordVisibility();

private:
    void setupUi();

    Mode mode_;
    bool confirmMode_;
    QLineEdit* passwordEdit_;
    QLineEdit* confirmPasswordEdit_;
    QLabel* confirmLabel_;
    QPushButton* okButton_;
    QPushButton* toggleButton_;
};

#endif // PASSWORDDIALOG_H
