#ifndef MARKDOWNHIGHLIGHTER_H
#define MARKDOWNHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument* parent = nullptr);

    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    enum class ElementType {
        Header1,
        Header2,
        Header3,
        Header4,
        Header5,
        Header6,
        Bold,
        Italic,
        BoldItalic,
        Code,
        CodeBlock,
        Link,
        Image,
        List,
        OrderedList,
        BlockQuote,
        HorizontalRule,
        Table
    };

    void setFormatForElement(ElementType type, const QTextCharFormat& format);
    QTextCharFormat formatForElement(ElementType type) const;

protected:
    void highlightBlock(const QString& text) override;

private:
    void setupDefaultFormats();
    void setupRules();
    void highlightBlockWithRules(const QString& text);
    void highlightHeaders(const QString& text);
    void highlightCodeBlocks(const QString& text);
    void highlightInlineElements(const QString& text);
    void highlightLists(const QString& text);
    void highlightBlockQuotes(const QString& text);
    void highlightLinks(const QString& text);
    void highlightImages(const QString& text);
    void highlightTables(const QString& text);
    void highlightHorizontalRules(const QString& text);

    QVector<HighlightingRule> highlightingRules_;
    QMap<ElementType, QTextCharFormat> elementFormats_;

    QRegularExpression header1Re_;
    QRegularExpression header2Re_;
    QRegularExpression header3Re_;
    QRegularExpression header4Re_;
    QRegularExpression header5Re_;
    QRegularExpression header6Re_;
    QRegularExpression boldRe_;
    QRegularExpression italicRe_;
    QRegularExpression boldItalicRe_;
    QRegularExpression codeRe_;
    QRegularExpression codeBlockStartRe_;
    QRegularExpression linkRe_;
    QRegularExpression imageRe_;
    QRegularExpression listRe_;
    QRegularExpression orderedListRe_;
    QRegularExpression blockQuoteRe_;
    QRegularExpression horizontalRuleRe_;
    QRegularExpression tableRe_;

    bool inCodeBlock_;
};

#endif // MARKDOWNHIGHLIGHTER_H
