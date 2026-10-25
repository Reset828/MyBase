#include "markdownhighlighter.h"
#include <QDebug>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent), inCodeBlock_(false) {
    setupDefaultFormats();
    setupRules();
}

void MarkdownHighlighter::setupDefaultFormats() {
    QTextCharFormat headerFormat;
    headerFormat.setFontWeight(QFont::Bold);
    headerFormat.setForeground(QColor(0x1b, 0x5e, 0x20));
    elementFormats_[ElementType::Header1] = headerFormat;

    QTextCharFormat header2Format;
    header2Format.setFontWeight(QFont::Bold);
    header2Format.setForeground(QColor(0x2e, 0x7d, 0x32));
    elementFormats_[ElementType::Header2] = header2Format;

    QTextCharFormat header3Format;
    header3Format.setFontWeight(QFont::Bold);
    header3Format.setForeground(QColor(0x43, 0xa0, 0x47));
    elementFormats_[ElementType::Header3] = header3Format;

    QTextCharFormat header4Format;
    header4Format.setFontWeight(QFont::Bold);
    header4Format.setForeground(QColor(0x4c, 0xaf, 0x50));
    elementFormats_[ElementType::Header4] = header4Format;

    QTextCharFormat header5Format;
    header5Format.setFontWeight(QFont::Bold);
    header5Format.setForeground(QColor(0x66, 0xbb, 0x6a));
    elementFormats_[ElementType::Header5] = header5Format;

    QTextCharFormat header6Format;
    header6Format.setFontWeight(QFont::Bold);
    header6Format.setForeground(QColor(0x81, 0xc7, 0x84));
    elementFormats_[ElementType::Header6] = header6Format;

    QTextCharFormat boldFormat;
    boldFormat.setFontWeight(QFont::Bold);
    boldFormat.setForeground(QColor(0x33, 0x69, 0x1e));
    elementFormats_[ElementType::Bold] = boldFormat;

    QTextCharFormat italicFormat;
    italicFormat.setFontItalic(true);
    italicFormat.setForeground(QColor(0x55, 0x8b, 0x2f));
    elementFormats_[ElementType::Italic] = italicFormat;

    QTextCharFormat boldItalicFormat;
    boldItalicFormat.setFontWeight(QFont::Bold);
    boldItalicFormat.setFontItalic(true);
    boldItalicFormat.setForeground(QColor(0x33, 0x69, 0x1e));
    elementFormats_[ElementType::BoldItalic] = boldItalicFormat;

    QTextCharFormat codeFormat;
    codeFormat.setFontFamily("Consolas");
    codeFormat.setForeground(QColor(0x6a, 0x5a, 0x3d));
    codeFormat.setBackground(QColor(0xf0, 0xf4, 0xe8));
    elementFormats_[ElementType::Code] = codeFormat;

    QTextCharFormat codeBlockFormat;
    codeBlockFormat.setFontFamily("Consolas");
    codeBlockFormat.setForeground(QColor(0x5d, 0x4e, 0x37));
    codeBlockFormat.setBackground(QColor(0xf5, 0xf7, 0xf0));
    elementFormats_[ElementType::CodeBlock] = codeBlockFormat;

    QTextCharFormat linkFormat;
    linkFormat.setForeground(QColor(0x2e, 0x7d, 0x32));
    linkFormat.setFontUnderline(true);
    elementFormats_[ElementType::Link] = linkFormat;

    QTextCharFormat imageFormat;
    imageFormat.setForeground(QColor(0x55, 0x8b, 0x2f));
    elementFormats_[ElementType::Image] = imageFormat;

    QTextCharFormat listFormat;
    listFormat.setForeground(QColor(0x43, 0xa0, 0x47));
    elementFormats_[ElementType::List] = listFormat;

    QTextCharFormat orderedListFormat;
    orderedListFormat.setForeground(QColor(0x43, 0xa0, 0x47));
    elementFormats_[ElementType::OrderedList] = orderedListFormat;

    QTextCharFormat blockQuoteFormat;
    blockQuoteFormat.setForeground(QColor(0x7c, 0xb3, 0x42));
    blockQuoteFormat.setFontItalic(true);
    elementFormats_[ElementType::BlockQuote] = blockQuoteFormat;

    QTextCharFormat horizontalRuleFormat;
    horizontalRuleFormat.setForeground(QColor(0xa5, 0xd6, 0xa7));
    elementFormats_[ElementType::HorizontalRule] = horizontalRuleFormat;

    QTextCharFormat tableFormat;
    tableFormat.setForeground(QColor(0x2e, 0x7d, 0x32));
    elementFormats_[ElementType::Table] = tableFormat;
}

void MarkdownHighlighter::setupRules() {
    header1Re_ = QRegularExpression(R"(^#{1}\s+.+$)");
    header2Re_ = QRegularExpression(R"(^#{2}\s+.+$)");
    header3Re_ = QRegularExpression(R"(^#{3}\s+.+$)");
    header4Re_ = QRegularExpression(R"(^#{4}\s+.+$)");
    header5Re_ = QRegularExpression(R"(^#{5}\s+.+$)");
    header6Re_ = QRegularExpression(R"(^#{6}\s+.+$)");

    boldItalicRe_ = QRegularExpression(R"(\*\*\*(.+?)\*\*\*|___(.+?)___)");
    boldRe_ = QRegularExpression(R"(\*\*(.+?)\*\*|__(.+?)__)");
    italicRe_ = QRegularExpression(R"(\*(.+?)\*|_(.+?)_)");
    codeRe_ = QRegularExpression(R"(`([^`]+)`)");

    codeBlockStartRe_ = QRegularExpression(R"(^```)");

    linkRe_ = QRegularExpression(R"(\[([^\]]+)\]\(([^)]+)\))");
    imageRe_ = QRegularExpression(R"(!\[([^\]]*)\]\(([^)]+)\))");

    listRe_ = QRegularExpression(R"(^\s*[-*+]\s+)");
    orderedListRe_ = QRegularExpression(R"(^\s*\d+\.\s+)");
    blockQuoteRe_ = QRegularExpression(R"(^\s*>\s*)");
    horizontalRuleRe_ = QRegularExpression(R"(^(-{3,}|_{3,}|\*{3,})$)");
    tableRe_ = QRegularExpression(R"(^\|.+\|$)");
}

void MarkdownHighlighter::setFormatForElement(ElementType type, const QTextCharFormat& format) {
    elementFormats_[type] = format;
    rehighlight();
}

QTextCharFormat MarkdownHighlighter::formatForElement(ElementType type) const {
    if (elementFormats_.contains(type)) {
        return elementFormats_[type];
    }
    return QTextCharFormat();
}

void MarkdownHighlighter::highlightBlock(const QString& text) {
    if (codeBlockStartRe_.match(text).hasMatch()) {
        inCodeBlock_ = !inCodeBlock_;
        setFormat(0, text.length(), elementFormats_[ElementType::CodeBlock]);
        return;
    }

    if (inCodeBlock_) {
        setFormat(0, text.length(), elementFormats_[ElementType::CodeBlock]);
        return;
    }

    highlightHeaders(text);
    highlightLists(text);
    highlightBlockQuotes(text);
    highlightTables(text);
    highlightHorizontalRules(text);
    highlightInlineElements(text);
    highlightLinks(text);
    highlightImages(text);
}

void MarkdownHighlighter::highlightHeaders(const QString& text) {
    QRegularExpressionMatch match;

    match = header1Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header1]);
        return;
    }

    match = header2Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header2]);
        return;
    }

    match = header3Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header3]);
        return;
    }

    match = header4Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header4]);
        return;
    }

    match = header5Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header5]);
        return;
    }

    match = header6Re_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Header6]);
        return;
    }
}

void MarkdownHighlighter::highlightInlineElements(const QString& text) {
    QRegularExpressionMatchIterator it;

    it = boldItalicRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::BoldItalic]);
    }

    it = boldRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::Bold]);
    }

    it = italicRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::Italic]);
    }

    it = codeRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::Code]);
    }
}

void MarkdownHighlighter::highlightLists(const QString& text) {
    QRegularExpressionMatch match;

    match = orderedListRe_.match(text);
    if (match.hasMatch()) {
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::OrderedList]);
        return;
    }

    match = listRe_.match(text);
    if (match.hasMatch()) {
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::List]);
    }
}

void MarkdownHighlighter::highlightBlockQuotes(const QString& text) {
    QRegularExpressionMatch match = blockQuoteRe_.match(text);
    if (match.hasMatch()) {
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::BlockQuote]);
    }
}

void MarkdownHighlighter::highlightLinks(const QString& text) {
    QRegularExpressionMatchIterator it = linkRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::Link]);
    }
}

void MarkdownHighlighter::highlightImages(const QString& text) {
    QRegularExpressionMatchIterator it = imageRe_.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), elementFormats_[ElementType::Image]);
    }
}

void MarkdownHighlighter::highlightTables(const QString& text) {
    QRegularExpressionMatch match = tableRe_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::Table]);
    }
}

void MarkdownHighlighter::highlightHorizontalRules(const QString& text) {
    QRegularExpressionMatch match = horizontalRuleRe_.match(text);
    if (match.hasMatch()) {
        setFormat(0, text.length(), elementFormats_[ElementType::HorizontalRule]);
    }
}
