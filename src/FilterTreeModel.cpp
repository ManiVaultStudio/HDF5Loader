#include "FilterTreeModel.h"

bool FilterTreeModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	enum { ItemIndex = Qt::UserRole, ItemVisible = Qt::UserRole + 1 };
	QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
	if (!index.isValid())
	{
		int bp = 0;
		bp++;
	}
	QVariant data = sourceModel()->data(index, ItemVisible);
	if (!data.toBool())
		return false;
	return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

