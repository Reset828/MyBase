#include "searchengine.h"
#include <QRegularExpression>
#include <QtConcurrent>

SearchEngine::SearchEngine() : snippetContextLength_(30) {
}

SearchEngine::SearchEngine(ISearchRepository& searchRepository)
    : searchRepository_(&searchRepository), snippetContextLength_(30) {
}

QString SearchEngine::buildFTS5Query(const QString& keywords) const {
    if (keywords.trimmed().isEmpty()) {
        return QString();
    }

    QStringList terms = keywords.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList ftsTerms;

    for (const QString& term : terms) {
        QString escaped = escapeFTS5Query(term);
        ftsTerms.append(escaped);
    }

    return ftsTerms.join(" AND ");
}

QString SearchEngine::escapeFTS5Query(const QString& input) const {
    QString escaped = input;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

QVector<SearchResult> SearchEngine::searchNotes(const QString& query) {
    return searchRepository_ ? searchRepository_->searchNotes(buildFTS5Query(query), snippetContextLength_)
                             : QVector<SearchResult>{};
}

QVector<SearchResult> SearchEngine::combinedSearch(const QString& query,
                                                    const QSet<qint64>& tagIds,
                                                    TagMatchMode matchMode) {
    const QString ftsQuery = query.trimmed().isEmpty() ? QString() : buildFTS5Query(query);
    if (!searchRepository_) return {};
    if (tagIds.isEmpty()) {
        return ftsQuery.isEmpty()
            ? QVector<SearchResult>{}
            : searchRepository_->searchNotes(ftsQuery, snippetContextLength_);
    }
    return searchRepository_->searchNotesByTags(ftsQuery, tagIds, matchMode, snippetContextLength_);
}

QFuture<QVector<SearchResult>> SearchEngine::combinedSearchAsync(const QString& query,
                                                                 const QSet<qint64>& tagIds,
                                                                 TagMatchMode matchMode) {
    ISearchRepository* const repository = searchRepository_;
    const QString ftsQuery = query.trimmed().isEmpty() ? QString() : buildFTS5Query(query);
    const int snippetContextLength = snippetContextLength_;
    return QtConcurrent::run([repository, ftsQuery, tagIds, matchMode, snippetContextLength]() {
        if (!repository) return QVector<SearchResult>{};
        if (tagIds.isEmpty()) {
            if (ftsQuery.isEmpty()) {
                return QVector<SearchResult>{};
            }
            return repository->searchNotes(ftsQuery, snippetContextLength);
        }
        return repository->searchNotesByTags(ftsQuery, tagIds, matchMode, snippetContextLength);
    });
}
QFuture<QVector<SearchResult>> SearchEngine::searchNotesAsync(const QString& query) {
    ISearchRepository* const repository = searchRepository_;
    const QString ftsQuery = buildFTS5Query(query);
    const int snippetContextLength = snippetContextLength_;
    return QtConcurrent::run([repository, ftsQuery, snippetContextLength]() {
        if (!repository || ftsQuery.isEmpty()) {
            return QVector<SearchResult>{};
        }
        return repository->searchNotes(ftsQuery, snippetContextLength);
    });
}

