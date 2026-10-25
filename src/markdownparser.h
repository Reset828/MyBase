#ifndef MARKDOWNPARSER_H
#define MARKDOWNPARSER_H

#include <QObject>
#include <QTextDocument>
#include <QString>
#include <QList>
#include <QFuture>
#include <QCache>
#include <QMutex>
#include <QtGlobal>

#include "markdownhtmlrenderer.h"
#include "markdownblock.h"

class MarkdownParser : public QObject {
    Q_OBJECT

public:
    explicit MarkdownParser(QObject* parent = nullptr);

    void setCacheEnabled(bool enabled);
    void clearCache();
    int cacheSize() const;

    void updateCache(qint64 noteId, const QString& markdown, const QString& html);
    void invalidateCache(qint64 noteId);

    QTextDocument* parseToDocument(const QString& markdown);
    QString parseToHTML(const QString& markdown);
    QString parseToWebHTML(const QString& markdown);
    QString parseToWebHTML(qint64 noteId, const QString& markdown);

    QFuture<QTextDocument*> parseToDocumentAsync(const QString& markdown);
    QFuture<QString> parseToHTMLAsync(const QString& markdown);
    QFuture<QString> parseToWebHTMLAsync(const QString& markdown);
    QFuture<QString> parseToWebHTMLAsync(qint64 noteId, const QString& markdown);

signals:
    void parsingStarted();
    void parsingFinished();

private:
    using ParsedBlock = MarkdownBlock;

    QList<ParsedBlock> parseBlocks(const QString& markdown);
    void applyBlockToDocument(QTextDocument* doc, const ParsedBlock& block);
    void applyInlineFormats(QTextCursor& cursor, const QString& text);

    MarkdownHtmlRenderer htmlRenderer_;
    bool cacheEnabled_;
    mutable QMutex cacheMutex_;
    struct CachedWebHtml { QByteArray fingerprint; QString html; };
    QCache<qint64, CachedWebHtml> webHTMLCache_;
    static const int MAX_CACHE_SIZE = 100;
};

#endif // MARKDOWNPARSER_H
