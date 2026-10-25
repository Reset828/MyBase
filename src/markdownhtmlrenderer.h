#ifndef MARKDOWNHTMLRENDERER_H
#define MARKDOWNHTMLRENDERER_H

#include <QList>
#include <QString>

#include "markdownblock.h"

class MarkdownHtmlRenderer final {
public:
    QString renderLegacyHtml(const QString& markdown);
    QString renderWebHtml(const QList<MarkdownBlock>& blocks);

private:
    QString renderInlineHtml(const QString& text);
    QString renderHeadersToHTML(const QString& markdown);
    QString renderListsToHTML(const QString& markdown);
    QString renderCodeBlocksToHTML(const QString& markdown);
    QString renderTablesToHTML(const QString& markdown);
    QString renderBlockquotesToHTML(const QString& markdown);
    QString renderHorizontalRulesToHTML(const QString& markdown);
    QString renderParagraphsToHTML(const QString& markdown);
    QString injectHTMLStyles(const QString& bodyHTML);
    QString injectWebHTMLTemplate(const QString& bodyHTML);
    QString blockToWebHTML(const MarkdownBlock& block);
};

#endif // MARKDOWNHTMLRENDERER_H
