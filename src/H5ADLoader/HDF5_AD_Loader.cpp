#include "HDF5_AD_Loader.h"

#include "H5ADUtils.h"

#include "H5Utils.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QListView>
#include <QDialogButtonBox>
#include <QtGlobal>

#include <cassert>
#include <iostream>
#include <map>
#include <set>

#include <PointData/PointData.h>

using namespace mv;

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

 
static void LoadProperties(H5::DataSet dataset, H5AD::LoaderInfo &datasetInfo)
 {
	 auto datasetClass = dataset.getDataType().getClass();
	 QVariantMap propertyMap;
	 if (datasetClass == H5T_COMPOUND)
	 {
		 
		 const auto nrOfOriginalDimensions = datasetInfo._originalDimensionNames.size();
		 std::map<std::string, std::vector<QVariant> > compoundMap;
		 if (H5Utils::read_compound(dataset, compoundMap))
		 {
			 for (auto component = compoundMap.begin(); component != compoundMap.end(); ++component)
			 {
				if(component->second.size())
				{
					filterValues(component->second, datasetInfo._selectedDimensionsLUT);
					propertyMap[component->first.c_str()] =  QVariantList(component->second.cbegin(), component->second.cend());
				}
			 }
		 }
	 }
	 datasetInfo._pointsDataset->setProperty(dataset.getObjName().c_str(), propertyMap);
	
 }

static void LoadProperties(H5::Group &group, H5AD::LoaderInfo &datasetInfo)
 {
	 QString name = group.getObjName().c_str();
	 if (name.isEmpty())
		 return;
	 if (name[0] == '/')
		 name.remove(0, 1);
	 QVariantMap propertyMap;

	 std::map<std::string, std::vector<QString>> categories;
	 H5AD::LoadCategories(group, categories);

	 const auto nrOfOriginalDimensions = datasetInfo._originalDimensionNames.size();
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
					 	filterValues(values, datasetInfo._selectedDimensionsLUT);
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
								 filterValues(values, datasetInfo._selectedDimensionsLUT);
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
										 // it shouldn't be needed to filter the labels since they are based on the values which where already filtered.
										 propertyMap[label] = labels;
									 }
								 }
							 }
						 }
						 else
						 {
							 if (values.size())
							 {
								 filterValues(values, datasetInfo._selectedDimensionsLUT);
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
					 filterValues(values, datasetInfo._selectedDimensionsLUT);
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
						 filterValues(values, datasetInfo._selectedDimensionsLUT);
						 propertyMap[objectName1.c_str()] = values;
					 }
				 }
			 }
		 }
	 }
	 
	 datasetInfo._pointsDataset->setProperty(name, propertyMap);
	
 }

bool HDF5_AD_Loader::load(int storageType)
{
	
	if (_file == nullptr)
		return false;
	try
	{
		auto nrOfObjects = _file->getNumObjs();
		QStandardItemModel model;
		std::set<std::string> ignoreItems = { "X", "uns", "var", "varm", "varp"};
		std::map<std::string, std::size_t> objectNameIdx;
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			objectNameIdx[objectName1] = fo;
			if(ignoreItems.count(objectName1)==0)
			{
				QStandardItem* item = new QStandardItem(objectName1.c_str());
				item->setCheckable(true);
				item->setCheckState(objectName1.rfind("obs", 0) == 0 ? Qt::Checked : Qt::Unchecked);
				model.appendRow(item);
			}
		}

		std::set<std::string> objectsToProcess;

		QString pointDatasetLabel;
		{
			QDialog dialog(nullptr);
			QGridLayout* layout = new QGridLayout;
			QLineEdit* lineEdit = new QLineEdit(QFileInfo(_fileName).baseName());
			layout->addWidget(new QLabel("Dataset Name: "), 0, 0);
			layout->addWidget(lineEdit, 0, 1);
			layout->addWidget(new QLabel("Load:"), 1, 0, 1, 2);
			QListView* listView = new QListView;
			listView->setModel(&model);
			listView->setSelectionMode(QListView::MultiSelection);
			layout->addWidget(listView, 2, 0, 1, 2);
			auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
			buttonBox->connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
			layout->addWidget(buttonBox, 3, 0, 1, 2);

			dialog.setLayout(layout);

			auto result = dialog.exec();

			if (result == 0)
				return false;

			pointDatasetLabel = lineEdit->displayText();

			for (hsize_t i = 0; i < model.rowCount(); ++i)
			{
				auto* item = model.item(i, 0);
				if (item->checkState() == Qt::Checked)
					objectsToProcess.insert(item->data(Qt::DisplayRole).toString().toLocal8Bit().data());
			}
		}
		
		// create and setup the pointsDataset here since we have access to _core, _filename and storageType here.
		
		Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, false,  pointDatasetLabel);
		pointsDataset->getDataHierarchyItem().setVisible(false);
	
		// first load the main data matrix
		H5AD::LoaderInfo loaderInfo;
		
		loaderInfo._pointsDataset = pointsDataset;
		loaderInfo._originalDimensionNames = _dimensionNames;
		loaderInfo._sampleNames = QVariantList(_sampleNames.cbegin(), _sampleNames.cend());

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
			// var can contain dimension specific information so for now we store it as properties.
			auto varIdx = objectNameIdx.find("var");
			if (varIdx != objectNameIdx.end())
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(varIdx->second);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet h5Dataset = _file->openDataSet("var");
					LoadProperties(h5Dataset, loaderInfo);

				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group h5Group = _file->openGroup("var");
					LoadProperties(h5Group, loaderInfo);
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

