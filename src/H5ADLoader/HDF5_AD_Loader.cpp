#include "HDF5_AD_Loader.h"

#include "H5ADUtils.h"

#include "H5Utils.h"

#include <Application.h>
#include <workflow/WorkflowOptions.h>
#include <workflow/WorkflowPlan.h>

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QListView>
#include <QtGlobal>

#include <cassert>
#include <iostream>
#include <map>
#include <set>

#include <PointData/PointData.h>

using namespace mv;

HDF5_AD_Loader::HDF5_AD_Loader(mv::CoreInterface* core, QObject* workflowReceiver, const QString& stagingDirectory)
{
	_core = core;
	_workflowReceiver = workflowReceiver;
	_stagingDirectory = stagingDirectory;
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
	
		const auto loaderInfo = std::make_shared<H5AD::LoaderInfo>();
		loaderInfo->_pointsDataset = pointsDataset;
		loaderInfo->_sourceFileName = _fileName;
		loaderInfo->_stagingDirectory = _stagingDirectory;
		loaderInfo->_originalDimensionNames = _dimensionNames;
		loaderInfo->_sampleNames = QVariantList(_sampleNames.cbegin(), _sampleNames.cend());
		const auto loader = shared_from_this();
		auto workflow = std::make_unique<mv::workflow::WorkflowPlan>(QObject::tr("Load H5AD point data"));

		workflow->addSequentialStage(QObject::tr("Stage HDF5 point data"), [loader, loaderInfo, storageType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
			const H5AD::ProgressCallback progress = [executionContext](const float value) {
				executionContext->setProgress(value);
			};

			if (!H5AD::load_X(loader->_file, *loaderInfo, storageType, progress))
				throw std::runtime_error("Unable to stage H5AD point data");
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::CurrentWorkerThread, 80);

		workflow->addNestedWorkflowStage(QObject::tr("Create point-data metadata"), [loader, loaderInfo, objectsToProcess, objectNameIdx](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
			auto metadataWorkflow = std::make_unique<mv::workflow::WorkflowPlan>(QObject::tr("Load optional H5AD metadata"));
			mv::workflow::WorkflowPlan::Jobs metadataJobs;

			const auto addMetadataJob = [&metadataJobs](const QString& name, mv::workflow::WorkflowPlan::JobFunction function) {
				metadataJobs.emplace_back(name, std::move(function), mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread);
			};

			const auto nrOfObjects = loader->_file->getNumObjs();
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				const auto objectName = loader->_file->getObjnameByIdx(fo);
				if (objectsToProcess.count(objectName) != 1)
					continue;

				const auto objectType = loader->_file->getObjTypeByIdx(fo);
				const auto jobName = QObject::tr("Load sample names and metadata: %1").arg(QString::fromLocal8Bit(objectName.c_str()));
				addMetadataJob(jobName, [loader, loaderInfo, objectName, objectType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
					const H5AD::MetadataProgressCallback progress = [executionContext](const float value, const QString&) {
						executionContext->setProgress(value);
					};

					try
					{
						if (objectType == H5G_DATASET)
						{
							auto h5Dataset = loader->_file->openDataSet(objectName);
							H5AD::LoadSampleNamesAndMetaDataFloat(h5Dataset, *loaderInfo, progress);
						}
						else if (objectType == H5G_GROUP)
						{
							auto h5Group = loader->_file->openGroup(objectName);
							H5AD::LoadSampleNamesAndMetaDataFloat(h5Group, *loaderInfo, progress);
						}
					}
					catch (const std::exception& exception)
					{
						std::cerr << "H5AD Loader: Optional metadata loading failed for " << objectName << ": " << exception.what() << std::endl;
					}
				});
			}

			const auto varIndex = objectNameIdx.find("var");
			if (varIndex != objectNameIdx.end())
			{
				const auto objectType = loader->_file->getObjTypeByIdx(varIndex->second);
				addMetadataJob(QObject::tr("Load properties: var"), [loader, loaderInfo, objectType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
					try
					{
						if (objectType == H5G_DATASET)
						{
							auto h5Dataset = loader->_file->openDataSet("var");
							LoadProperties(h5Dataset, *loaderInfo);
						}
						else if (objectType == H5G_GROUP)
						{
							auto h5Group = loader->_file->openGroup("var");
							LoadProperties(h5Group, *loaderInfo);
						}
					}
					catch (const std::exception& exception)
					{
						std::cerr << "H5AD Loader: Optional property loading failed for var: " << exception.what() << std::endl;
					}
				});
			}

			if (metadataJobs.empty())
				addMetadataJob(QObject::tr("No optional metadata selected"), [](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {});

			metadataWorkflow->addSequentialStage(QObject::tr("Load discovered metadata"), std::move(metadataJobs), 1.0);
			return metadataWorkflow;
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread, 19);

		workflow->addSequentialStage(QObject::tr("Publish point-data metadata"), [loaderInfo](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
			events().notifyDatasetDataChanged(loaderInfo->_pointsDataset);
			loaderInfo->_pointsDataset->getDataHierarchyItem().setVisible(true);
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread, 1);

		mv::workflow::WorkflowOptions options;
		options.cancellation.enabled = true;
		options.reporting.progress = true;
		options.reporting.finishedNotification = true;
		auto future = mv::Application::getWorkflowPlanExecutor().execute(std::move(workflow), nullptr, options);
		future.onFinished(_workflowReceiver, [pointsDataset](const mv::workflow::SharedWorkflowResult& result) {
			if (!result || result->getStatus() != mv::workflow::WorkflowResultBase::Status::Success)
				mv::data().removeDataset(pointsDataset);
		});

		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	return false;
}

