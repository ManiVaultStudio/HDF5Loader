#include "HDF5_AD_Loader.h"
#include "FilterTreeModel.h"

#include "H5ADUtils.h"
#include "H5Utils.h"



#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QTreeView>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QtGlobal>

#include <cassert>
#include <iostream>
#include <map>
#include <set>

#include <PointData/PointData.h>
#include <actions/StringAction.h>
//#include <util/StyledIcon.h>

#include "TreeModel.h"

using namespace mv;

namespace local
{
	struct TreeItemRole
	{
		enum { Value = Qt::UserRole, Visible = Qt::UserRole + 1, Sorting = Qt::UserRole+2 };
		enum { Description = 0, Size = 1};
	};
	

	hsize_t hideChildrenAndComputeSize(QStandardItem* currentItem)
	{
		
		hsize_t sum = 0;
		if (currentItem)
		{
			
			for (std::size_t i = 0; i < currentItem->rowCount(); ++i)
			{
				auto size = currentItem->child(i, TreeItemRole::Size)->data(TreeItemRole::Value).toULongLong();
				sum += size;
				currentItem->child(i, TreeItemRole::Description)->setData(false, TreeItemRole::Visible);
				currentItem->child(i, TreeItemRole::Description)->setIcon(util::StyledIcon("user-ninja"));
			}
		}
		return sum;
	}
	void checkParent(QStandardItem* parent, Qt::CheckState checkState)
	{
		if (parent == nullptr)
			return;
		if (checkState == Qt::PartiallyChecked)
			parent->setCheckState(Qt::PartiallyChecked);
		else
		{
			Qt::CheckState stateToCheck = checkState;

			auto rowCount = parent->rowCount();
			bool allChildrenHaveTheSameCheckState = true;
			for (std::size_t i = 0; i < rowCount; ++i)
			{
				if (parent->child(i, 0)->checkState() != stateToCheck)
				{
					parent->setCheckState(Qt::PartiallyChecked);
					allChildrenHaveTheSameCheckState = false;
					break;
				}
			}
			if (allChildrenHaveTheSameCheckState)
				parent->setCheckState(stateToCheck);
		}
	}

	bool isGroup(const H5::H5Object& obj) 
	{
		try 
		{
			// Try to cast the object to a Group
			H5::Group group = dynamic_cast<const H5::Group&>(obj);
			return true; // Successful cast means it is a Group
		}
		catch (std::bad_cast&) {
			return false; // If cast fails, it's not a Group
		}
		return false;
	}

	bool isDataSet(const H5::H5Object& obj)
	{
		try
		{
			// Try to cast the object to a Group
			H5::DataSet dataSet = dynamic_cast<const H5::DataSet&>(obj);
			return true; // Successful cast means it is a Group
		}
		catch (std::bad_cast&) {
			return false; // If cast fails, it's not a Group
		}
		return false;
	}

	QString formatBytes1024_QString(hsize_t bytes) {
		const QStringList units = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
		double value = static_cast<double>(bytes);
		int unitIndex = 0;

		while (value >= 1024 && unitIndex < units.size() - 1) {
			value /= 1024;
			unitIndex++;
		}

		if (unitIndex == 0) { // For bytes, no decimal places
			return QString("%1 %2").arg(static_cast<long long>(value)).arg(units[unitIndex]);
		}
		else {
			return QString("%1 %2").arg(value, 0, 'f', 2).arg(units[unitIndex]);
		}
	}

	// Function to recursively add HDF5 objects to the QStandardItemModel
	bool addHdf5ObjectToModel(const H5::H5Object& obj, hsize_t idx, QStandardItem* parentItem, QStandardItemModel *model = nullptr)
	{
		
		
		if ((parentItem == nullptr) && (model == nullptr))
			return false;

		const bool objIsGroup = isGroup(obj);
		// Get the object name
		auto fullPath = obj.getObjName();
		auto name = std::filesystem::path(fullPath).filename().string();

		QString qName = QString::fromStdString(name);
		if (parentItem && parentItem->text() == "obs")
		{
			if (qName == "_index")
				return false;
		}

		if (parentItem && parentItem->text() == "raw")
		{
			if (qName == "var")
				return false;
			if (qName == "varm")
				return false;
			if (qName == "varp")
				return false;
		}

		bool addObjectToHierarchy = isDataSet(obj);

		static const QString PointsIconName = "database";
		static const QString ClustersIconName = "table-cells-large";
		static const QString UnknownIconName = "circle-question";
		static const QString GroupIconName = "layer-group";
		QString currentItemIconName = (objIsGroup ? GroupIconName :  UnknownIconName);

		
		hsize_t currentItemSize = 0;
		// Check if the object is a group
		if (isDataSet(obj))
		{
			H5::DataSet dataset(obj.getId());
			auto datatype = dataset.getDataType();
			H5T_class_t class_type = datatype.getClass();
			currentItemSize = dataset.getStorageSize();
			

			switch (class_type)
			{
			case H5T_INTEGER:
			case H5T_FLOAT:
			case H5T_ENUM: currentItemIconName = PointsIconName; // point dataset
				break;
			case H5T_STRING: currentItemIconName = ClustersIconName; // cluster dataset
				break;
			}
		}
		

		// Create a new item for the current object
		QStandardItem* currentItem = new QStandardItem(qName);
		currentItem->setCheckable(true);
		currentItem->setData(idx, TreeItemRole::Value);
		currentItem->setData(true, TreeItemRole::Visible);
		currentItem->setData(qName, TreeItemRole::Sorting);

		// Check if the object is a group
		if (objIsGroup)
		{
			
			H5::Group group(obj.getId());

			// Iterate over the members of the group
			hsize_t numObjs = group.getNumObjs();
			for (hsize_t i = 0; i < numObjs; ++i)
			{
				// Get the object by index
				H5std_string objName = group.getObjnameByIdx(i);
				H5O_info_t objInfo;
				group.getObjinfo(objName, objInfo);

				// Open the object based on its type
				if (objInfo.type == H5O_TYPE_GROUP)
				{
					H5::Group subGroup = group.openGroup(objName);
					addObjectToHierarchy |= addHdf5ObjectToModel(subGroup, i, currentItem);
				}
				else if (objInfo.type == H5O_TYPE_DATASET)
				{
					H5::DataSet dataset = group.openDataSet(objName);
					addObjectToHierarchy |= addHdf5ObjectToModel(dataset, i, currentItem);
				}
			}


			if (group.attrExists("shape"))
			{
				H5::Attribute attribute = group.openAttribute("shape");
				H5::DataSpace dataSpaceOfAttribute = attribute.getSpace();
			
				const int dimensions = dataSpaceOfAttribute.getSimpleExtentNdims();
				std::size_t nrOfItems = 1;
				if (dimensions > 0)
				{

					std::vector<hsize_t> dimensionSize(dimensions);
					int ndims = dataSpaceOfAttribute.getSimpleExtentDims(&(dimensionSize[0]), NULL);
					for (std::size_t d = 0; d < dimensions; ++d)
					{
						nrOfItems *= dimensionSize[d];
					}

				}

				std::size_t totalSize = 0;
				if (dimensions == 1)
				{
					std::vector<std::size_t> temp(nrOfItems);
					attribute.read(H5Utils::getH5DataType<std::size_t>(), temp.data());

					std::size_t totalNrOfElements = temp[0];
					for (std::size_t i = 1; i < nrOfItems; ++i)
					{
						totalNrOfElements *= temp[i];
					}
					totalSize = totalNrOfElements;

					if (group.exists("data"))
					{
						H5::DataSet data = group.openDataSet("data");
						H5::DataType datatype = data.getDataType();
						currentItemSize = totalNrOfElements * datatype.getSize();
						auto dummy = local::hideChildrenAndComputeSize(currentItem);
						
					}
				}
			}
			else
			{
				// for some known combination we don't want to show the underlying datasets.
				if (numObjs >= 2)
				{
					// hide categories & codes
					QSet<QString> childNames;
					for (std::size_t i = 0; i < currentItem->rowCount(); ++i)
					{
						childNames.insert(currentItem->child(i, 0)->text());
					}
					if (childNames.size() == 2)
					{
						if (childNames.contains({ "codes","categories" }))
						{
							currentItemSize += local::hideChildrenAndComputeSize(currentItem);

							currentItemIconName = ClustersIconName;
						}
					}
					/*
					else if (childNames.size() == 3)
					{
						if (childNames.contains({ "data","indices","indptr"}))
						{
							currentItemSize += local::hideChildrenAndComputeSize(currentItem);
						}
						currentItemIconName = PointsIconName;

					}
					*/
				}
			}
			
		}
		

		if (addObjectToHierarchy)
		{
			currentItem->setIcon(util::StyledIcon(currentItemIconName));
			

			
			QStandardItem* currentSizeItem = currentItemSize ? new QStandardItem(local::formatBytes1024_QString(currentItemSize)) : new QStandardItem("");
			currentSizeItem->setData(currentItemSize, TreeItemRole::Value);
			currentSizeItem->setData(currentItemSize, TreeItemRole::Sorting);
			if (parentItem)
			{
				parentItem->appendRow({ currentItem, currentSizeItem });
			}
			else if (model)
			{
				std::size_t r = model->rowCount();
				model->appendRow({ currentItem, currentSizeItem });
				
			}
			// do this after adding it to the model otherwise the children don't get checked.
			currentItem->setCheckState( (qName=="obs") ? Qt::Checked : Qt::Unchecked);
			return true;
		}
		else
		{
			delete currentItem;
			
			return false;
		}
	}

	// Recursive function to check all children
	void checkAllChildren(QStandardItem* parentItem, Qt::CheckState checkState) {
		if (!parentItem) {
			return;
		}

		// Set the check state for the current item (optional, depending on your logic)
		// If you only want to set children, remove this line or add a condition.
		// parentItem->setCheckState(checkState);

		for (int i = 0; i < parentItem->rowCount(); ++i) {
			QStandardItem* childItem = parentItem->child(i);
			if (childItem) {
				childItem->setCheckState(checkState); // Set the check state for the child
				if (childItem->hasChildren()) {
					checkAllChildren(childItem, checkState); // Recurse for grand-children
				}
			}
		}
	}
}
HDF5_AD_Loader::HDF5_AD_Loader(mv::CoreInterface *core)
{
	_core = core;
}

bool HDF5_AD_Loader::open(const QString& fileName)
{
	try
	{
		_file.reset(new H5::H5File(fileName.toLatin1().constData(), H5F_ACC_RDONLY));
		auto nrOfObjects = _file->getNumObjs();
		bool dataFound = false;

		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			if (objectName1 == "X")
			{
				dataFound = true;
				
			}
			else if(objectName1 == "var")
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);
					_var_indexName = H5AD::LoadIndexStrings(dataset, _dimensionNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					_var_indexName = H5AD::LoadIndexStrings(group, _dimensionNames);
				}
			}
			else if (objectName1 == "obs")
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);
					_obs_indexName = H5AD::LoadIndexStrings(dataset, _sampleNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					_obs_indexName = H5AD::LoadIndexStrings(group, _sampleNames);
				}
			}
		}
		
		if(dataFound && (!_dimensionNames.empty()))
		{
			_fileName = fileName;
			return true;
		}
	}
	catch (...)
	{
		_fileName.clear();
		_file->close();
		_file.release();
	}
	return false;
}

 const std::vector<QString>& HDF5_AD_Loader::getDimensionNames() const
{
	return _dimensionNames;
}

 template<typename T>
 void filterValues(T& values, const std::vector<std::ptrdiff_t>& dimensionIndices)
 {
	if(dimensionIndices.empty())
		return;
	assert(values.size() == dimensionIndices.size());
	const auto nrOfSelectedIdices = std::count_if(dimensionIndices.cbegin(), dimensionIndices.cend(), [](std::ptrdiff_t i) { return (i >= 0); });
	T selectedValues(nrOfSelectedIdices);
	#pragma omp parallel for
	 for (std::ptrdiff_t i = 0; i < dimensionIndices.size(); ++i)
	 {
		 auto index = dimensionIndices[i];
		 if (index >= 0)
		 {
			 auto value = values[i];
			 assert(index < nrOfSelectedIdices);
			 selectedValues[index] = value;
		 }
		 
	 }
	 values = std::move(selectedValues);
 }

 
static void LoadDimensionProperties(H5::DataSet dataset,  H5AD::LoaderInfo &loaderInfo)
 {
	 
	 if (loaderInfo._pointsDataset.isValid())
	 {
		 QVariantMap propertyMap;

		 auto datasetClass = dataset.getDataType().getClass();
		 if (datasetClass == H5T_COMPOUND)
		 {

			 const auto nrOfOriginalDimensions = loaderInfo._originalDimensionNames.size();
			 std::map<std::string, std::vector<QVariant> > compoundMap;
			 if (H5Utils::read_compound(dataset, compoundMap))
			 {
				 for (auto component = compoundMap.begin(); component != compoundMap.end(); ++component)
				 {
					 if (component->second.size())
					 {
						 filterValues(component->second, loaderInfo._selectedDimensionsLUT);
						 propertyMap[component->first.c_str()] = QVariantList(component->second.cbegin(), component->second.cend());
					 }
				 }
			 }
		 }
		 QVariantMap dimensionPropertyMap = loaderInfo._pointsDataset->getProperty("Dimension").toMap();
		 dimensionPropertyMap[dataset.getObjName().c_str()] = propertyMap;
		 loaderInfo._pointsDataset->setProperty("Dimension", dimensionPropertyMap);
	 }
	 
	
 }

static void LoadDimensionProperties(H5::Group &group,  H5AD::LoaderInfo &loaderInfo)
 {
	if (loaderInfo._pointsDataset.isValid())
	{
		QString name = group.getObjName().c_str();
		if (name.isEmpty())
			return;
		if (name[0] == '/')
			name.remove(0, 1);
		QVariantMap propertyMap;

		std::map<std::string, std::vector<QString>> categories;
		H5AD::LoadCategories(group, categories);

		const auto nrOfOriginalDimensions = loaderInfo._originalDimensionNames.size();
		const auto nrOfObjects = group.getNumObjs();
		for (auto go = 0; go < nrOfObjects; ++go)
		{
			std::string objectName1 = group.getObjnameByIdx(go);
			H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataSet = group.openDataSet(objectName1);
				auto datasetClass = dataSet.getDataType().getClass();

				if (datasetClass == H5T_FLOAT)
				{
					std::vector<float> values;
					if (H5Utils::read_vector(group, objectName1, &values))
					{
						if (values.size())
						{
							filterValues(values, loaderInfo._selectedDimensionsLUT);
							propertyMap[objectName1.c_str()] = QVariantList(values.cbegin(), values.cend());
						}
					}
				}
				else if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_ENUM))
				{
					std::vector<qlonglong> values;
					if (H5Utils::read_vector(group, objectName1, &values))
					{
						if (values.size())
						{
							QString label = objectName1.c_str();
							QVariantList labels;
							if (dataSet.attrExists("categories"))
							{
								if (categories.count(objectName1))
								{
									const std::vector<QString>& category_labels = categories[objectName1];
									labels.resize(values.size());
									for (qsizetype i = 0; i < labels.size(); ++i)
									{
										qsizetype index = values[i];
										if (index < category_labels.size())
										{
											labels[i] = category_labels[index];
										}
										else
										{
											labels.clear();
											break;
										}
									}
								}
							}
							if (labels.isEmpty())
							{
								if (values.size())
								{
									filterValues(values, loaderInfo._selectedDimensionsLUT);
									propertyMap[label] = QVariantList(values.cbegin(), values.cend());
									if (datasetClass == H5T_ENUM)
									{
										auto enumType = dataSet.getEnumType();
										labels.reserve(values.size());
										for (std::size_t i = 0; i < values.size(); ++i)
										{
											int v = values[i];
											labels.push_back(enumType.nameOf(&v, 100).c_str());
										}
										QString label = objectName1.c_str();
										label += " (label)";
										if (labels.size())
										{
											filterValues(labels, loaderInfo._selectedDimensionsLUT);
											propertyMap[label] = labels;
										}
									}
								}
							}
							else
							{
								if (values.size())
								{
									filterValues(values, loaderInfo._selectedDimensionsLUT);
									propertyMap[label] = labels;
								}
							}

						}
					}
				}
				else if (datasetClass == H5T_STRING)
				{
					// try to read as strings
					std::vector<QString> values;
					H5Utils::read_vector_string(dataSet, values);
					if (values.size())
					{
						filterValues(values, loaderInfo._selectedDimensionsLUT);
						propertyMap[objectName1.c_str()] = QVariantList(values.cbegin(), values.cend());
					}
				}
			}
			else if (objectType1 == H5G_GROUP)
			{
				H5::Group subgroup = group.openGroup(objectName1);
				std::map<QString, std::vector<unsigned>> codedCategories;

				if (H5AD::LoadCodedCategories(subgroup, codedCategories))
				{
					std::size_t count = 0;
					for (auto cat_it = codedCategories.cbegin(); cat_it != codedCategories.cend(); ++cat_it)
						count += cat_it->second.size();
					if (count != nrOfOriginalDimensions)
						std::cout << "WARNING: " << "not all dimensions are accounted for" << std::endl;
					//				 else
					{
						QVariantList values(count);
						for (auto cat_it = codedCategories.cbegin(); cat_it != codedCategories.cend(); ++cat_it)
						{
							for (auto idx_it = cat_it->second.cbegin(); idx_it != cat_it->second.cend(); ++idx_it)
							{

								if (values[*idx_it].isNull())
								{
									values[*idx_it] = cat_it->first;
								}
								else
								{
									values.clear();
									idx_it = cat_it->second.cend();
									cat_it = codedCategories.cend();
									break;
								}
							}

						}
						if (values.size())
						{
							filterValues(values, loaderInfo._selectedDimensionsLUT);
							propertyMap[objectName1.c_str()] = values;
						}
					}
				}
			}
		}

		QVariantMap dimensionPropertyMap = loaderInfo._pointsDataset->getProperty("Dimension").toMap();
		dimensionPropertyMap[name] = propertyMap;
		loaderInfo._pointsDataset->setProperty("Dimension", dimensionPropertyMap);
	}
 }




bool HDF5_AD_Loader::load(int storageType)
{
	
	if (_file == nullptr)
		return false;
	try
	{
		auto nrOfObjects = _file->getNumObjs();

		auto  model = H5Utils::CreateTreeModel();
		H5Utils::BuildTreeModel(model, _file.get());
		/*
		QStandardItemModel model;
		model.setColumnCount(2);
		model.setHorizontalHeaderLabels({ "Name", "Estimated Size" });

		// Connect the itemChanged signal to handle checking
		QObject::connect(&model, &QStandardItemModel::itemChanged, [&](QStandardItem* item) {
			// When an item's check state changes, we want to update its children
			if (item->isCheckable()) 
			{
				if(item->checkState() != Qt::PartiallyChecked)
					local::checkAllChildren(item, item->checkState());
		        
				local::checkParent(item->parent(), item->checkState());
			}
			});
			*/
		

		//QStandardItem* rootItem = new QStandardItem("/");
		//model.appendRow(rootItem);
		/*
		std::set<std::string> ignoreItems = { "X", "uns", "var", "varm", "varp"};
		std::map<std::string, std::size_t> objectNameIdx;
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			objectNameIdx[objectName1] = fo;
			if(ignoreItems.count(objectName1)==0)
			{
				
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet h5Dataset = _file->openDataSet(objectName1);
					std::size_t r = model.rowCount();
					local::addHdf5ObjectToModel(h5Dataset, fo, nullptr, &model);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group h5Group = _file->openGroup(objectName1);
					std::size_t r = model.rowCount();
					local::addHdf5ObjectToModel(h5Group, fo, nullptr, &model);
				}
			}
		}
		*/
		//std::set<std::string> objectsToProcess;

		QString pointDatasetLabel;
		bool filterUniqueProperties = false;
		{
			FilterTreeModel filterModel(nullptr);
			filterModel.setSourceModel(model.get());
			filterModel.setRecursiveFilteringEnabled(true);
			filterModel.setSortRole(local::TreeItemRole::Sorting);

			int line = 0;
			QDialog dialog(nullptr);
			dialog.setMinimumWidth(400);
			QGridLayout* layout = new QGridLayout;
			QLineEdit* lineEdit = new QLineEdit(QFileInfo(_fileName).baseName());
			layout->addWidget(new QLabel("Dataset Name: "), line, 0);
			layout->addWidget(lineEdit, line++, 1);

			layout->addWidget(new QLabel("Try to load:"), line++, 0, 1, 2);

			QTreeView* h5View = new QTreeView;

			
			h5View->setModel(&filterModel);
			h5View->expandAll();
			h5View->setSortingEnabled(true); h5View->expandAll();

			
			
			h5View->header()->setSectionResizeMode(local::TreeItemRole::Description, QHeaderView::Stretch);
			h5View->header()->setSectionResizeMode(local::TreeItemRole::Size, QHeaderView::Fixed);
			h5View->header()->setStretchLastSection(false);

			h5View->header()->resizeSection(local::TreeItemRole::Description, 320);
			h5View->header()->resizeSection(local::TreeItemRole::Size, 80);
			
			//listView->setSelectionMode(QListView::MultiSelection);
			layout->addWidget(h5View, line++, 0, 1, 2);

			mv::gui::StringAction filterAction(&dialog, "Filter on name");
			filterAction.setSearchMode(true);
			filterAction.setClearable(true);
			filterAction.setPlaceHolderString("Filter on Name");
			QObject::connect(&filterAction, &mv::gui::StringAction::stringChanged, &filterModel, [&filterModel](const QString& text) {filterModel.setFilterRegularExpression(text); });
			QWidget* filterWidget = filterAction.createWidget(&dialog);
			layout->addWidget(filterWidget, line++, 0, 1, 2);
			
			layout->addWidget(new QLabel(), line++, 0, 1, 2);
			QCheckBox* filterUniquePropertiesCheckBox = new QCheckBox("Filter Unique Properties");
			layout->addWidget(filterUniquePropertiesCheckBox, line++, 0, 1, 2);

			auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
			buttonBox->connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
			layout->addWidget(buttonBox, line++, 0, 1, 2);

			

			dialog.setLayout(layout);

			auto result = dialog.exec();

			if (result == 0)
				return false;

			pointDatasetLabel = lineEdit->displayText();

			filterUniqueProperties = filterUniquePropertiesCheckBox->isChecked();
		}
		
		// create and setup the pointsDataset here since we have access to _core, _filename and storageType here.
		
		Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, false,  pointDatasetLabel);
		pointsDataset->getDataHierarchyItem().setVisible(false);
	
		// first load the main data matrix
		H5AD::LoaderInfo loaderInfo;
		
		loaderInfo._pointsDataset = pointsDataset;
		loaderInfo._originalDimensionNames = _dimensionNames;
		loaderInfo._sampleNames = QVariantList(_sampleNames.cbegin(), _sampleNames.cend());
		loaderInfo._filterUniqueProperties = filterUniqueProperties;

		if (!H5AD::load_X(_file, loaderInfo, storageType))
		{
			mv::data().removeDataset(pointsDataset);
			_dimensionNames.clear();
			_sampleNames.clear();
			return false;
		}
		
		//_dimensionNames = pointsDataset->getDimensionNames();
		
		// now we look for nice to have annotation for the observations in the main data matrix
		try
		{
			std::size_t x = model->rowCount();
			for (hsize_t i = 0; i < model->rowCount(); ++i)
			{
				
				loaderInfo.currentItem = model->item(i, 0);
				if (loaderInfo.currentItem)
				{
					hsize_t idx = loaderInfo.currentItem->data(Qt::UserRole).toULongLong();
					std::string objectName1 = _file->getObjnameByIdx(idx);
					H5G_obj_t objectType1 = _file->getObjTypeByIdx(idx);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet h5Dataset = _file->openDataSet(objectName1);
						H5AD::LoadSampleNamesAndMetaDataFloat(h5Dataset, loaderInfo);
					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group h5Group = _file->openGroup(objectName1);
						H5AD::LoadSampleNamesAndMetaDataFloat(h5Group, loaderInfo);
					}
				}
				else
				{
					assert(false); // this should never happen
				}
			}
			/*
			auto nrOfObjects = _file->getNumObjs();
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				std::string objectName1 = _file->getObjnameByIdx(fo);
			
				if (objectsToProcess.count(objectName1) == 1)
				{
					H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet h5Dataset = _file->openDataSet(objectName1);
						H5AD::LoadSampleNamesAndMetaDataFloat(h5Dataset, loaderInfo);
					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group h5Group = _file->openGroup(objectName1);
						H5AD::LoadSampleNamesAndMetaDataFloat(h5Group, loaderInfo);
					}
				}
			}
			*/
			// var can contain dimension specific information so for now we store it as properties.
		//	auto varIdx = objectNameIdx.find("var");
			auto nrOfObjects = _file->getNumObjs();
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				std::string objectName1 = _file->getObjnameByIdx(fo);
				
				if (objectName1 == "var")
				{
					H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet h5Dataset = _file->openDataSet("var");
						LoadDimensionProperties(h5Dataset, loaderInfo);

					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group h5Group = _file->openGroup("var");
						LoadDimensionProperties(h5Group, loaderInfo);
					}
					break;
				}
			}
			

			events().notifyDatasetDataChanged(pointsDataset);

			pointsDataset->getDataHierarchyItem().setVisible(true);
			return true;

		}
		catch(const std::exception &e)
		{
			mv::data().removeDataset(pointsDataset);
		}
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	return false;
}

