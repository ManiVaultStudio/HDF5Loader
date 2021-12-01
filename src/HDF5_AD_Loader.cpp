#include "HDF5_AD_Loader.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>

#include "H5Utils.h"
#include "DataContainerInterface.h"
#include <iostream>

#include "Cluster.h"
#include "ClusterData.h"
#include "util/DatasetRef.h"
#include <set>

using namespace hdps;

namespace H5AD
{
	struct compareStringsAsNumbers {
		bool operator()(const std::string& a, const std::string& b) const
		{
			try
			{
				if (H5Utils::is_number(a) && H5Utils::is_number(b))
				{
					double d_a = strtod(a.c_str(), nullptr);
					double d_b = strtod(b.c_str(), nullptr);
					return d_a < d_b;
				}
				return a < b;
			}
			catch (...)
			{

			}
		}
	};


	void CreateColorVector(std::size_t nrOfColors, std::vector<QColor> &colors)
	{
		
		if (nrOfColors)
		{
			colors.resize(nrOfColors);
			std::size_t index = 0;
			for (std::size_t i=0; i < nrOfColors; ++i)
			{
				const float h = std::min<float>((1.0 * i / (nrOfColors + 1)), 1.0f);
				if (h > 1 || h < 0)
				{
					int bp = 0;
					bp++;
				}
				colors[i] = QColor::fromHsvF(h, 0.5f, 1.0f);
			}
		}
		else
			colors.clear();
	}


	void addClusters(const std::map<std::string, std::vector<unsigned>> &indices, QString name, QString mainDatasetName, CoreInterface *core)
	{
		if(indices.size() <= 1)
			return; // no point in adding only a single cluster
		if (name[0] == '/')
			name.remove(0, 1);
		std::vector<QColor> qcolors;
		CreateColorVector(indices.size(),qcolors);

		util::DatasetRef<Clusters> clustersDatasetRef(core->addData("Cluster", name, mainDatasetName));
		
		core->notifyDataAdded(clustersDatasetRef.getDatasetName());
		auto& clustersDataset = *clustersDatasetRef;
		clustersDataset.getClusters().reserve(indices.size());

		int index = 0;
		for (auto& indice : indices)
		{
			Cluster cluster;
			if (indice.first.empty())
				cluster.setName(QString(" "));
			else
				cluster.setName(indice.first.c_str());
			cluster.setColor(qcolors[index]);

			cluster.setIndices(indice.second);
			clustersDataset.addCluster(cluster);
			++index;
		}
		
		// Notify others that the clusters have changed
		core->notifyDataChanged(clustersDatasetRef.getDatasetName());
	}
	  
	template<typename T>
	void addNumericalMetaData(std::vector<T> &numericalMetaData, std::vector<QString> numericalMetaDataDimensionNames, QString name, QString mainDatasetName, CoreInterface *core, bool transpose)
	{
		std::size_t nrOfDimensions = numericalMetaDataDimensionNames.size();
		if (nrOfDimensions)
		{
			std::size_t nrOfSamples = numericalMetaData.size() / nrOfDimensions;
			if(transpose)
				H5Utils::transpose(numericalMetaData.begin(), numericalMetaData.end(),nrOfSamples);

			// if numericalMetaDataDimensionNames start with name, remove it
			for (auto &dimName : numericalMetaDataDimensionNames)
			{
				if (dimName.indexOf(name) == 0)
				{
					dimName.remove(0, name.length());
				}
				// remove forward slash from dimName if it has one
				if (dimName[0] == '/')
					dimName.remove(0, 1);
			}
			// remove forward slash from name if it has one
			if (name[0] == '/')
				name.remove(0, 1);
			QString numericalDatasetName = name + " (numerical)";
			QString uniqueNumericalMetaDatasetName = core->addData("Points", numericalDatasetName, mainDatasetName);
			util::DatasetRef<Points> numericalDatasetRef(uniqueNumericalMetaDatasetName);
			core->notifyDataAdded(numericalDatasetRef.getDatasetName());
			Points& numericalMetadataDataset = *numericalDatasetRef;
			numericalMetadataDataset.setDataElementType<T>();
			numericalMetadataDataset.setData(std::move(numericalMetaData), nrOfDimensions);
			
			numericalMetadataDataset.setDimensionNames(numericalMetaDataDimensionNames);

			core->notifyDataChanged(numericalDatasetRef.getDatasetName());
		}
	}
	void LoadData(const H5::DataSet &dataset, Points &points)
	{
		H5Utils::MultiDimensionalData<float> mdd;

		if (H5Utils::read_multi_dimensional_data(dataset, mdd, H5::PredType::NATIVE_FLOAT))
		{
			if (mdd.size.size() == 2)
			{
				points.setData(std::move(mdd.data), mdd.size[1]);
			}
		}
	}

	void LoadData(H5::Group& group, Points& points)
	{
		bool result = true;
		std::vector<float> data;
		std::vector<std::uint64_t> indices;
		std::vector<std::uint32_t> indptr;
		result &= H5Utils::read_vector(group, "data", &data, H5::PredType::NATIVE_FLOAT);
		if (result)
			result &= H5Utils::read_vector(group, "indices", &indices, H5::PredType::NATIVE_UINT64);
		if (result)
			result &= H5Utils::read_vector(group, "indptr", &indptr, H5::PredType::NATIVE_UINT32);

		if (result)
		{
			DataContainerInterface dci(points);
			std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;
			std::uint64_t ysize = *std::max_element(indices.cbegin(), indices.cend()) + 1;
			dci.resize(xsize, ysize);
			dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());
		}
	}

	void LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result)
	{
		result.clear();
		std::map<std::string, std::vector<QVariant> > compoundMap;
		if (H5Utils::read_compound(dataset, compoundMap))
		{
			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				if (component->first == "index")
				{
					std::size_t nrOfItems = component->second.size();
					result.resize(nrOfItems);
					for (std::size_t i = 0; i < nrOfItems; ++i)
						result[i] = component->second[i].toString();
					break;
				}
			}
		}
		
	}

	void LoadIndexStrings(H5::Group& group, std::vector<QString> &result)
	{
		auto nrOfObjects = group.getNumObjs();
		// first get the gene names
		for (auto go = 0; go < nrOfObjects; ++go)
		{
			std::string objectName1 = group.getObjnameByIdx(go);
			H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

			if ((objectType1 == H5G_DATASET) && (objectName1 == "index" || objectName1 == "_index"))
			{
				H5::DataSet dataSet = group.openDataSet(objectName1);
				
				H5Utils::read_vector_string(dataSet, result);
				break;;
			}
		}
	}

	void LoadSampleNamesAndMetaData(H5::DataSet &dataset, Points &points, hdps::CoreInterface* _core)
	{
		

		auto pointsDatasetName = points.getName();

		std::string h5datasetName = dataset.getObjName();
		if (h5datasetName[0] == '/')
			h5datasetName.erase(h5datasetName.cbegin());
		std::map<std::string, std::vector<QVariant> > compoundMap;

		typedef float numericMetaDataType;

		if (H5Utils::read_compound(dataset, compoundMap))
		{
			std::size_t nrOfComponents = compoundMap.size();
			std::vector<numericMetaDataType> numericalMetaData;
			std::vector<QString> numericalMetaDataDimensionNames;

			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				const std::size_t nrOfSamples = component->second.size();
				if(nrOfSamples == points.getNumPoints())
				{
					if (component->first != "index")
					{
						std::map<std::string, std::vector<unsigned>> indices;


						bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value
						std::vector<numericMetaDataType> values;
						values.reserve(points.getNumPoints());
						for (std::size_t s = 0; s < nrOfSamples; ++s)
						{
							std::string item = component->second[s].toString().toStdString();
							if (currentMetaDataIsNumerical)
							{
								if (H5Utils::is_number(item))
								{
									values.push_back(component->second[s].toFloat());
								}
								else
								{
									values.clear();
									currentMetaDataIsNumerical = false;
									// add former labels as caterogical as well.
									for (std::size_t i = 0; i <= s; ++i)
									{
										std::string value = component->second[i].toString().toStdString();
										indices[value].push_back(i);
									}
								}
							}
							else
							{
								indices[item].push_back(s);
							}
						}
						if (currentMetaDataIsNumerical)
						{
							numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
							numericalMetaDataDimensionNames.push_back(component->first.c_str());
						}
						else
						{
							addClusters(indices, component->first.c_str(), pointsDatasetName, _core);
						}

					}
				}
				
			}

			if(numericalMetaDataDimensionNames.size())
			{
				addNumericalMetaData(numericalMetaData, numericalMetaDataDimensionNames, h5datasetName.c_str(), pointsDatasetName, _core, true);
			}
		}
	}

	


	void LoadSampleNamesAndMetaData(H5::Group &group,  Points & points, CoreInterface *_core)
	{

		auto nrOfObjects = group.getNumObjs();


		std::vector<float> numericalMetaData;
		std::size_t nrOfNumericalMetaData = 0;
		std::vector<QString> numericalMetaDataDimensionNames;
		std:size_t nrOfRows = points.getNumPoints();

		auto h5groupName = group.getObjName();
		// first do the basics
		for (auto go = 0; go < nrOfObjects; ++go)
		{
			std::string objectName1 = group.getObjnameByIdx(go);
			if (objectName1[0] == '\\')
				objectName1.erase(objectName1.begin());
			H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataSet = group.openDataSet(objectName1);
				if (!((objectName1 == "index") || (objectName1 == "_index")))
				{
					auto datasetClass = dataSet.getDataType().getClass();
					if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_FLOAT))
					{
						std::vector<float> values;
						H5Utils::read_vector(group, objectName1, &values, H5::PredType::NATIVE_FLOAT);
						if (values.size() == points.getNumPoints())
						{
							numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
							numericalMetaDataDimensionNames.push_back(dataSet.getObjName().c_str());
						}
					}
					else // try to read as strings for now
					{

						std::map<std::string, std::vector<unsigned>> indices;
						std::vector<std::string> items;
						H5Utils::read_vector_string(dataSet, items);
						if (items.size() == points.getNumPoints())
						{
							for (unsigned i = 0; i < items.size(); ++i)
							{
								indices[items[i]].push_back(i);
							}
							addClusters(indices, dataSet.getObjName().c_str(), points.getName(), _core);
						}
					}

				}
			}
			else if (objectType1 == H5G_GROUP)
			{
				H5::Group group2 = group.openGroup(objectName1);
				LoadSampleNamesAndMetaData(group2, points,_core);
			}
		}
		if (numericalMetaDataDimensionNames.size())
		{
			addNumericalMetaData(numericalMetaData, numericalMetaDataDimensionNames, h5groupName.c_str(), points.getName(), _core, true);
		}
	}


}// namespace

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

bool HDF5_AD_Loader::load()
{
	
	if (_file == nullptr)
		return false;
	try
	{

		QString mainDatasetName;
		auto nrOfObjects = _file->getNumObjs();

		std::size_t rows = 0;
		std::size_t columns = 0;
		// first read the main data
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);
			if (objectType1 == H5G_DATASET)
			{
				if (objectName1 == "X")
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);

					mainDatasetName = H5Utils::createPointsDataset(_core, true, QFileInfo(_fileName).baseName());
					hdps::util::DatasetRef<Points> mainDatassetRef(mainDatasetName);
					Points& points = dynamic_cast<Points&>(*mainDatassetRef);
					QGuiApplication::setOverrideCursor(Qt::WaitCursor);
					H5AD::LoadData(dataset, points);
					rows = points.getNumPoints();
					columns = points.getNumDimensions();
				}
			}
			else if (objectType1 == H5G_GROUP)
			{
				if (objectName1 == "X")
				{
					H5::Group group = _file->openGroup(objectName1);
					mainDatasetName = H5Utils::createPointsDataset(_core);
					hdps::util::DatasetRef<Points> mainDatassetRef(mainDatasetName);
					Points& points = dynamic_cast<Points&>(*mainDatassetRef);
					QGuiApplication::setOverrideCursor(Qt::WaitCursor);
					H5AD::LoadData(group, points);
					rows = points.getNumPoints();
					columns = points.getNumDimensions();
				}
			}
		}
		if (mainDatasetName.isEmpty())
		{
			QApplication::restoreOverrideCursor();
			return false;
		}
		
		hdps::util::DatasetRef<Points> mainDatassetRef(mainDatasetName);
		Points& points = dynamic_cast<Points&>(*mainDatassetRef);
		points.setDimensionNames(_dimensionNames);
		points.setProperty("Sample Names", QList<QVariant>(_sampleNames.cbegin(), _sampleNames.cend()));

		std::set<std::string> objectsToSkip = { "X", "var", "raw.X", "raw.var"};
		
		// now we look for nice to have data
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataset = _file->openDataSet(objectName1);
				H5AD::LoadSampleNamesAndMetaData(dataset, points, _core);
			}
			else if (objectType1 == H5G_GROUP)
			{
				H5::Group group = _file->openGroup(objectName1);
				H5AD::LoadSampleNamesAndMetaData(group, points, _core);
			}
		}
		

		
		_core->notifyDataChanged(mainDatasetName);
		QGuiApplication::restoreOverrideCursor();
		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		QGuiApplication::restoreOverrideCursor();
		return false;
	}

	
}

