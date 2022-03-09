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


	
	  
	
	void LoadData(const H5::DataSet &dataset, Dataset<Points> &pointsDataset)
	{
		H5Utils::MultiDimensionalData<float> mdd;

		if (H5Utils::read_multi_dimensional_data(dataset, mdd, H5::PredType::NATIVE_FLOAT))
		{
			if (mdd.size.size() == 2)
			{
				pointsDataset->setData(std::move(mdd.data), mdd.size[1]);
			}
		}
	}

	void LoadData(H5::Group& group, Dataset<Points>& pointsDataset)
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
			DataContainerInterface dci(pointsDataset);
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

	void LoadSampleNamesAndMetaData(H5::DataSet &dataset, Dataset<Points> &pointsDataset, hdps::CoreInterface* _core)
	{

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
				if(nrOfSamples == pointsDataset->getNumPoints())
				{
					if (component->first != "index")
					{
						std::map<QString, std::vector<unsigned>> indices;


						bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value
						std::vector<numericMetaDataType> values;
						values.reserve(pointsDataset->getNumPoints());
						for (std::size_t s = 0; s < nrOfSamples; ++s)
						{
							QString item = component->second[s].toString();
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
										QString value = component->second[i].toString();
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
							H5Utils::addClusterMetaData(_core, indices, component->first.c_str(), pointsDataset);
						}

					}
				}
				
			}

			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset, h5datasetName.c_str());
		}
	}

	


	void LoadSampleNamesAndMetaData(H5::Group &group,  Dataset<Points> & pointsDataset, CoreInterface *_core)
	{

		auto nrOfObjects = group.getNumObjs();


		std::vector<float> numericalMetaData;
		std::size_t nrOfNumericalMetaData = 0;
		std::vector<QString> numericalMetaDataDimensionNames;
		std:size_t nrOfRows = pointsDataset->getNumPoints();

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
						if (values.size() == pointsDataset->getNumPoints())
						{
							numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
							numericalMetaDataDimensionNames.push_back(dataSet.getObjName().c_str());
						}
					}
					else // try to read as strings for now
					{

						std::map<QString, std::vector<unsigned>> indices;
						std::vector<QString> items;
						H5Utils::read_vector_string(dataSet, items);
						if (items.size() == pointsDataset->getNumPoints())
						{
							for (unsigned i = 0; i < items.size(); ++i)
							{
								indices[items[i]].push_back(i);
							}
							H5Utils::addClusterMetaData(_core,indices, dataSet.getObjName().c_str(), pointsDataset);
						}
					}

				}
			}
			else if (objectType1 == H5G_GROUP)
			{
				H5::Group group2 = group.openGroup(objectName1);
				LoadSampleNamesAndMetaData(group2, pointsDataset,_core);
			}
		}
		if (numericalMetaDataDimensionNames.size())
		{
			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames,  true, pointsDataset, h5groupName.c_str());
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

		Dataset<Points> pointsDataset;
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

					pointsDataset = H5Utils::createPointsDataset(_core, true, QFileInfo(_fileName).baseName());
					
					H5AD::LoadData(dataset, pointsDataset);
					rows = pointsDataset->getNumPoints();
					columns = pointsDataset->getNumDimensions();
				}
			}
			else if (objectType1 == H5G_GROUP)
			{
				if (objectName1 == "X")
				{
					H5::Group group = _file->openGroup(objectName1);
					pointsDataset = H5Utils::createPointsDataset(_core);
					H5AD::LoadData(group, pointsDataset);
					rows = pointsDataset->getNumPoints();
					columns = pointsDataset->getNumDimensions();
				}
			}
		}
		if (!pointsDataset.isValid())
		{
			return false;
		}
		
		
		pointsDataset->setDimensionNames(_dimensionNames);
		pointsDataset->setProperty("Sample Names", QList<QVariant>(_sampleNames.cbegin(), _sampleNames.cend()));

		std::set<std::string> objectsToSkip = { "X", "var", "raw.X", "raw.var"};
		
		// now we look for nice to have data
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataset = _file->openDataSet(objectName1);
				H5AD::LoadSampleNamesAndMetaData(dataset, pointsDataset, _core);
			}
			else if (objectType1 == H5G_GROUP)
			{
				H5::Group group = _file->openGroup(objectName1);
				H5AD::LoadSampleNamesAndMetaData(group, pointsDataset, _core);
			}
		}
		

		
		_core->notifyDatasetChanged(pointsDataset);
		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	
}

