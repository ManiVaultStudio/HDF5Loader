#include "FilterTreeModel.h"

bool FilterTreeModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	enum { Value = Qt::UserRole + 1, Visible = Qt::UserRole + 2, Sorting = Qt::UserRole + 3 };
	QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
	if (!index.isValid())
	{
		int bp = 0;
		bp++;
	}
	QVariant data = sourceModel()->data(index, Visible);
	if (!data.toBool())
		return false;
	return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

