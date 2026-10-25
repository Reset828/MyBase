#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include <QVector>
#include <QString>
#include <QSet>
#include <QFuture>
#include "searchresult.h"
#include "repositories.h"

class SearchEngine {
public:
    SearchEngine();
    explicit SearchEngine(ISearchRepository& searchRepository);

    QString buildFTS5Query(const QString& keywords) const;

    QVector<SearchResult> searchNotes(const QString& query);

    QVector<SearchResult> combinedSearch(const QString& query, const QSet<qint64>& tagIds,
                                         TagMatchMode matchMode = TagMatchMode::And);

    QFuture<QVector<SearchResult>> combinedSearchAsync(const QString& query,
                                                        const QSet<qint64>& tagIds,
                                                        TagMatchMode matchMode = TagMatchMode::And);

    QFuture<QVector<SearchResult>> searchNotesAsync(const QString& query);

    int getSnippetContextLength() const { return snippetContextLength_; }
    void setSnippetContextLength(int length) { snippetContextLength_ = length; }

private:
    ISearchRepository* searchRepository_ = nullptr;
    int snippetContextLength_;

    QString escapeFTS5Query(const QString& input) const;
};

#endif // SEARCHENGINE_H

