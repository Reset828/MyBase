#include "markdownhtmlrenderer.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

QString MarkdownHtmlRenderer::renderLegacyHtml(const QString& markdown) {
    QString html = markdown;

    html = renderHeadersToHTML(html);
    html = renderCodeBlocksToHTML(html);
    html = renderTablesToHTML(html);
    html = renderListsToHTML(html);
    html = renderBlockquotesToHTML(html);
    html = renderHorizontalRulesToHTML(html);
    html = renderParagraphsToHTML(html);

    return injectHTMLStyles(html);
}

QString MarkdownHtmlRenderer::renderInlineHtml(const QString& text) {
    QString result = text;

    result.replace(QRegularExpression(R"(\*\*(.+?)\*\*)"), R"(<strong>\1</strong>)");
    result.replace(QRegularExpression(R"(\*(.+?)\*)"), R"(<em>\1</em>)");
    result.replace(QRegularExpression(R"(`(.+?)`)"), R"(<code>\1</code>)");
    result.replace(QRegularExpression(R"(\[(.+?)\]\((.+?)\))"), R"(<a href="\2">\1</a>)");
    result.replace(QRegularExpression(R"(\$\$(.+?)\$\$)"), R"(<div class="math">\1</div>)");
    result.replace(QRegularExpression(R"(\$(.+?)\$)"), R"(<span class="math">\1</span>)");

    return result;
}

QString MarkdownHtmlRenderer::renderHeadersToHTML(const QString& markdown) {
    QString result = markdown;
    QRegularExpression headerRe(R"(^#{1,6}\s+.+$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = headerRe.globalMatch(result);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString line = match.captured();
        int level = 0;
        for (QChar c : line) {
            if (c == '#') level++;
            else break;
        }
        QString content = line.mid(level).trimmed();
        QString html = QString("<h%1>%2</h%1>").arg(level).arg(renderInlineHtml(content));
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    return result;
}

QString MarkdownHtmlRenderer::renderListsToHTML(const QString& markdown) {
    QString result = markdown;

    QRegularExpression unorderedRe(R"((?:^[-*+]\s+.+$\n?)+)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = unorderedRe.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString block = match.captured();
        QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        QString html = "<ul>\n";
        for (const QString& line : lines) {
            QString content = line.mid(2).trimmed();
            if (content.startsWith("[ ] ") || content.startsWith("[x] ") || content.startsWith("[X] ")) {
                bool checked = content[1] != ' ';
                content = content.mid(4).trimmed();
                QString checkbox = checked
                    ? QString("<input type=\"checkbox\" checked disabled> ")
                    : QString("<input type=\"checkbox\" disabled> ");
                html += QString("<li class=\"task-item\">%1%2</li>\n").arg(checkbox, renderInlineHtml(content));
            } else {
                html += QString("<li>%1</li>\n").arg(renderInlineHtml(content));
            }
        }
        html += "</ul>";
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    QRegularExpression orderedRe(R"((?:^\d+\.\s+.+$\n?)+)", QRegularExpression::MultilineOption);
    it = orderedRe.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString block = match.captured();
        QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        QString html = "<ol>\n";
        for (const QString& line : lines) {
            QString content = line.mid(line.indexOf('.') + 2).trimmed();
            html += QString("<li>%1</li>\n").arg(renderInlineHtml(content));
        }
        html += "</ol>";
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    return result;
}

QString MarkdownHtmlRenderer::renderCodeBlocksToHTML(const QString& markdown) {
    QString result = markdown;
    QRegularExpression codeRe(R"(```(\w*)\s*\n([\s\S]*?)```)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = codeRe.globalMatch(result);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString lang = match.captured(1).trimmed();
        QString code = match.captured(2);
        if (code.endsWith('\n')) code.chop(1);
        code.replace("&", "&amp;");
        code.replace("<", "&lt;");
        code.replace(">", "&gt;");

        QString langClass = lang.isEmpty() ? QString() : QString(" class=\"language-%1\"").arg(lang.toLower());
        QString langLabel = lang.isEmpty() ? QString() : QString("<span class=\"code-lang\">%1</span>").arg(lang.toLower());

        QString html = QString("<div class=\"code-block\">%4<pre><code%1>%2</code></pre></div>").arg(langClass, code, lang, langLabel);
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    return result;
}

QString MarkdownHtmlRenderer::renderTablesToHTML(const QString& markdown) {
    QString result = markdown;
    QRegularExpression tableRe(R"((?:^\|.+\|(?:\n|$))+)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = tableRe.globalMatch(result);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString block = match.captured();
        QStringList lines = block.split('\n', Qt::SkipEmptyParts);

        if (lines.size() < 2) continue;

        QRegularExpression separatorRe(R"(^[\s\-:|]+$)");

        QString html = "<table>\n<thead>\n<tr>\n";
        QStringList headers = lines[0].split('|', Qt::SkipEmptyParts);
        for (const QString& header : headers) {
            html += QString("<th>%1</th>\n").arg(renderInlineHtml(header.trimmed()));
        }
        html += "</tr>\n</thead>\n<tbody>\n";

        for (int i = 1; i < lines.size(); ++i) {
            if (separatorRe.match(lines[i]).hasMatch()) continue;
            html += "<tr>\n";
            QStringList cells = lines[i].split('|', Qt::SkipEmptyParts);
            for (const QString& cell : cells) {
                html += QString("<td>%1</td>\n").arg(renderInlineHtml(cell.trimmed()));
            }
            html += "</tr>\n";
        }
        html += "</tbody>\n</table>";
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    return result;
}

QString MarkdownHtmlRenderer::renderBlockquotesToHTML(const QString& markdown) {
    QString result = markdown;
    QRegularExpression blockquoteRe(R"((?:^>\s+.+$\n?)+)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = blockquoteRe.globalMatch(result);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString block = match.captured();
        QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        QString content;
        for (const QString& line : lines) {
            content += line.mid(line.indexOf('>') + 1).trimmed() + "\n";
        }
        QString html = QString("<blockquote>%1</blockquote>").arg(renderInlineHtml(content.trimmed()));
        result.replace(match.capturedStart(), match.capturedLength(), html);
    }

    return result;
}

QString MarkdownHtmlRenderer::renderHorizontalRulesToHTML(const QString& markdown) {
    QString result = markdown;
    result.replace(QRegularExpression(R"(^---$)", QRegularExpression::MultilineOption), "<hr>");
    result.replace(QRegularExpression(R"(^\*\*\*$)", QRegularExpression::MultilineOption), "<hr>");
    result.replace(QRegularExpression(R"(^___$)", QRegularExpression::MultilineOption), "<hr>");
    return result;
}

QString MarkdownHtmlRenderer::renderParagraphsToHTML(const QString& markdown) {
    QString result = markdown;
    QStringList lines = result.split('\n');
    QString processed;
    QString paragraph;

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty() && !paragraph.isEmpty()) {
            processed += QString("<p>%1</p>\n").arg(renderInlineHtml(paragraph.trimmed()));
            paragraph.clear();
        } else if (line.startsWith("<h") || line.startsWith("<ul") || line.startsWith("<ol") ||
                   line.startsWith("<pre") || line.startsWith("<table") || line.startsWith("<blockquote") ||
                   line.startsWith("<hr")) {
            if (!paragraph.isEmpty()) {
                processed += QString("<p>%1</p>\n").arg(renderInlineHtml(paragraph.trimmed()));
                paragraph.clear();
            }
            processed += line + "\n";
        } else {
            paragraph += line + " ";
        }
    }

    if (!paragraph.isEmpty()) {
        processed += QString("<p>%1</p>\n").arg(renderInlineHtml(paragraph.trimmed()));
    }

    return processed;
}

QString MarkdownHtmlRenderer::injectHTMLStyles(const QString& bodyHTML) {
    QString css = R"(
<style>
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; line-height: 1.6; padding: 20px; max-width: 800px; margin: 0 auto; }
h1, h2, h3, h4, h5, h6 { margin-top: 24px; margin-bottom: 16px; font-weight: 600; }
code { background: #f5f5f5; padding: 2px 4px; border-radius: 3px; font-family: Consolas, monospace; }
pre { background: #f5f5f5; padding: 16px; border-radius: 6px; overflow-x: auto; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; width: 100%; margin: 16px 0; }
th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
th { background: #f5f5f5; }
blockquote { border-left: 4px solid #ddd; padding-left: 16px; color: #666; margin: 16px 0; }
a { color: #0366d6; text-decoration: none; }
a:hover { text-decoration: underline; }
hr { border: none; border-top: 1px solid #ddd; margin: 24px 0; }
.math { font-style: italic; }
</style>
)";

    return QString("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n%1\n</head>\n<body>\n%2\n</body>\n</html>").arg(css, bodyHTML);
}
QString MarkdownHtmlRenderer::injectWebHTMLTemplate(const QString& bodyHTML) {
    QString template_ = R"(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
/* Atom One Light */
.hljs{color:#383a42;background:#fafafa}.hljs-comment,.hljs-quote{color:#a0a1a7;font-style:italic}.hljs-doctag,.hljs-keyword,.hljs-formula{color:#a626a4}.hljs-section,.hljs-name,.hljs-selector-tag,.hljs-deletion,.hljs-subst{color:#e45649}.hljs-literal{color:#0184bb}.hljs-string,.hljs-regexp,.hljs-addition,.hljs-attribute,.hljs-meta .hljs-string{color:#50a14f}.hljs-attr,.hljs-variable,.hljs-template-variable,.hljs-type,.hljs-selector-class,.hljs-selector-attr,.hljs-selector-pseudo,.hljs-number{color:#986801}.hljs-symbol,.hljs-bullet,.hljs-link,.hljs-meta,.hljs-selector-id,.hljs-title{color:#4078f2}.hljs-built_in,.hljs-title.class_,.hljs-class .hljs-title{color:#c18401}.hljs-emphasis{font-style:italic}.hljs-strong{font-weight:bold}.hljs-link{text-decoration:underline}
</style>
<style>
:root {
    --primary-color: #4285f4;
    --text-color: #303133;
    --text-secondary: #606266;
    --text-muted: #909399;
    --border-color: #e4e7ed;
    --bg-code: #fafafa;
    --bg-hover: #f5f7fa;
    --bg-blockquote: #f0f5ff;
    --border-blockquote: #4285f4;
    --shadow-code: 0 2px 12px rgba(0,0,0,0.06);
}

* { box-sizing: border-box; }

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'PingFang SC', 'Hiragino Sans GB',
                 'Microsoft YaHei', 'Helvetica Neue', Helvetica, Arial, sans-serif;
    font-size: 15px;
    line-height: 1.8;
    color: var(--text-color);
    background: #ffffff;
    padding: 32px 40px;
    max-width: 900px;
    margin: 0 auto;
    -webkit-font-smoothing: antialiased;
    text-rendering: optimizeLegibility;
}

h1, h2, h3, h4, h5, h6 {
    color: var(--text-color);
    font-weight: 600;
    line-height: 1.4;
    margin-top: 1.8em;
    margin-bottom: 0.6em;
}

h1 {
    font-size: 1.9em;
    text-align: center;
    margin-top: 0;
    margin-bottom: 1em;
    padding-bottom: 0.5em;
}

h1::after {
    content: '';
    display: block;
    width: 80px;
    height: 3px;
    background: var(--primary-color);
    margin: 0.5em auto 0;
    border-radius: 2px;
}

h2 {
    font-size: 1.5em;
    padding-bottom: 0.3em;
    border-bottom: 1px solid var(--border-color);
}

h3 { font-size: 1.25em; }
h4 { font-size: 1.1em; }
h5 { font-size: 1em; color: var(--text-secondary); }
h6 { font-size: 0.9em; color: var(--text-muted); }

p {
    margin-top: 0;
    margin-bottom: 1em;
    word-break: break-word;
    overflow-wrap: break-word;
}

a {
    color: var(--primary-color);
    text-decoration: none;
    border-bottom: 1px solid transparent;
    transition: border-color 0.2s ease;
}

a:hover {
    border-bottom-color: var(--primary-color);
}

strong { font-weight: 600; color: var(--text-color); }
em { font-style: italic; }

code {
    background: #f0f2f5;
    color: #c7254e;
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 0.9em;
    font-family: 'SF Mono', 'Fira Code', 'Fira Mono', 'Roboto Mono', Consolas, 'Liberation Mono', Menlo, monospace;
    word-break: break-word;
}

.code-block {
    position: relative;
    margin: 1em 0;
    border-radius: 8px;
    overflow: hidden;
    box-shadow: var(--shadow-code);
    border: 1px solid var(--border-color);
}

.code-block .code-lang {
    position: absolute;
    top: 0;
    right: 0;
    padding: 3px 10px;
    font-size: 11px;
    color: var(--text-muted);
    background: #f5f5f5;
    border-left: 1px solid var(--border-color);
    border-bottom: 1px solid var(--border-color);
    border-radius: 0 8px 0 4px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    user-select: none;
    z-index: 1;
}

pre {
    margin: 0;
    background: var(--bg-code);
    padding: 16px 20px;
    overflow-x: auto;
    border-radius: 8px;
}

pre code {
    background: none;
    color: inherit;
    padding: 0;
    border-radius: 0;
    font-size: 13.5px;
    line-height: 1.6;
    word-break: normal;
    white-space: pre;
}

.hljs {
    background: var(--bg-code) !important;
    padding: 0 !important;
}

table {
    border-collapse: collapse;
    width: 100%;
    margin: 1em 0;
    border-radius: 8px;
    overflow: hidden;
    box-shadow: 0 1px 4px rgba(0,0,0,0.04);
    border: 1px solid var(--border-color);
}

thead { background: #f8f9fb; }

th {
    padding: 10px 16px;
    text-align: left;
    font-weight: 600;
    font-size: 0.9em;
    color: var(--text-secondary);
    border-bottom: 2px solid var(--border-color);
}

td {
    padding: 10px 16px;
    border-bottom: 1px solid #f0f2f5;
    font-size: 0.95em;
}

tr:last-child td { border-bottom: none; }

tr:hover td { background: var(--bg-hover); }

blockquote {
    margin: 1em 0;
    padding: 12px 20px;
    border-left: 4px solid var(--border-blockquote);
    background: var(--bg-blockquote);
    border-radius: 0 8px 8px 0;
    color: var(--text-secondary);
}

blockquote p:last-child { margin-bottom: 0; }

blockquote blockquote {
    margin-top: 0.8em;
    border-left-color: #67c23a;
    background: #f0f9eb;
}

hr {
    border: none;
    height: 1px;
    background: linear-gradient(to right, transparent, var(--border-color), transparent);
    margin: 2em 0;
}

ul, ol {
    padding-left: 1.8em;
    margin-top: 0;
    margin-bottom: 1em;
}

li {
    margin-bottom: 0.35em;
    line-height: 1.7;
}

li.task-item {
    list-style: none;
    margin-left: -1.3em;
}

li.task-item input[type="checkbox"] {
    margin-right: 6px;
    vertical-align: middle;
    accent-color: var(--primary-color);
    transform: scale(1.1);
}

li > ul, li > ol {
    margin-top: 0.2em;
    margin-bottom: 0;
}

ul li::marker { color: var(--primary-color); }
ol li::marker { color: var(--primary-color); font-weight: 600; }

img {
    max-width: 100%;
    border-radius: 8px;
    box-shadow: 0 2px 12px rgba(0,0,0,0.08);
    margin: 0.5em 0;
}

.math {
    font-style: italic;
    color: var(--text-secondary);
    background: #f9f2f4;
    padding: 2px 4px;
    border-radius: 3px;
}

@media print {
    body { padding: 0; max-width: none; }
    .code-block { box-shadow: none; border: 1px solid #ddd; }
    table { box-shadow: none; }
}
</style>
</head>
<body>
%1
<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
<script>
document.addEventListener('DOMContentLoaded', function() {
    document.querySelectorAll('pre code').forEach(function(block) {
        hljs.highlightElement(block);
    });
});
</script>
</body>
</html>)";

    return template_.arg(bodyHTML);
}
QString MarkdownHtmlRenderer::renderWebHtml(const QList<MarkdownBlock>& blocks) {
    QString bodyHTML;

    bool inTable = false;
    bool isFirstTableRow = true;

    for (const MarkdownBlock& block : blocks) {
        if (block.type == MarkdownBlock::Table) {
            if (!inTable) {
                bodyHTML += "<table>\n<thead>\n<tr>\n";
                isFirstTableRow = true;
                inTable = true;
            }

            if (isFirstTableRow) {
                for (const QString& cell : block.cells) {
                    bodyHTML += QString("<th>%1</th>\n").arg(renderInlineHtml(cell.trimmed()));
                }
                bodyHTML += "</tr>\n</thead>\n<tbody>\n";
                isFirstTableRow = false;
            } else {
                bodyHTML += "<tr>\n";
                for (const QString& cell : block.cells) {
                    bodyHTML += QString("<td>%1</td>\n").arg(renderInlineHtml(cell.trimmed()));
                }
                bodyHTML += "</tr>\n";
            }
        } else {
            if (inTable) {
                bodyHTML += "</tbody>\n</table>\n";
                inTable = false;
            }
            bodyHTML += blockToWebHTML(block);
        }
    }

    if (inTable) {
        bodyHTML += "</tbody>\n</table>\n";
    }

    QString html = injectWebHTMLTemplate(bodyHTML);

    return html;
}

QString MarkdownHtmlRenderer::blockToWebHTML(const MarkdownBlock& block) {
    switch (block.type) {
        case MarkdownBlock::Header: {
            int level = qBound(1, block.level, 6);
            return QString("<h%1>%2</h%1>\n").arg(level).arg(renderInlineHtml(block.content));
        }

        case MarkdownBlock::Paragraph: {
            return QString("<p>%1</p>\n").arg(renderInlineHtml(block.content.trimmed()));
        }

        case MarkdownBlock::CodeBlock: {
            QString code = block.content;
            if (code.endsWith('\n')) code.chop(1);
            code.replace("&", "&amp;");
            code.replace("<", "&lt;");
            code.replace(">", "&gt;");

            QString lang = block.lang;
            QString langClass = lang.isEmpty() ? QString() : QString(" class=\"language-%1\"").arg(lang.toLower());
            QString langLabel = lang.isEmpty() ? QString() : QString("<span class=\"code-lang\">%1</span>").arg(lang.toLower());

            return QString("<div class=\"code-block\">%4<pre><code%1>%2</code></pre></div>\n").arg(langClass, code, lang, langLabel);
        }

        case MarkdownBlock::List: {
            QStringList items = block.content.split('\n', Qt::SkipEmptyParts);
            QString tag = block.isOrdered ? "ol" : "ul";
            QString html = QString("<%1>\n").arg(tag);
            for (const QString& item : items) {
                if (block.isTask) {
                    html += QString("<li class=\"task-item\"><input type=\"checkbox\" disabled> %1</li>\n").arg(renderInlineHtml(item.trimmed()));
                } else {
                    html += QString("<li>%1</li>\n").arg(renderInlineHtml(item.trimmed()));
                }
            }
            html += QString("</%1>\n").arg(tag);
            return html;
        }

        case MarkdownBlock::Blockquote: {
            return QString("<blockquote>%1</blockquote>\n").arg(renderInlineHtml(block.content.trimmed()));
        }

        case MarkdownBlock::HorizontalRule: {
            return QString("<hr>\n");
        }

        case MarkdownBlock::EmptyLine: {
            return QString();
        }

        case MarkdownBlock::Table: {
            return QString();
        }

        default:
            return QString();
    }
}
