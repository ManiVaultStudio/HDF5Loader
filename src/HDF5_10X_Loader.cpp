#include "HDF5_10X_Loader.h"

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
#include "DataHierarchyItem.h"
#include "util/Miscellaneous.h"

#include "biovault_bfloat16.h"
#include "H5Utils.h"
using namespace hdps;

namespace HDF5_10X
{
	

	/*
Column	Type	Description
barcodes	string	Barcode sequences and their corresponding gem groups (e.g. AAACGGGCAGCTCGAC-1)
data	uint32	Nonzero UMI counts in column-major order
gene_names	string	Gene symbols (e.g. Xkr4)
genes	string	Ensembl gene IDs (e.g. ENSMUSG00000051951)
indices	uint32	Row index of corresponding element in data
indptr	uint32	Index into data / indices of the start of each column
shape	uint64	Tuple of (n_rows, n_columns)
*/

/*
	union bfloat16_conversion {
		float fraw;
		uint16_t iraw[2];
	};
	*/
	int loadFromFile(std::string _fileName, std::shared_ptr<DataContainerInterface> &rawData, int optimization, TRANSFORM::Type transform_settings, hdps::CoreInterface* _core)
	{
	
		try
		{
			H5::H5File file(_fileName, H5F_ACC_RDONLY);
			auto nrOfObjects = file.getNumObjs();
			std::string objectName1 = file.getObjnameByIdx(0);
			H5G_obj_t objectType1 = file.getObjTypeByIdx(0);
			if (objectType1 == H5G_GROUP)
			{
				H5::Group group = file.openGroup(objectName1);
				std::vector<float> data;
				std::vector<biovault::bfloat16_t> data16;
				std::vector<uint32_t> indptr;
				std::vector<uint64_t> indices;
				std::vector<std::string> barcodes;
				std::vector<std::string> genes;

				bool result = true;

				if (group.exists("barcodes"))
				{
					H5::DataSet dataset = group.openDataSet("barcodes");
					if (!H5Utils::read_vector_string(dataset, barcodes))
						result = false;
				}
				else
					result = false;

				if (result & group.exists("genes"))
				{
					H5::DataSet dataset = group.openDataSet("genes");
					if (!H5Utils::read_vector_string(dataset, genes))
						result = false;
				}
				else
					result = false;

				
				if (result & group.exists("indptr"))
				{
					if (!H5Utils::read_vector(group, "indptr", &indptr, H5::PredType::NATIVE_UINT32))
						result = false;
				}
				else
					result = false;

				if (result & group.exists("indices"))
				{
					if (!H5Utils::read_vector(group, "indices", &indices, H5::PredType::NATIVE_UINT64))
						result = false;
				}
				else
					result = false;

				

				if (result & group.exists("data"))
				{
					if (!H5Utils::read_vector(group, "data", &data, H5::PredType::NATIVE_FLOAT))
						result = false;
				}
				else if(result & group.exists("data16"))
				{
					if (!H5Utils::read_vector(group, "data16", &data16, H5::PredType::NATIVE_UINT16))
						result = false;
					else
					{
						auto& dataHierarchyItem = rawData->points()->getDataHierarchyItem();

						dataHierarchyItem.setTaskName("Loading points");
						dataHierarchyItem.setTaskDescription(QString("Loading %1 points").arg(util::getIntegerCountHumanReadable(data16.size())));
						dataHierarchyItem.setTaskRunning();

						data.resize(data16.size());
						#pragma omp parallel for
						for(std::int64_t i=0; i < data16.size(); ++i)
						{
							data[i] = biovault::bfloat16_t(data16[i], true);

							if (i % 100000 == 0) {
								dataHierarchyItem.setTaskProgress(static_cast<float>(i) / static_cast<float>(data16.size()));
							}
								
						}

						dataHierarchyItem.setTaskFinished();
					}
				}
				else
					result = false;

				if (!result)
					return false;


				
				std::size_t rows = barcodes.size();
				assert(indptr.size() == (rows + 1));
				std::size_t columns = genes.size();

				Dataset<Points> pointsDataset = rawData->points();

				
				
				if (data16.size())
				{
					pointsDataset->setDataElementType<biovault::bfloat16_t>();
					rawData->resize(rows, columns);
					rawData->set_sparse_row_data(indices, indptr, data16, transform_settings);
				}
				else
				{
					pointsDataset->setDataElementType<float>();
					rawData->resize(rows, columns);
					rawData->set_sparse_row_data(indices, indptr, data, transform_settings);
				}
				
				
				std::vector<QString> dimensionNames(genes.size());
				#pragma omp parallel for
				for (int64_t i = 0; i < genes.size(); ++i)
				{
					dimensionNames[i] = genes[i].c_str();
				};
				pointsDataset->setDimensionNames(dimensionNames);

				QList<QVariant> sample_names;
				for (int64_t i = 0; i < barcodes.size(); ++i)
				{
					sample_names.append(barcodes[i].c_str());
				}
				pointsDataset.setProperty("Sample Names", sample_names);

				pointsDataset->getDataHierarchyItem().setTaskDescription("");

				
				if (result & group.exists("meta"))
				{
					auto metaDataSuperGroup = group.openGroup("meta");
					hsize_t nrOfMetaData = metaDataSuperGroup.getNumObjs();
					std::size_t nrOfNumericalMetaData = 0;
					std::vector<float> numericalMetaData;
					numericalMetaData.reserve(nrOfMetaData * rows);
					std::vector<QString> numericalMetaDataDimensionNames;
					for (hsize_t m = 0; m < nrOfMetaData; ++m)
					{
						std::string metaDataLabel = metaDataSuperGroup.getObjnameByIdx(m).c_str();
						auto metaDataGroup = metaDataSuperGroup.openGroup(metaDataLabel);
						std::vector<std::string> items;
						std::vector<std::uint8_t> colors;
						bool ok = H5Utils::read_vector_string(metaDataGroup, "l", items);
						ok &= H5Utils::read_vector(metaDataGroup, "c", &colors, H5::PredType::NATIVE_UINT8);
						ok &= ((3 * items.size()) == colors.size());

						if (ok)
						{

							std::map<std::string, std::vector<unsigned>> indices;
							std::map<std::string, QColor> qcolors;

							typedef float numericMetaDataType;
							double minValue = std::numeric_limits<numericMetaDataType>::max();
							double maxValue = std::numeric_limits<numericMetaDataType>::lowest();
							double sumValue = 0;
							bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value

							for (std::size_t l = 0; l < items.size(); ++l)
							{

								std::string item = items[l];
								if (currentMetaDataIsNumerical)
								{
									if (H5Utils::is_number(item))
									{
										double value = atof(item.c_str());
										if (std::isfinite(value))
										{
											if ((value >= std::numeric_limits<numericMetaDataType>::lowest()) && (value <= std::numeric_limits<numericMetaDataType>::max()))
											{
												sumValue += value;
												if (value < minValue)
													minValue = static_cast<numericMetaDataType>(value);
												if (value > maxValue)
													maxValue = static_cast<numericMetaDataType>(value);
											}
										}
										numericalMetaData.push_back(value);
									}
									else
									{
										numericalMetaData.resize(nrOfNumericalMetaData * rows);
										currentMetaDataIsNumerical = false;
										minValue = 0;
										maxValue = 0;
										sumValue = 0;
										// add former labels as caterogical as well.
										for (std::size_t i = 0; i <= l; ++i)
										{
											std::string value = items[i];
											indices[value].push_back(i);
											if (qcolors.find(value) == qcolors.end())
											{
												const auto colorOffset = i * 3;
												const auto red = colors[colorOffset];
												const auto green = colors[colorOffset + 1];
												const auto blue = colors[colorOffset + 2];
												qcolors[value] = QColor(red, green, blue);
											}
										}
									}
								}
								else
								{
									indices[item].push_back(l);
									if (qcolors.find(item) == qcolors.end())
									{
										const auto colorOffset = l * 3;
										const auto red = colors[colorOffset];
										const auto green = colors[colorOffset + 1];
										const auto blue = colors[colorOffset + 2];
										qcolors[item] = QColor(red, green, blue);
									}
								}
							}
							if (currentMetaDataIsNumerical)
							{
								nrOfNumericalMetaData++;
								numericalMetaDataDimensionNames.push_back(metaDataLabel.c_str());
							}
							else // it was categorical data
							{
								auto clusterDataset = _core->addDataset<Clusters>("Cluster", metaDataLabel.c_str(), pointsDataset);

								// Notify others that the dataset was added
								_core->notifyDatasetAdded(clusterDataset);

								clusterDataset->getClusters().reserve(indices.size());

								std::size_t sum = 0;
								for (auto& indice : indices)
								{
									
									Cluster cluster;
									if (indice.first.empty())
										cluster.setName(QString(" "));
									else
										cluster.setName(indice.first.c_str());
									cluster.setColor(qcolors[indice.first]);

									sum += indice.second.size();
									cluster.setIndices(indice.second);
									
									clusterDataset->addCluster(cluster);
								}

								assert(sum == rows);

								// Notify others that the clusters have changed
								_core->notifyDatasetChanged(clusterDataset);
							}

						} // if(ok)
					} // for nrOfMetaData

					if (nrOfNumericalMetaData)
					{
						H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset);
					}
				}

			}
			file.close();
			return true;
		}
		catch (const std::exception & e)
		{
			std::cout << "Error Reading File: " << e.what() << std::endl;
		}
		catch (const H5::FileIException &e)
		{
			std::cout << "Error Reading File " << _fileName << std::endl;
		}
		return false;
	}
	
}



HDF5_10X_Loader::HDF5_10X_Loader(hdps::CoreInterface *core)
{
	_core = core;
}

bool HDF5_10X_Loader::open(const QString& fileName)
{
	bool result = false;
	try
	{
		_file.reset(new H5::H5File(fileName.toLatin1().constData(), H5F_ACC_RDONLY));
		H5G_obj_t baseObjectType = _file->getObjTypeByIdx(0);

		if (baseObjectType == H5G_GROUP)
		{
			std::string baseObjectName = _file->getObjnameByIdx(0);
			H5::Group group = _file->openGroup(baseObjectName);
			if (group.exists("genes"))
			{
				H5::DataSet dataset = group.openDataSet("genes");
				result = H5Utils::read_vector_string(dataset, _dimensionNames);
			}
			if (group.exists("barcodes"))
			{
				H5::DataSet dataset = group.openDataSet("barcodes");
				result = H5Utils::read_vector_string(dataset, _sampleNames);
			}
			if (result)
				result &= group.exists("indptr");
			if (result)
				result &= group.exists("indices");
			if (result)
				result &= (group.exists("data") || group.exists("data16"));
			if (result)
				_fileName = fileName;
		}
		
	}
	catch (...)
	{
		_file->close();
		_file.release();
	}
	return result;
}

const std::vector<QString>& HDF5_10X_Loader::getDimensionNames() const
{
	return _dimensionNames;
}

bool HDF5_10X_Loader::load(TRANSFORM::Type transform_settings, int speedIndex)
{
	/*
Column	Type	Description
barcodes	string	Barcode sequences and their corresponding gem groups (e.g. AAACGGGCAGCTCGAC-1)
data	uint32	Nonzero UMI counts in column-major order
gene_names	string	Gene symbols (e.g. Xkr4)
genes	string	Ensembl gene IDs (e.g. ENSMUSG00000051951)
indices	uint32	Row index of corresponding element in data
indptr	uint32	Index into data / indices of the start of each column
shape	uint64	Tuple of (n_rows, n_columns)
*/

	
	if (_file == nullptr)
		return false;
	bool result = false;
	try
	{
		auto nrOfObjects = _file->getNumObjs();
		std::string objectName1 = _file->getObjnameByIdx(0);
		H5G_obj_t objectType1 = _file->getObjTypeByIdx(0);
		if (objectType1 == H5G_GROUP)
		{
			H5::Group group = _file->openGroup(objectName1);
			std::vector<float> data;
			std::vector<biovault::bfloat16_t> data16;
			std::vector<uint32_t> indptr;
			std::vector<uint64_t> indices;
			

			// dataset existance already checked when opening file so now directly open them

			bool result = !(_dimensionNames.empty());
			result &= !(_sampleNames.empty());
			if (result)
				result &= H5Utils::read_vector(group, "indptr", &indptr, H5::PredType::NATIVE_UINT32);
			if (result)
				result &= H5Utils::read_vector(group, "indices", &indices, H5::PredType::NATIVE_UINT64);
			if (result & group.exists("data"))
			{
				result &= H5Utils::read_vector(group, "data", &data, H5::PredType::NATIVE_FLOAT);
			}
			else if (result & group.exists("data16"))
			{
				result &= H5Utils::read_vector(group, "data16", &data16, H5::PredType::NATIVE_UINT16);
			}

			if (result)
			{
				std::size_t rows = _sampleNames.size();
				assert(indptr.size() == (rows + 1));
				std::size_t columns = _dimensionNames.size();

				
				Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, true, QFileInfo(_fileName).baseName());
				std::unique_ptr<DataContainerInterface> rawData(new DataContainerInterface(pointsDataset));

				if (data16.size())
				{
					pointsDataset->setDataElementType<biovault::bfloat16_t>();
					rawData->resize(rows, columns);
					rawData->set_sparse_row_data(indices, indptr, data16, transform_settings);
				}
				else
				{
					pointsDataset->setDataElementType<float>();
					rawData->resize(rows, columns);
					rawData->set_sparse_row_data(indices, indptr, data, transform_settings);
				}
				pointsDataset->setDimensionNames(_dimensionNames);
				pointsDataset->setProperty("Sample Names", QList<QVariant>(_sampleNames.cbegin(), _sampleNames.cend()));
				
			

				if (group.exists("meta"))
				{

					
					auto metaDataSuperGroup = group.openGroup("meta");
					hsize_t nrOfMetaData = metaDataSuperGroup.getNumObjs();
					std::size_t nrOfNumericalMetaData = 0;
					std::vector<float> numericalMetaData;
					numericalMetaData.reserve(nrOfMetaData * rows);
					std::vector<QString> numericalMetaDataDimensionNames;
					auto& dataHierarchyItem = pointsDataset->getDataHierarchyItem();
					dataHierarchyItem.setTaskDescription(QString("Loading %1 metadata items").arg(util::getIntegerCountHumanReadable(nrOfMetaData)));
					dataHierarchyItem.setTaskRunning();
					for (hsize_t m = 0; m < nrOfMetaData; ++m)
					{
						std::string metaDataLabel = metaDataSuperGroup.getObjnameByIdx(m).c_str();
						auto metaDataGroup = metaDataSuperGroup.openGroup(metaDataLabel);
						std::vector<QString> items;
						std::vector<std::uint8_t> colors;
						bool ok = H5Utils::read_vector_string(metaDataGroup, "l", items);
						ok &= H5Utils::read_vector(metaDataGroup, "c", &colors, H5::PredType::NATIVE_UINT8);
						ok &= ((3 * items.size()) == colors.size());

						if (ok)
						{

							std::map<QString, std::vector<unsigned>> indices;
							std::map<QString, QColor> qcolors;

							typedef float numericMetaDataType;
							double minValue = std::numeric_limits<numericMetaDataType>::max();
							double maxValue = std::numeric_limits<numericMetaDataType>::lowest();
							double sumValue = 0;
							bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value

							for (std::size_t l = 0; l < items.size(); ++l)
							{
								QString item = items[l];
								if (currentMetaDataIsNumerical)
								{
									
									if (H5Utils::is_number(item))
									{
										double value = item.toDouble();
										if (std::isfinite(value))
										{
											if ((value >= std::numeric_limits<numericMetaDataType>::lowest()) && (value <= std::numeric_limits<numericMetaDataType>::max()))
											{
												sumValue += value;
												if (value < minValue)
													minValue = static_cast<numericMetaDataType>(value);
												if (value > maxValue)
													maxValue = static_cast<numericMetaDataType>(value);
											}
										}
										numericalMetaData.push_back(value);
									}
									else
									{
										numericalMetaData.resize(nrOfNumericalMetaData * rows);
										currentMetaDataIsNumerical = false;
										minValue = 0;
										maxValue = 0;
										sumValue = 0;
										// add former labels as caterogical as well.
										for (std::size_t i = 0; i <= l; ++i)
										{
											QString value = items[i];
											indices[value].push_back(i);
											if (qcolors.find(value) == qcolors.end())
											{
												const auto colorOffset = i * 3;
												const auto red = colors[colorOffset];
												const auto green = colors[colorOffset + 1];
												const auto blue = colors[colorOffset + 2];
												qcolors[value] = QColor(red, green, blue);
											}
										}
									}
								}
								else
								{
									indices[item].push_back(l);
									if (qcolors.find(item) == qcolors.end())
									{
										const auto colorOffset = l * 3;
										const auto red = colors[colorOffset];
										const auto green = colors[colorOffset + 1];
										const auto blue = colors[colorOffset + 2];
										qcolors[item] = QColor(red, green, blue);
									}
								}
							}
							if (currentMetaDataIsNumerical)
							{
								nrOfNumericalMetaData++;
								numericalMetaDataDimensionNames.push_back(metaDataLabel.c_str());
							}
							else // it was categorical data
							{
								H5Utils::addClusterMetaData(_core, indices, metaDataLabel.c_str(), pointsDataset, qcolors);
							}

						} // if(ok)
						dataHierarchyItem.setTaskProgress(static_cast<float>(m) / static_cast<float>(nrOfMetaData));

					} // for nrOfMetaData
					dataHierarchyItem.setTaskFinished();
					H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset);
					
				}
				_core->notifyDatasetChanged(pointsDataset);
			}

		}
	}
	catch (const std::exception& e)
	{
		std::cout << "Error Reading File: " << e.what() << std::endl;
	}
	catch (const H5::FileIException& e)
	{
		std::cout << "Error Reading File: " << e.getCDetailMsg() << std::endl;
	}
	
	return result;
}
	

