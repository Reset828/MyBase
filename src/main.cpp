#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("MyBase");
    app.setOrganizationName("MyBase");
#ifndef MYBASE_VERSION
#define MYBASE_VERSION "2.0.0"
#endif
    app.setApplicationVersion(QStringLiteral(MYBASE_VERSION));

    QString dbPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("mybase.db");
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    MainWindow window;
    if (!window.initialize(dbPath)) {
        return 1;
    }
    window.show();

    return app.exec();
}
