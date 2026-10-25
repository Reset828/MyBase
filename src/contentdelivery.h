#ifndef CONTENTDELIVERY_H
#define CONTENTDELIVERY_H

#include <QMap>
#include <QString>
#include <atomic>

class MarkdownHtmlRenderer;

class TemplateService final {
public:
    static QString applyVariables(const QString& content, const QMap<QString, QString>& values);
    static QMap<QString, QString> builtInTemplates();
};

class DocumentExporter final {
public:
    explicit DocumentExporter(MarkdownHtmlRenderer& renderer);
    bool exportMarkdown(const QString& markdown, const QString& path, QString* error = nullptr, bool overwrite = false, const std::atomic_bool* cancelled = nullptr) const;
    bool exportHtml(const QString& markdown, const QString& path, QString* error = nullptr, bool overwrite = false, const std::atomic_bool* cancelled = nullptr) const;
    bool exportPdf(const QString& markdown, const QString& path, QString* error = nullptr, bool overwrite = false, const std::atomic_bool* cancelled = nullptr) const;

private:
    MarkdownHtmlRenderer& renderer_;
};

#endif
