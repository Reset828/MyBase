#include "markdownoutlinemodel.h"

#include <QRegularExpression>
#include <QStandardItem>

MarkdownOutlineModel::MarkdownOutlineModel(QObject* parent)
    : QStandardItemModel(parent) {
    setHorizontalHeaderLabels({QStringLiteral("文档大纲")});
}

void MarkdownOutlineModel::setMarkdown(const QString& markdown) {
    removeRows(0, rowCount());
    QVector<QStandardItem*> parents(7, nullptr);
    bool inFence = false;
    QChar fenceCharacter;
    int fenceLength = 0;
    int position = 0;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression headingPattern(
        QStringLiteral(R"(^ {0,3}(#{1,6})[ \t]+(.+?)[ \t]*#*[ \t]*$)"));

    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        const int markerLength = trimmed.startsWith(QStringLiteral("~~~")) ? 3
                               : trimmed.startsWith(QStringLiteral("```")) ? 3 : 0;
        if (markerLength > 0) {
            const QChar marker = trimmed.front();
            if (!inFence) {
                inFence = true;
                fenceCharacter = marker;
                fenceLength = 0;
                while (fenceLength < trimmed.size() && trimmed.at(fenceLength) == marker) {
                    ++fenceLength;
                }
            } else if (marker == fenceCharacter) {
                int closingLength = 0;
                while (closingLength < trimmed.size() && trimmed.at(closingLength) == marker) {
                    ++closingLength;
                }
                if (closingLength >= fenceLength) {
                    inFence = false;
                }
            }
            position += line.size() + 1;
            continue;
        }

        if (!inFence) {
            const QRegularExpressionMatch match = headingPattern.match(line);
            if (match.hasMatch()) {
                const int level = match.captured(1).size();
                const QString title = match.captured(2).trimmed();
                if (!title.isEmpty()) {
                    auto* item = new QStandardItem(title);
                    item->setEditable(false);
                    item->setData(position, PositionRole);
                    item->setData(level, LevelRole);
                    int parentLevel = level - 1;
                    while (parentLevel > 0 && !parents[parentLevel]) {
                        --parentLevel;
                    }
                    if (parentLevel == 0) {
                        appendRow(item);
                    } else {
                        parents[parentLevel]->appendRow(item);
                    }
                    for (int index = level; index < parents.size(); ++index) {
                        parents[index] = nullptr;
                    }
                    parents[level] = item;
                }
            }
        }
        position += line.size() + 1;
    }
}
