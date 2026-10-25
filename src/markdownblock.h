#ifndef MARKDOWNBLOCK_H
#define MARKDOWNBLOCK_H

#include <QList>
#include <QString>

struct MarkdownBlock {
    enum Type { Paragraph, Header, List, CodeBlock, Table, Blockquote, HorizontalRule, EmptyLine };
    Type type;
    QString content;
    int level;
    QList<QString> cells;
    bool isOrdered;
    QString lang;
    bool isTask;
    bool taskChecked;
};

#endif // MARKDOWNBLOCK_H
