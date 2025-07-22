#pragma once

#include <QSortFilterProxyModel>

class FilterTreeModel : public QSortFilterProxyModel
{
	Q_OBJECT
public:
	explicit FilterTreeModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) 
	{
		int here = 0;
		here++;
	}

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
	
};