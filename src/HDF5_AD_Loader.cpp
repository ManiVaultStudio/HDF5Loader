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


	
	  
	
	void LoadData(const H5::DataSet &dataset, Dataset<Points> pointsDataset)
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
			std::vector<std::string> indexNames(2);
			indexNames[0] = "index";
			indexNames[1] = "_index";
			for(std::size_t i=0; i < 2; ++i)
			{
				std::string currentIndexName = indexNames[i];
				for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
				{
					if (component->first == currentIndexName)
					{
						std::size_t nrOfItems = component->second.size();
						result.resize(nrOfItems);
						for (std::size_t i = 0; i < nrOfItems; ++i)
							result[i] = component->second[i].toString();
						return;
					}
				}
			}
		}
		
	}

	void LoadIndexStrings(H5::Group& group, std::vector<QString> &result)
	{
		/* order to look for is
		   -  value of _index attribute
		   -  _index dataset
		   -  index dataset
		*/
		std::vector<std::string> indexObjectNames;
		indexObjectNames.reserve(3);
		if(group.attrExists("_index"))
		{
			auto attribute = group.openAttribute("_index");
			std::string objectName;
			attribute.read(attribute.getDataType(), objectName);
			indexObjectNames.push_back(objectName);
		}
		indexObjectNames.push_back("_index");
		indexObjectNames.push_back("index");

		auto nrOfObjects = group.getNumObjs();
		for(std::size_t i=0; i < indexObjectNames.size(); ++i)
		{
			std::string currentIndexObjectName = indexObjectNames[i];
			for (auto go = 0; go < nrOfObjects; ++go)
			{
				std::string objectName = group.getObjnameByIdx(go);
				H5G_obj_t objectType = group.getObjTypeByIdx(go);

				if ((objectType == H5G_DATASET) && (objectName == currentIndexObjectName))
				{
					H5::DataSet dataSet = group.openDataSet(objectName);

					H5Utils::read_vector_string(dataSet, result);
					return;
				}
			}
		}
		
		
	}

	void LoadSampleNamesAndMetaData(H5::DataSet &dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core)
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

	
	bool LoadSparseMatrix(H5::Group& group, Dataset<Points>& pointsDataset, CoreInterface* _core)
	{
		auto nrOfObjects = group.getNumObjs();
		auto h5groupName = group.getObjName();

		bool containsSparseMatrix = false;
		if (nrOfObjects > 2)
		{
			// check for sparse data
			std::vector<float> data;
			std::vector<std::uint64_t> indices;
			std::vector<std::uint32_t> indptr;
			containsSparseMatrix = H5Utils::read_vector(group, "data", &data, H5::PredType::NATIVE_FLOAT);
			if (containsSparseMatrix)
				containsSparseMatrix &= H5Utils::read_vector(group, "indices", &indices, H5::PredType::NATIVE_UINT64);
			if (containsSparseMatrix)
				containsSparseMatrix &= H5Utils::read_vector(group, "indptr", &indptr, H5::PredType::NATIVE_UINT32);

			if (containsSparseMatrix)
			{
				std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;
				if (xsize == pointsDataset->getNumPoints())
				{
					std::uint64_t ysize = *std::max_element(indices.cbegin(), indices.cend()) + 1;

					std::vector<QString> dimensionNames(ysize);
					for (std::size_t l = 0; l < ysize; ++l)
						dimensionNames[l] = QString::number(l);
					QString numericalDatasetName = QString(h5groupName.c_str()) /* + " (numerical)" */ ;
					while (numericalDatasetName[0] == '/')
						numericalDatasetName.remove(0, 1);
					while (numericalDatasetName[0] == '\\')
						numericalDatasetName.remove(0, 1);
					Dataset<Points> numericalDataset = _core->createDerivedDataset(numericalDatasetName, pointsDataset); // core->addDataset("Points", numericalDatasetName, parent);
					_core->notifyDatasetAdded(numericalDataset);
					DataContainerInterface dci(numericalDataset);
					dci.resize(xsize, ysize);
					dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());
					numericalDataset->setDimensionNames(dimensionNames);
					_core->notifyDatasetChanged(numericalDataset);
				}
			}
		}

		return containsSparseMatrix;
	}

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>> &categories)
	{
		if (!group.exists("__categories"))
			return false;

		H5::Group categoriesGroup = group.openGroup("__categories");

		auto nrOfObjects = categoriesGroup.getNumObjs();
		for (auto go = 0; go < nrOfObjects; ++go)
		{
			std::string objectName1 = categoriesGroup.getObjnameByIdx(go);
			if (objectName1[0] == '\\')
				objectName1.erase(objectName1.begin());
			H5G_obj_t objectType1 = categoriesGroup.getObjTypeByIdx(go);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataSet = categoriesGroup.openDataSet(objectName1);
				std::vector<QString> items;
				H5Utils::read_vector_string(dataSet, items);
				categories[objectName1] = items;
			}
		}

		return (!categories.empty());
	}

	DataHierarchyItem *GetDerivedDataset(const QString &name, Dataset<Points>& pointsDataset)
	{
		const auto& children = pointsDataset->getDataHierarchyItem().getChildren();
		for(auto it  = children.begin(); it != children.end(); ++it)
		{
			if((*it)->getGuiName() == name)
			{
				return *it;
			}
		}
		return nullptr;
	}


	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>> & result)
	{
		auto nrOfObjects = group.getNumObjs();
		if ((nrOfObjects == 2) && (group.getObjnameByIdx(0) == "categories") && (group.getObjnameByIdx(1) == "codes"))
		{
			H5G_obj_t objectTypeCategories = group.getObjTypeByIdx(0);
			H5G_obj_t objectTypeCodes = group.getObjTypeByIdx(1);
			H5::DataSet catDataset = group.openDataSet("categories");
			std::vector<QString> catValues;
			H5Utils::read_vector_string(catDataset, catValues);

			H5::DataSet codesDataset = group.openDataSet("codes");
			auto datasetClass = codesDataset.getDataType().getClass();

			std::vector<QString> categories;
			if (datasetClass == H5T_INTEGER)
			{
				std::vector<std::int64_t> values;

				if (H5Utils::read_vector(group, "codes", &values, H5::PredType::NATIVE_INT64))
				{

					for (unsigned i = 0; i < values.size(); ++i)
					{
						std::int64_t value = values[i];
						if( value < catValues.size())
							result[catValues[value]].push_back(i);
						else
						{
							result.clear();
							return  false;
						}
					}
					return  true;
				}
			}
		}
		result.clear();
		return false;
	}
	
	void LoadSampleNamesAndMetaData(H5::Group &group,  Dataset<Points>  pointsDataset, CoreInterface *_core)
	{

		auto nrOfObjects = group.getNumObjs();
		auto h5groupName = group.getObjName();

		if(LoadSparseMatrix(group, pointsDataset, _core))
			return;


		std::vector<float> numericalMetaData;
		std::size_t nrOfNumericalMetaData = 0;
		std::vector<QString> numericalMetaDataDimensionNames;
		std::size_t nrOfRows = pointsDataset->getNumPoints();
		std::map<std::string, std::vector<QString>> categories;

		bool categoriesLoaded = LoadCategories(group, categories);

		std::map<QString, std::vector<unsigned>> codedCategories;
		if(LoadCodedCategories(group, codedCategories))
		{
			std::size_t count = 0;
			for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
				count += it->second.size();
			if (count != pointsDataset->getNumPoints())
				std::cout << "WARNING: " << "not all datapoints are accounted for" << std::endl;

			std::size_t posFound = h5groupName.find("_color");
			if(posFound == std::string::npos)
			{
				if (count == pointsDataset->getNumPoints())
				{
					H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset);
				}
			}
			else
			{
				bool itemsAreColors = true;
				for (auto codedCat = codedCategories.cbegin(); codedCat != codedCategories.cend(); ++codedCat)
				{
					if (codedCat->first != "NA")
					{
						if (!QColor::isValidColor(codedCat->first))
						{

							itemsAreColors = false;
							break;
						}
					}
				}
				if (itemsAreColors)
				{

					QString datasetNameToFind = h5groupName.c_str();
					datasetNameToFind.resize(posFound);
					datasetNameToFind += "_label";
					if (datasetNameToFind[0] == '/')
						datasetNameToFind.remove(0, 1);
					DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, pointsDataset);
					if (foundDataset)
					{
						if (foundDataset->getDataType() == DataType("Clusters"))
						{
							std::cout << " --- " << datasetNameToFind.toStdString() << " --- " << std::endl;
							auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
							int unchangedClusterColors = 0;
							for (std::size_t i = 0; i < clusters.size(); ++i)
							{
								const auto& clusterIndices = clusters[i].getIndices();
								std::set<uint32_t> clusterIndicesSet(clusterIndices.cbegin(), clusterIndices.cend());
								bool clusterColorChanged = false;
								for (auto codedCat = codedCategories.cbegin(); codedCat != codedCategories.cend(); ++codedCat)
								{
									const auto& indices = codedCat->second;
									if( indices.size() >= clusterIndices.size())
									{
										std::set<uint32_t> indicesSet(indices.cbegin(), indices.cend());
										if (std::includes(indices.cbegin(), indices.cend(), clusterIndices.cbegin(), clusterIndices.cend()))
										{
											bool subset = (indices.size() > clusterIndices.size());
											QString newColor = QColor::isValidColor(codedCat->first) ? codedCat->first : "#000000";
											if (subset)
												std::cout << "* ";
											else
												std::cout << "  ";
											std::cout << "changing cluster " << clusters[i].getName().toStdString() << " color " << clusters[i].getColor().name().toStdString() << " to " << newColor.toStdString() << std::endl;
											clusters[i].setColor(codedCat->first);
											clusterColorChanged = true;
											break;
										}
									}
								}
								if(!clusterColorChanged)
								{
									++unchangedClusterColors;
									std::cout << "cluster " << clusters[i].getName().toStdString() << " color " << clusters[i].getColor().name().toStdString() << " was not changed" << std::endl;
									
								}
								Application::core()->notifyDatasetChanged(foundDataset->getDataset());
							}

							if(unchangedClusterColors)
							{
								// if not all cluster colors where changed, we will add the color as well so at least it's visible.
								std::map<QString, QColor> colors;
								for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
									colors[it->first] = QColor(it->first);

								
								H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset, colors);
							}
						}
					}
				}
				else
				{
					H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset);
				}
			}
		}
		else
		{
			// first do the basics
			for (int load_colors = 0; load_colors < 2; ++load_colors)
			{

				for (auto go = 0; go < nrOfObjects; ++go)
				{
					std::string objectName1 = group.getObjnameByIdx(go);
					std::size_t posFound = objectName1.find("_color");
					if (load_colors == 0)
					{
						if (posFound != std::string::npos)
							continue;
					}
					else
					{
						if (posFound == std::string::npos)
							continue;
					}
					if (objectName1[0] == '\\')
						objectName1.erase(objectName1.begin());
					H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet dataSet = group.openDataSet(objectName1);
						if (!((objectName1 == "index") || (objectName1 == "_index")))
						{
							if (categories.find(objectName1) != categories.end())
							{
								if (dataSet.attrExists("categories"))
								{
									std::vector<uint64_t> index;
									const std::vector<QString>& labels = categories[objectName1];
									if (H5Utils::read_vector(group, objectName1, &index, H5::PredType::NATIVE_UINT64))
									{
										std::map<QString, std::vector<unsigned>> indices;
										if (index.size() == pointsDataset->getNumPoints())
										{
											for (unsigned i = 0; i < index.size(); ++i)
											{
												indices[labels[index[i]]].push_back((i));
											}
											H5Utils::addClusterMetaData(_core, indices, dataSet.getObjName().c_str(), pointsDataset);
										}
									}
								}
							}
							else
							{
								auto datasetClass = dataSet.getDataType().getClass();

								if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_FLOAT) || (datasetClass == H5T_ENUM))
								{
									std::vector<float> values;
									if (H5Utils::read_vector(group, objectName1, &values, H5::PredType::NATIVE_FLOAT))
									{
										// 1 dimensional
										if (values.size() == pointsDataset->getNumPoints())
										{
											numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
											numericalMetaDataDimensionNames.push_back(dataSet.getObjName().c_str());
										}
									}
									else
									{
										// multi-dimensiona,  only 2 supported for now
										H5Utils::MultiDimensionalData<float> mdd;

										if (H5Utils::read_multi_dimensional_data(dataSet, mdd, H5::PredType::NATIVE_FLOAT))
										{
											if (mdd.size.size() == 2)
											{
												if (mdd.size[0] == pointsDataset->getNumPoints())
												{

													QString baseString = dataSet.getObjName().c_str();
													std::vector<QString> dimensionNames(mdd.size[1]);
													for (std::size_t l = 0; l < mdd.size[1]; ++l)
													{
														dimensionNames[l] = QString::number(l + 1);
													}
													H5Utils::addNumericalMetaData(_core, mdd.data, dimensionNames, false, pointsDataset, baseString);
												}
											}
										}
									}

								}
								else if (datasetClass == H5T_COMPOUND)
								{
									std::map<std::string, std::vector<QVariant> >result;
									if(H5Utils::read_compound(dataSet,result))
									{
										int i = 0;
										++i;
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
										H5Utils::addClusterMetaData(_core, indices, dataSet.getObjName().c_str(), pointsDataset);
									}
									else
									{
										bool itemsAreColors = true;
										for (std::size_t i = 0; i < items.size(); ++i)
										{
											if (items[i] != "NA")
											{
												if (!QColor::isValidColor(items[i]))
												{

													itemsAreColors = false;
													break;
												}
											}
										}
										if (itemsAreColors)
										{
											std::size_t posFound =  posFound = objectName1.find("_colors");
											if (posFound != std::string::npos)
											{
												QString datasetNameToFind;
												QString temp = objectName1.c_str();
												temp.resize(posFound);
												datasetNameToFind = QString("obs/") + temp;
											
												DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, pointsDataset);
												if (foundDataset)
												{
													if (foundDataset->getDataType() == DataType("Clusters"))
													{
														auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
														if (clusters.size() == items.size())
														{
															for (std::size_t i = 0; i < clusters.size(); ++i)
																clusters[i].setColor(items[i]);

															Application::core()->notifyDatasetChanged(foundDataset->getDataset());
														}
													}
												}
											}

										}

									}
								}
							}

						}
					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group group2 = group.openGroup(objectName1);
						LoadSampleNamesAndMetaData(group2, pointsDataset, _core);
					}
				}


			}
		}
		

		
		if (numericalMetaDataDimensionNames.size())
		{
			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset, h5groupName.c_str());
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
		try
		{
			hsize_t indexOfVar = nrOfObjects;
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				std::string objectName1 = _file->getObjnameByIdx(fo);
				if (objectsToSkip.count(objectName1) == 0)
				{
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
				else if (objectName1 == "var")
				{
					indexOfVar = fo;
				}
			}
			// var can contain dimension specific information so for now we store it as properties.
			if (indexOfVar != nrOfObjects)
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(indexOfVar);

				if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup("var"); // have to make sure that "var" is a group and not a dataset
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
											/*
											  if (datasetClass == H5T_ENUM)
												label += " (numerical)";
												*/
											pointsDataset->setProperty(label, QList<QVariant>(values.cbegin(), values.cend()));

											if (datasetClass == H5T_ENUM)
											{
												auto enumType = dataSet.getEnumType();
												QList<QVariant> labels;
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
		
		

		
		_core->notifyDatasetChanged(pointsDataset);
		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	
}

