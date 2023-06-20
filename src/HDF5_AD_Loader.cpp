#include "HDF5_AD_Loader.h"
#include "H5ADUtils.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QListView>
#include <QDialogButtonBox>
#include "H5Utils.h"
#include "DataContainerInterface.h"
#include <iostream>

#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"

#include <set>

#include "DataContainerInterface.h"
#include <omp.h>

using namespace hdps;




HDF5_AD_Loader::HDF5_AD_Loader(hdps::CoreInterface *core)
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
					H5AD::LoadIndexStrings(dataset, _dimensionNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					H5AD::LoadIndexStrings(group, _dimensionNames);
				}
			}
			else if (objectName1 == "obs")
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);
					H5AD::LoadIndexStrings(dataset, _sampleNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					H5AD::LoadIndexStrings(group, _sampleNames);
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


 

bool HDF5_AD_Loader::load(int storageType)
{
	
	if (_file == nullptr)
		return false;
	try
	{
		

		auto nrOfObjects = _file->getNumObjs();
		QStandardItemModel model;
		std::set<std::string> ignoreItems = { "X", "uns", "var", "varm", "varp"};
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			if(ignoreItems.count(objectName1)==0)
			{
				QStandardItem* item = new QStandardItem(objectName1.c_str());
				item->setCheckable(true);
				item->setCheckState(objectName1.rfind("obs", 0) == 0 ? Qt::Checked : Qt::Unchecked);
				model.appendRow(item);
			}
		}

		QDialog dialog(nullptr);
		QGridLayout* layout = new QGridLayout;
		QLineEdit* lineEdit = new QLineEdit(QFileInfo(_fileName).baseName());
		layout->addWidget(new QLabel("Dataset Name: "), 0, 0);
		layout->addWidget(lineEdit, 0, 1);
		layout->addWidget(new QLabel("Loaded parts:"), 1, 0, 1, 2);
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
		// create and setup the pointsDataset here since we have access to _core, _filename and storageType here.
		Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, false, lineEdit->displayText());
		if (storageType == 2)
			pointsDataset->setDataElementType<biovault::bfloat16_t>();
		else
			pointsDataset->setDataElementType<float>();

		std::set<std::string> objectsToProcess;
		if(result)
		{
			for(hsize_t i=0; i < model.rowCount(); ++i)
			{
				auto* item = model.item(i, 0);
				if (item->checkState() == Qt::Checked)
					objectsToProcess.insert(item->data(Qt::DisplayRole).toString().toLocal8Bit().data());
			}
		}
		// first load the main data matrix
		if (!H5AD::load_X(_file,pointsDataset))
			return false;
		assert(pointsDataset->getNumDimensions() == _dimensionNames.size());
		pointsDataset->setDimensionNames(_dimensionNames);
		pointsDataset->setProperty("Sample Names", QList<QVariant>(_sampleNames.cbegin(), _sampleNames.cend()));

		//std::set<std::string> objectsToSkip = { "X", "var", "uns", "varm," "raw.X", "raw.var", "raw"};


		//std::set<std::string> objectsToProcess = { "obs","obsm","obsp" };
		// now we look for nice to have annotation for the observations in the main data matrix
		std::map<std::string, hsize_t> indexOfObject;
		try
		{
			auto nrOfObjects = _file->getNumObjs();
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				std::string objectName1 = _file->getObjnameByIdx(fo);
				indexOfObject[objectName1] = fo;
				if (objectsToProcess.count(objectName1) == 1)
				{
					H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet h5Dataset = _file->openDataSet(objectName1);
						if(storageType == 2)
							H5AD::LoadSampleNamesAndMetaData<biovault::bfloat16_t>(h5Dataset, pointsDataset, _core);
						else
							H5AD::LoadSampleNamesAndMetaData<float>(h5Dataset, pointsDataset, _core);
					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group h5Group = _file->openGroup(objectName1);
						if (storageType == 2)
							H5AD::LoadSampleNamesAndMetaData<biovault::bfloat16_t>(h5Group, pointsDataset, _core);
						else
							H5AD::LoadSampleNamesAndMetaData<float>(h5Group, pointsDataset, _core);
					}
				}
			}
			// var can contain dimension specific information so for now we store it as properties.
			if (indexOfObject.find("var") !=indexOfObject.end())
			{
				hsize_t indexOfVar = indexOfObject["var"];
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(indexOfVar);

				if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup("var"); // have to make sure that "var" is a group and not a dataset


					std::map<std::string, std::vector<QString>> categories;
					H5AD::LoadCategories(group, categories);

					auto nrOfObjects = group.getNumObjs();
					for (auto go = 0; go < nrOfObjects; ++go)
					{
						std::string objectName1 = group.getObjnameByIdx(go);
						H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

						if (objectType1 == H5G_DATASET)
						{
							H5::DataSet dataSet = group.openDataSet(objectName1);
							if (!((objectName1 == "index") || (objectName1 == "_index")))
							{
								auto datasetClass = dataSet.getDataType().getClass();

								if (datasetClass == H5T_FLOAT)
								{
									std::vector<float> values;
									if (H5Utils::read_vector(group, objectName1, &values, H5::PredType::NATIVE_FLOAT))
									{
										if (values.size() == _dimensionNames.size())
										{
											pointsDataset->setProperty(objectName1.c_str(), QList<QVariant>(values.cbegin(), values.cend()));
										}
									}
								}
								else if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_ENUM))
								{
									std::vector<int64_t> values;
									if (H5Utils::read_vector(group, objectName1, &values, H5::PredType::NATIVE_INT64))
									{
										if (values.size() == _dimensionNames.size())
										{
											QString label = objectName1.c_str();
											QVariantList labels;
											if (dataSet.attrExists("categories"))
											{
												if(categories.count(objectName1))
												{
													const std::vector<QString>& category_labels = categories[objectName1];
													labels.resize(values.size());
													for(qsizetype i =0; i < labels.size(); ++i)
													{
														qsizetype index = values[i];
														if(index < category_labels.size())
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
												pointsDataset->setProperty(label, QList<QVariant>(values.cbegin(), values.cend()));
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
													pointsDataset->setProperty(label, labels);
												}
											}
											else
												pointsDataset->setProperty(label, labels);
										}
									}
								}
								else if (datasetClass == H5T_STRING)
								{
									// try to read as strings
									std::vector<QString> items;
									H5Utils::read_vector_string(dataSet, items);
									if (items.size() == _dimensionNames.size())
									{
										pointsDataset->setProperty(objectName1.c_str(), QList<QVariant>(items.cbegin(), items.cend()));
									}
								}
							}
						}
					}
				}
				else if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet("var");
					auto datasetClass = dataset.getDataType().getClass();
					if (datasetClass == H5T_COMPOUND)
					{
						std::map<std::string, std::vector<QVariant> > compoundMap;
						if (H5Utils::read_compound(dataset, compoundMap))
						{
							for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
							{
								std::size_t nrOfItems = component->second.size();
								if (nrOfItems == _dimensionNames.size())
								{
									if (component->first == "index")
									{
										for (std::size_t i = 0; i < nrOfItems; ++i)
											_dimensionNames[i] = component->second[i].toString();
										pointsDataset->setDimensionNames(_dimensionNames);
									}
									else
									{
										pointsDataset->setProperty(component->first.c_str(), QList<QVariant>(component->second.cbegin(), component->second.cend()));
									}
								}

							}
						}
					}
				}
			}
			

		}
		catch(const std::exception &e)
		{
			std::string mesg = e.what();
			int bp = 0;
			++bp;
		}
		
		

		
		events().notifyDatasetChanged(pointsDataset);
		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	
}

