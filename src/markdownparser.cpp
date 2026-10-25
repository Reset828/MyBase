#include "markdownparser.h"
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QFont>
#include <QTextTable>
#include <QTextTableCell>
#include <QRegularExpressionMatch>
#include <QtConcurrent/QtConcurrent>
#include <QCryptographicHash>

MarkdownParser::MarkdownParser(QObject* parent)
    : QObject(parent), cacheEnabled_(true) {
    webHTMLCache_.setMaxCost(MAX_CACHE_SIZE);
}

void MarkdownParser::setCacheEnabled(bool enabled) {
    QMutexLocker locker(&cacheMutex_);
    cacheEnabled_ = enabled;
    if (!enabled) {
        webHTMLCache_.clear();
    }
}

void MarkdownParser::clearCache() {
    QMutexLocker locker(&cacheMutex_);
    webHTMLCache_.clear();
}

int MarkdownParser::cacheSize() const {
    QMutexLocker locker(&cacheMutex_);
    return webHTMLCache_.size();
}

void MarkdownParser::updateCache(qint64 noteId, const QString& markdown, const QString& html) {
    QMutexLocker locker(&cacheMutex_);
    if (cacheEnabled_ && noteId > 0) {
        auto* cached = new CachedWebHtml;
        cached->fingerprint = QCryptographicHash::hash(markdown.toUtf8(), QCryptographicHash::Sha256);
        cached->html = html;
        webHTMLCache_.insert(noteId, cached);
    }
}

void MarkdownParser::invalidateCache(qint64 noteId) {
    QMutexLocker locker(&cacheMutex_);
    if (noteId > 0) {
        webHTMLCache_.remove(noteId);
    }
}

QTextDocument* MarkdownParser::parseToDocument(const QString& markdown) {
    emit parsingStarted();

    QTextDocument* doc = new QTextDocument();

    if (markdown.trimmed().isEmpty()) {
        emit parsingFinished();
        return doc;
    }

    QList<ParsedBlock> blocks = parseBlocks(markdown);

    for (const ParsedBlock& block : blocks) {
        applyBlockToDocument(doc, block);
    }

    emit parsingFinished();
    return doc;
}

QString MarkdownParser::parseToHTML(const QString& markdown) {
    return htmlRenderer_.renderLegacyHtml(markdown);
}

QList<MarkdownParser::ParsedBlock> MarkdownParser::parseBlocks(const QString& markdown) {
    QList<ParsedBlock> blocks;
    QStringList lines = markdown.split('\n');

    int i = 0;
    while (i < lines.size()) {
        QString line = lines[i];

        if (line.trimmed().isEmpty()) {
            ParsedBlock block;
            block.type = ParsedBlock::EmptyLine;
            block.level = 0;
            block.isOrdered = false;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            i++;
            continue;
        }

        if (line.startsWith("```")) {
            QString lang = line.mid(3).trimmed();
            QString content;
            i++;
            while (i < lines.size() && !lines[i].startsWith("```")) {
                content += lines[i] + "\n";
                i++;
            }
            if (i < lines.size()) i++;
            ParsedBlock block;
            block.type = ParsedBlock::CodeBlock;
            block.content = content;
            block.level = 0;
            block.isOrdered = false;
            block.lang = lang;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            continue;
        }

        QRegularExpression headerRe(R"(^(#{1,6})\s+(.+))");
        QRegularExpressionMatch headerMatch = headerRe.match(line);
        if (headerMatch.hasMatch()) {
            ParsedBlock block;
            block.type = ParsedBlock::Header;
            block.content = headerMatch.captured(2);
            block.level = static_cast<int>(headerMatch.captured(1).length());
            block.isOrdered = false;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            i++;
            continue;
        }

        if (line.startsWith("- ") || line.startsWith("* ") || line.startsWith("+ ")) {
            QString content;
            bool isTask = false;
            bool taskChecked = false;
            while (i < lines.size() && (lines[i].startsWith("- ") || lines[i].startsWith("* ") || lines[i].startsWith("+ "))) {
                QString itemContent = lines[i].mid(2);
                if (itemContent.startsWith("[ ] ") || itemContent.startsWith("[x] ") || itemContent.startsWith("[X] ")) {
                    isTask = true;
                    if (itemContent[1] != ' ') taskChecked = true;
                    itemContent = itemContent.mid(4);
                }
                content += itemContent.trimmed() + "\n";
                i++;
            }
            ParsedBlock block;
            block.type = ParsedBlock::List;
            block.content = content;
            block.level = 0;
            block.isOrdered = false;
            block.lang.clear();
            block.isTask = isTask;
            block.taskChecked = taskChecked;
            blocks.append(block);
            continue;
        }

        QRegularExpression orderedListRe(R"(^\d+\.\s+(.+))");
        QRegularExpressionMatch orderedMatch = orderedListRe.match(line);
        if (orderedMatch.hasMatch()) {
            QString content;
            while (i < lines.size() && orderedListRe.match(lines[i]).hasMatch()) {
                content += orderedListRe.match(lines[i]).captured(1) + "\n";
                i++;
            }
            ParsedBlock block;
            block.type = ParsedBlock::List;
            block.content = content;
            block.level = 0;
            block.isOrdered = true;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            continue;
        }

        if (line.startsWith("|") && line.endsWith("|")) {
            QRegularExpression separatorRe(R"(\|^[\s\-:|]+$)");
            QList<QStringList> tableRows;

            while (i < lines.size() && lines[i].startsWith("|") && lines[i].endsWith("|")) {
                if (!separatorRe.match(lines[i]).hasMatch()) {
                    QString cellLine = lines[i].mid(1, lines[i].length() - 2);
                    QStringList cells = cellLine.split("|");
                    for (QString& cell : cells) {
                        cell = cell.trimmed();
                    }
                    tableRows.append(cells);
                }
                i++;
            }

            for (const QStringList& rowCells : tableRows) {
                ParsedBlock block;
                block.type = ParsedBlock::Table;
                block.level = 0;
                block.cells = rowCells;
                block.isOrdered = false;
                block.isTask = false;
                block.taskChecked = false;
                blocks.append(block);
            }
            continue;
        }

        if (line.startsWith("> ")) {
            QString content;
            while (i < lines.size() && lines[i].startsWith("> ")) {
                content += lines[i].mid(2) + "\n";
                i++;
            }
            ParsedBlock block;
            block.type = ParsedBlock::Blockquote;
            block.content = content;
            block.level = 0;
            block.isOrdered = false;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            continue;
        }

        if (line == "---" || line == "***" || line == "___") {
            ParsedBlock block;
            block.type = ParsedBlock::HorizontalRule;
            block.level = 0;
            block.isOrdered = false;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
            i++;
            continue;
        }

        QString paragraph;
        while (i < lines.size() && !lines[i].trimmed().isEmpty() &&
               !lines[i].startsWith("#") && !lines[i].startsWith("```") &&
               !lines[i].startsWith("- ") && !lines[i].startsWith("* ") &&
               !lines[i].startsWith("+ ") && !lines[i].startsWith("|") &&
               !lines[i].startsWith("> ") && lines[i] != "---" && lines[i] != "***" && lines[i] != "___" &&
               !orderedListRe.match(lines[i]).hasMatch()) {
            paragraph += lines[i] + "\n";
            i++;
        }
        if (!paragraph.isEmpty()) {
            ParsedBlock block;
            block.type = ParsedBlock::Paragraph;
            block.content = paragraph;
            block.level = 0;
            block.isOrdered = false;
            block.isTask = false;
            block.taskChecked = false;
            blocks.append(block);
        }
    }

    return blocks;
}

void MarkdownParser::applyBlockToDocument(QTextDocument* doc, const ParsedBlock& block) {
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);

    if (block.type == ParsedBlock::EmptyLine) {
        cursor.insertBlock();
        return;
    }

    if (block.type == ParsedBlock::HorizontalRule) {
        cursor.insertBlock();
        QTextCharFormat format = cursor.charFormat();
        format.setFontWeight(QFont::Bold);
        cursor.insertText("---", format);
        cursor.insertBlock();
        return;
    }

    QTextBlockFormat blockFormat;
    QTextCharFormat charFormat;

    if (block.type == ParsedBlock::Header) {
        switch (block.level) {
            case 1: charFormat.setFontPointSize(24); charFormat.setFontWeight(QFont::Bold); break;
            case 2: charFormat.setFontPointSize(20); charFormat.setFontWeight(QFont::Bold); break;
            case 3: charFormat.setFontPointSize(18); charFormat.setFontWeight(QFont::Bold); break;
            case 4: charFormat.setFontPointSize(16); charFormat.setFontWeight(QFont::Bold); break;
            case 5: charFormat.setFontPointSize(14); charFormat.setFontWeight(QFont::Bold); break;
            case 6: charFormat.setFontPointSize(12); charFormat.setFontWeight(QFont::Bold); break;
        }
        cursor.insertBlock();
        cursor.setCharFormat(charFormat);
        cursor.insertText(block.content);
    } else if (block.type == ParsedBlock::CodeBlock) {
        QTextCharFormat codeFormat;
        codeFormat.setFontFamily("Consolas");
        codeFormat.setBackground(Qt::lightGray);
        cursor.insertBlock();
        cursor.setCharFormat(codeFormat);
        cursor.insertText(block.content);
    } else if (block.type == ParsedBlock::List) {
        QStringList items = block.content.split('\n', Qt::SkipEmptyParts);
        for (int i = 0; i < items.size(); ++i) {
            cursor.insertBlock();
            QString prefix = block.isOrdered ? QString("%1. ").arg(i + 1) : "• ";
            cursor.insertText(prefix);
            applyInlineFormats(cursor, items[i]);
        }
    } else if (block.type == ParsedBlock::Table) {
        cursor.insertBlock();
        cursor.insertText(block.cells.join(" | "));
    } else if (block.type == ParsedBlock::Blockquote) {
        QTextCharFormat quoteFormat;
        quoteFormat.setFontItalic(true);
        cursor.insertBlock();
        cursor.insertText("> ");
        cursor.setCharFormat(quoteFormat);
        applyInlineFormats(cursor, block.content.trimmed());
    } else {
        cursor.insertBlock();
        applyInlineFormats(cursor, block.content.trimmed());
    }
}

void MarkdownParser::applyInlineFormats(QTextCursor& cursor, const QString& text) {
    QString remaining = text;

    QRegularExpression boldRe(R"(\*\*(.+?)\*\*)");
    QRegularExpression italicRe(R"(\*(.+?)\*)");
    QRegularExpression codeRe(R"(`(.+?)`)");
    QRegularExpression linkRe(R"(\[(.+?)\]\((.+?)\))");

    while (!remaining.isEmpty()) {
        QRegularExpressionMatch boldMatch = boldRe.match(remaining);
        QRegularExpressionMatch italicMatch = italicRe.match(remaining);
        QRegularExpressionMatch codeMatch = codeRe.match(remaining);
        QRegularExpressionMatch linkMatch = linkRe.match(remaining);

        int minPos = remaining.length();
        QString matchedText;
        QString displayText;
        bool isBold = false, isItalic = false, isCode = false, isLink = false;

        if (boldMatch.hasMatch() && boldMatch.capturedStart() < minPos) {
            minPos = boldMatch.capturedStart();
            matchedText = boldMatch.captured();
            displayText = boldMatch.captured(1);
            isBold = true;
        }
        if (italicMatch.hasMatch() && italicMatch.capturedStart() < minPos) {
            minPos = italicMatch.capturedStart();
            matchedText = italicMatch.captured();
            displayText = italicMatch.captured(1);
            isItalic = true;
        }
        if (codeMatch.hasMatch() && codeMatch.capturedStart() < minPos) {
            minPos = codeMatch.capturedStart();
            matchedText = codeMatch.captured();
            displayText = codeMatch.captured(1);
            isCode = true;
        }
        if (linkMatch.hasMatch() && linkMatch.capturedStart() < minPos) {
            minPos = linkMatch.capturedStart();
            matchedText = linkMatch.captured();
            displayText = linkMatch.captured(1);
            isLink = true;
        }

        if (minPos > 0) {
            cursor.insertText(remaining.left(minPos));
        }

        if (minPos < remaining.length()) {
            QTextCharFormat format;
            if (isBold) format.setFontWeight(QFont::Bold);
            if (isItalic) format.setFontItalic(true);
            if (isCode) {
                format.setFontFamily("Consolas");
                format.setBackground(Qt::lightGray);
            }
            if (isLink) {
                format.setFontUnderline(true);
                format.setForeground(Qt::blue);
            }
            cursor.setCharFormat(format);
            cursor.insertText(displayText);
            cursor.setCharFormat(QTextCharFormat());
            remaining = remaining.mid(minPos + matchedText.length());
        } else {
            cursor.insertText(remaining);
            remaining.clear();
        }
    }
}



QFuture<QTextDocument*> MarkdownParser::parseToDocumentAsync(const QString& markdown) {
    emit parsingStarted();
    return QtConcurrent::run([markdown]() -> QTextDocument* {
        MarkdownParser workerParser;
        QTextDocument* doc = new QTextDocument();
        if (markdown.trimmed().isEmpty()) {
            return doc;
        }
        const QList<ParsedBlock> blocks = workerParser.parseBlocks(markdown);
        for (const ParsedBlock& block : blocks) {
            workerParser.applyBlockToDocument(doc, block);
        }
        return doc;
    });
}

QFuture<QString> MarkdownParser::parseToHTMLAsync(const QString& markdown) {
    emit parsingStarted();
    return QtConcurrent::run([markdown]() -> QString {
        MarkdownHtmlRenderer renderer;
        return renderer.renderLegacyHtml(markdown);
    });
}

QString MarkdownParser::parseToWebHTML(const QString& markdown) {
    return htmlRenderer_.renderWebHtml(parseBlocks(markdown));
}

QString MarkdownParser::parseToWebHTML(qint64 noteId, const QString& markdown) {
    if (cacheEnabled_ && noteId > 0) {
        QMutexLocker locker(&cacheMutex_);
        const auto* cached = webHTMLCache_.object(noteId);
        const QByteArray fingerprint = QCryptographicHash::hash(markdown.toUtf8(), QCryptographicHash::Sha256);
        if (cached && cached->fingerprint == fingerprint) {
            return cached->html;
        }
    }

    QString html = parseToWebHTML(markdown);

    updateCache(noteId, markdown, html);
    return html;
}


QFuture<QString> MarkdownParser::parseToWebHTMLAsync(const QString& markdown) {
    emit parsingStarted();
    return QtConcurrent::run([markdown]() -> QString {
        MarkdownParser workerParser;
        return workerParser.parseToWebHTML(markdown);
    });
}

QFuture<QString> MarkdownParser::parseToWebHTMLAsync(qint64 noteId, const QString& markdown) {
    emit parsingStarted();
    if (cacheEnabled_ && noteId > 0) {
        const QByteArray fingerprint = QCryptographicHash::hash(markdown.toUtf8(), QCryptographicHash::Sha256);
        QMutexLocker locker(&cacheMutex_);
        const auto* cached = webHTMLCache_.object(noteId);
        if (cached && cached->fingerprint == fingerprint) {
            const QString html = cached->html;
            return QtConcurrent::run([html]() { return html; });
        }
    }
    return QtConcurrent::run([markdown]() -> QString {
        MarkdownParser workerParser;
        return workerParser.parseToWebHTML(markdown);
    });
}
