#pragma once

#include <QStandardItemModel>
#include <QStandardItem>
#include <QSharedPointer>
#include <QString>

#include "H5Cpp.h"

namespace H5Utils
{
	using TreeModel = QStandardItemModel;


	// function to create and initialize the TreeModel
	QSharedPointer<TreeModel> CreateTreeModel();

	void BuildTreeModel(QSharedPointer<TreeModel> model, H5::H5File* file);

}
