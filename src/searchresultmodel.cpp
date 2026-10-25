#include "searchresultmodel.h"

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractListModel(parent) {
}

void SearchResultModel::setResults(const QVector<SearchResult>& results) {
    beginResetModel();
    results_ = results;
    endResetModel();
    emit resultsChanged();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
    emit resultsChanged();
}

QVector<SearchResult> SearchResultModel::results() const {
    return results_;
}

SearchResult SearchResultModel::resultAt(int row) const {
    if (row >= 0 && row < results_.size()) {
        return results_[row];
    }
    return SearchResult();
}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return results_.size();
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= results_.size()) {
        return QVariant();
    }

    const SearchResult& result = results_[index.row()];

    switch (role) {
        case NoteIdRole:
            return result.noteId;
        case TitleRole:
            return result.title;
        case SnippetRole:
            return result.snippet;
        case RelevanceScoreRole:
            return result.relevanceScore;
        case ModifiedTimeRole:
            return result.modifiedTime.toString(Qt::ISODate);
        case Qt::DisplayRole:
            return result.title;
        case Qt::ToolTipRole:
            return result.snippet;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> SearchResultModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NoteIdRole] = "noteId";
    roles[TitleRole] = "title";
    roles[SnippetRole] = "snippet";
    roles[RelevanceScoreRole] = "relevanceScore";
    roles[ModifiedTimeRole] = "modifiedTime";
    return roles;
}
