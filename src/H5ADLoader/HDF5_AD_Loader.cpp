#include "HDF5_AD_Loader.h"

#include "H5ADUtils.h"

#include "H5Utils.h"

#include <Application.h>
#include <workflow/WorkflowOptions.h>
#include <workflow/WorkflowPlan.h>

#include <QGuiApplication>
#include <QtGlobal>

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
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

QStringList HDF5_AD_Loader::getOptionalObjects() const
{
	QStringList names;
	if (!_file)
		return names;

	const std::set<std::string> ignoredObjects = { "X", "uns", "var", "varm", "varp" };
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
	{
		const auto name = _file->getObjnameByIdx(index);
		if (ignoredObjects.count(name) == 0)
			names.append(QString::fromStdString(name));
	}
	return names;
}

std::pair<std::uint64_t, std::uint64_t> HDF5_AD_Loader::getMatrixShape() const
{
	if (!_file)
		return { 0, 0 };

	H5G_obj_t objectType = H5G_UNKNOWN;
	try
	{
		for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		{
			if (_file->getObjnameByIdx(index) == "X")
			{
				objectType = _file->getObjTypeByIdx(index);
				break;
			}
		}
	}
	catch (const H5::Exception&)
	{
		return { 0, 0 };
	}

	try
	{
		if (objectType == H5G_DATASET)
		{
			const auto dataSet = _file->openDataSet("X");
			const auto space = dataSet.getSpace();
			if (space.getSimpleExtentNdims() != 2)
				return { 0, 0 };

			hsize_t dimensions[2] = {};
			space.getSimpleExtentDims(dimensions);
			return { dimensions[0], dimensions[1] };
		}

		if (objectType == H5G_GROUP)
		{
			const auto group = _file->openGroup("X");
			if (!group.attrExists("shape"))
				return { 0, 0 };

			const auto attribute = group.openAttribute("shape");
			const auto space = attribute.getSpace();
			if (space.getSimpleExtentNdims() != 1 || space.getSimpleExtentNpoints() != 2)
				return { 0, 0 };

			hsize_t dimensions[2] = {};
			attribute.read(H5::PredType::NATIVE_HSIZE, dimensions);
			return { dimensions[0], dimensions[1] };
		}
	}
	catch (const H5::Exception&)
	{
		return { 0, 0 };
	}

	return { 0, 0 };
}

H5ADMatrixScanResult HDF5_AD_Loader::estimateTargetElementType() const
{
	return analyzeTargetElementType(false, {});
}

H5ADMatrixScanResult HDF5_AD_Loader::determineTargetElementType(const std::function<void(double)>& progress) const
{
	return analyzeTargetElementType(true, progress);
}

H5ADMatrixScanResult HDF5_AD_Loader::analyzeTargetElementType(const bool fullScan, const std::function<void(double)>& progress) const
{
	H5ADMatrixScanResult result;
	if (!_file)
		return result;

	H5::DataSet dataSet;
	bool sparseData = false;
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
	{
		if (_file->getObjnameByIdx(index) != "X")
			continue;

		if (_file->getObjTypeByIdx(index) == H5G_DATASET)
			dataSet = _file->openDataSet("X");
		else if (_file->getObjTypeByIdx(index) == H5G_GROUP)
		{
			auto group = _file->openGroup("X");
			if (H5Utils::contains_name(group, "data") && H5Utils::contains_name(group, "indices") && H5Utils::contains_name(group, "indptr"))
			{
				dataSet = group.openDataSet("data");
				sparseData = true;
			}
		}
		break;
	}

	if (dataSet.getId() < 0)
		return result;

	const auto dataType = dataSet.getDataType();
	const auto dataClass = dataType.getClass();
	if (dataClass != H5T_FLOAT && dataClass != H5T_INTEGER && dataClass != H5T_ENUM)
		return result;

	double minimum = std::numeric_limits<double>::infinity();
	double maximum = -std::numeric_limits<double>::infinity();
	bool allIntegral = true;
	std::uint64_t sampledValues = 0;
	std::uint64_t zeroValues = 0;
	const auto analyzeValues = [&](const std::vector<double>& values) {
		for (const auto value : values)
		{
			if (value == 0.0)
				++zeroValues;

			if (!std::isfinite(value))
			{
				allIntegral = false;
				++sampledValues;
				continue;
			}

			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
			allIntegral = allIntegral && std::trunc(value) == value;
			++sampledValues;
		}
	};

	if (sparseData)
	{
		result.sparseInput = true;
		const auto dataSpace = dataSet.getSpace();
		const auto valueCount = static_cast<std::uint64_t>(dataSpace.getSimpleExtentNpoints());
		result.storedEntryCount = valueCount;
		const auto blockSize = fullScan ? std::uint64_t { 1024 * 1024 } : std::uint64_t { 1024 };
		const auto segmentCount = valueCount == 0 ? 0 : fullScan ? (valueCount - 1) / blockSize + 1 : std::min<std::uint64_t>(64, (valueCount - 1) / blockSize + 1);
		for (std::uint64_t segment = 0; segment < segmentCount; ++segment)
		{
			const auto currentBlockSize = fullScan ? std::min(blockSize, valueCount - segment * blockSize) : blockSize;
			const auto startOffset = fullScan ? segment * blockSize : segment * (valueCount - blockSize) / std::max<std::uint64_t>(1, segmentCount - 1);
			const hsize_t start[] = { startOffset };
			const hsize_t count[] = { currentBlockSize };
			auto fileSpace = dataSet.getSpace();
			fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
			H5::DataSpace memorySpace(1, count);
			std::vector<double> values(static_cast<std::size_t>(currentBlockSize));
			dataSet.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
			analyzeValues(values);
			if (fullScan && progress)
				progress(static_cast<double>(segment + 1) / static_cast<double>(segmentCount));
		}
		if (fullScan)
		{
			const auto shape = getMatrixShape();
			if (shape.first > 0 && shape.second > 0 && shape.first <= std::numeric_limits<std::uint64_t>::max() / shape.second)
			{
				result.elementCount = shape.first * shape.second;
				if (result.elementCount >= valueCount)
				{
					result.zeroCount = std::min(result.elementCount, result.elementCount - valueCount + zeroValues);
					result.zeroFraction = static_cast<double>(result.zeroCount) / static_cast<double>(result.elementCount);
					result.sparsityAvailable = true;
				}
			}
		}
		else
		{
			const auto shape = getMatrixShape();
			if (shape.first > 0 && shape.second > 0 && shape.first <= std::numeric_limits<std::uint64_t>::max() / shape.second)
			{
				result.elementCount = shape.first * shape.second;
				if (result.elementCount >= valueCount)
				{
					const auto sampledZeroFraction = sampledValues > 0 ? static_cast<double>(zeroValues) / static_cast<double>(sampledValues) : 0.0;
					result.zeroFraction = static_cast<double>(result.elementCount - valueCount) / static_cast<double>(result.elementCount) +
						static_cast<double>(valueCount) / static_cast<double>(result.elementCount) * sampledZeroFraction;
					result.sparsityAvailable = true;
				}
			}
		}
	}
	else
	{
		const auto dataSpace = dataSet.getSpace();
		if (dataSpace.getSimpleExtentNdims() != 2)
			return result;

		hsize_t dimensions[2] = {};
		dataSpace.getSimpleExtentDims(dimensions);
		if (dimensions[0] == 0 || dimensions[1] == 0)
			return result;
		if (fullScan && dimensions[0] <= std::numeric_limits<std::uint64_t>::max() / dimensions[1])
			result.elementCount = dimensions[0] * dimensions[1];
		const auto scanRowCount = fullScan ? dimensions[0] : std::min<std::uint64_t>(128, dimensions[0]);
		const auto sampleColumnCount = std::min<std::uint64_t>(fullScan ? 1024 * 1024 : 512, dimensions[1]);
		for (std::uint64_t sampleRow = 0; sampleRow < scanRowCount; ++sampleRow)
		{
			const auto row = fullScan ? sampleRow : sampleRow * (dimensions[0] - 1) / std::max<std::uint64_t>(1, scanRowCount - 1);
			const auto column = fullScan ? 0 : sampleRow * (dimensions[1] - sampleColumnCount) / std::max<std::uint64_t>(1, scanRowCount - 1);
			const auto rowColumnCount = fullScan ? dimensions[1] : sampleColumnCount;
			for (std::uint64_t columnOffset = 0; columnOffset < rowColumnCount; columnOffset += sampleColumnCount)
			{
				const auto countColumns = std::min(sampleColumnCount, rowColumnCount - columnOffset);
				const hsize_t start[] = { row, column + columnOffset };
				const hsize_t count[] = { 1, countColumns };
				auto fileSpace = dataSet.getSpace();
				fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
				H5::DataSpace memorySpace(2, count);
				std::vector<double> values(static_cast<std::size_t>(countColumns));
				dataSet.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
				analyzeValues(values);
				if (fullScan && progress)
					progress((static_cast<double>(sampleRow) + static_cast<double>(columnOffset + countColumns) / static_cast<double>(rowColumnCount)) / static_cast<double>(dimensions[0]));
			}
		}
		if (fullScan && result.elementCount == sampledValues)
		{
			result.zeroCount = zeroValues;
			result.zeroFraction = static_cast<double>(zeroValues) / static_cast<double>(result.elementCount);
			result.sparsityAvailable = true;
		}
		else if (!fullScan && dimensions[0] <= std::numeric_limits<std::uint64_t>::max() / dimensions[1])
		{
			result.elementCount = dimensions[0] * dimensions[1];
			result.zeroFraction = sampledValues > 0 ? static_cast<double>(zeroValues) / static_cast<double>(sampledValues) : 0.0;
			result.sparsityAvailable = sampledValues > 0;
		}
	}
	result.analyzedValueCount = sampledValues;

	if (fullScan)
	{
		if (sparseData && result.sparsityAvailable && result.zeroCount > 0)
		{
			minimum = std::min(minimum, 0.0);
			maximum = std::max(maximum, 0.0);
		}

		result.rangeAvailable = std::isfinite(minimum) && std::isfinite(maximum);
		if (result.rangeAvailable)
		{
			result.minimum = minimum;
			result.maximum = maximum;
		}
	}

	if (sampledValues == 0)
		result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint8) + 3;
	else if (allIntegral && (dataClass == H5T_FLOAT || dataType.getSize() <= 4))
	{
		if (minimum < 0)
		{
			if (minimum >= std::numeric_limits<std::int8_t>::min() && maximum <= std::numeric_limits<std::int8_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::int8) + 3;
			else if (minimum >= std::numeric_limits<std::int16_t>::min() && maximum <= std::numeric_limits<std::int16_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::int16) + 3;
		}
		else
		{
			if (maximum <= std::numeric_limits<std::uint8_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint8) + 3;
			else if (maximum <= std::numeric_limits<std::uint16_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint16) + 3;
		}
	}

	if (result.recommendedTypeIndex < 0)
		result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::float32) + 3;

	return result;
}

bool HDF5_AD_Loader::sourceMayLosePrecision() const
{
	if (!_file)
		return false;

	try
	{
		for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		{
			if (_file->getObjnameByIdx(index) != "X")
				continue;

			H5::DataType dataType;
			if (_file->getObjTypeByIdx(index) == H5G_DATASET)
				dataType = _file->openDataSet("X").getDataType();
			else if (_file->getObjTypeByIdx(index) == H5G_GROUP)
				dataType = _file->openGroup("X").openDataSet("data").getDataType();
			else
				return false;

			return (dataType.getClass() == H5T_FLOAT && dataType.getSize() > sizeof(float)) ||
				(dataType.getClass() == H5T_INTEGER && dataType.getSize() > sizeof(std::uint16_t));
		}
	}
	catch (const H5::Exception&)
	{
		return true;
	}

	return false;
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

bool HDF5_AD_Loader::load(const H5ADImportOptions& options)
{
	
	if (_file == nullptr)
		return false;
	try
	{
		auto nrOfObjects = _file->getNumObjs();
		std::map<std::string, std::size_t> objectNameIdx;
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			objectNameIdx[objectName1] = fo;
		}

		std::set<std::string> objectsToProcess;
		for (const auto& name : options.optionalObjects)
			objectsToProcess.insert(name.toStdString());

		Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, false, options.datasetName);
		pointsDataset->getDataHierarchyItem().setVisible(false);
	
		const auto loaderInfo = std::make_shared<H5AD::LoaderInfo>();
		loaderInfo->_pointsDataset = pointsDataset;
		loaderInfo->_sourceFileName = _fileName;
		loaderInfo->_stagingDirectory = options.stagingDirectory.isEmpty() ? _stagingDirectory : options.stagingDirectory;
		loaderInfo->_useTiledStorage = options.useTiledStorage;
		loaderInfo->_tiledStorageConfiguration = options.tiledStorageConfiguration;
		loaderInfo->_originalDimensionNames = _dimensionNames;
		loaderInfo->_sampleNames = QVariantList(_sampleNames.cbegin(), _sampleNames.cend());
		const auto loader = shared_from_this();
		auto workflow = std::make_unique<mv::workflow::WorkflowPlan>(QObject::tr("Load H5AD point data"));

		workflow->addSequentialStage(QObject::tr("Stage HDF5 point data"), [loader, loaderInfo, storageType = options.storageType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
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

