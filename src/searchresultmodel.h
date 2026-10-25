#ifndef SEARCHRESULTMODEL_H
#define SEARCHRESULTMODEL_H

#include <QAbstractListModel>
#include <QVector>
#include "searchresult.h"

class SearchResultModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum SearchResultRoles {
        NoteIdRole = Qt::UserRole + 1,
        TitleRole,
        SnippetRole,
        RelevanceScoreRole,
        ModifiedTimeRole
    };

    explicit SearchResultModel(QObject* parent = nullptr);

    void setResults(const QVector<SearchResult>& results);
    void clear();
    QVector<SearchResult> results() const;
    SearchResult resultAt(int row) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void resultsChanged();

private:
    QVector<SearchResult> results_;
};

#endif // SEARCHRESULTMODEL_H
