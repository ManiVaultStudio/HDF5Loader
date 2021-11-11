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
#include "util/DatasetRef.h"

using namespace hdps;

namespace HDF5_10X
{

	template<typename T>
	void transposeInPlace(std::vector<T>& vec, std::size_t R, std::size_t C)
	{
		for (std::size_t j = 0; R > 1; j += C, R--)
		{
			for (std::size_t i = j + R, k = j + 1; i < vec.size(); i += R)
			{
				vec.insert(vec.begin() + k++, vec[i]);
				vec.erase(vec.begin() + i + 1);
			}
		}
	}



	bool is_number(const std::string& s)
	{

		char* end = nullptr;
		strtod(s.c_str(), &end);
		return *end == '\0';
	}


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


	union bfloat16_conversion {
		float fraw;
		uint16_t iraw[2];
	};

	int loadFromFile(std::string _fileName, std::shared_ptr<DataContainerInterface> &rawData, int optimization, int conversionOption, hdps::CoreInterface* _core)
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
					std::vector<uint16_t> data16;
					if (!H5Utils::read_vector(group, "data16", &data16, H5::PredType::NATIVE_UINT16))
						result = false;
					else
					{
						data.resize(data16.size());
						#pragma omp parallel for
						for(std::int64_t i=0; i < data16.size(); ++i)
						{ 
							bfloat16_conversion fr;
							fr.iraw[1] = data16[i];
							fr.iraw[0] = 0;
							data[i] = fr.fraw;
						}
					}
				}
				else
					result = false;

				if (!result)
					return false;


				
				std::size_t rows = barcodes.size();
				assert(indptr.size() == (rows + 1));
				std::size_t columns = genes.size();
				rawData->resize(rows, columns);
				rawData->set_sparse_row_data(indices, indptr, data, conversionOption);
				Points *points = rawData->points();
				
				std::vector<QString> dimensionNames(genes.size());
				#pragma omp parallel for
				for (int64_t i = 0; i < genes.size(); ++i)
				{
					dimensionNames[i] = genes[i].c_str();
				};
				points->setDimensionNames(dimensionNames);

				QList<QVariant> sample_names;
				for (int64_t i = 0; i < barcodes.size(); ++i)
				{
					sample_names.append(barcodes[i].c_str());
				}
				points->setProperty("Sample Names", sample_names);


				auto pointsDatasetName = points->getName();

				

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
									if (is_number(item))
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
										for (std::size_t i = 0; i < l; ++i)
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
								QString uniqueClustersName = _core->addData("Cluster", metaDataLabel.c_str(), pointsDatasetName);
								util::DatasetRef<Clusters> clustersDatasetRef;
								clustersDatasetRef.setDatasetName(uniqueClustersName);
								_core->notifyDataAdded(metaDataLabel.c_str());
								auto& clustersDataset = *clustersDatasetRef;

								clustersDataset.getClusters().reserve(indices.size());
								for (auto& indice : indices)
								{
									Cluster cluster;
									cluster.setName(indice.first.c_str());
									cluster.setColor(qcolors[indice.first]);
									cluster.setIndices(indice.second);
									clustersDataset.addCluster(cluster);
								}

								// Notify others that the clusters have changed
								_core->notifyDataChanged(clustersDatasetRef.getDatasetName());
							}

						} // if(ok)
					} // for nrOfMetaData

					if (nrOfNumericalMetaData)
					{
						transposeInPlace(numericalMetaData, numericalMetaData.size() / nrOfNumericalMetaData, nrOfNumericalMetaData);
						util::DatasetRef<Points> numericalDatasetRef(_core->createDerivedData("Numerical Metadata", pointsDatasetName));
						_core->notifyDataAdded(numericalDatasetRef.getDatasetName());
						auto& numericalMetadataDataset = *numericalDatasetRef;
						numericalMetadataDataset.setData(numericalMetaData, nrOfNumericalMetaData);
						numericalMetadataDataset.setDimensionNames(numericalMetaDataDimensionNames);
						
					}
				}

			}
			file.close();
			
		}
		catch (const std::exception & e)
		{
			std::cout << "Error Reading File: " << e.what() << std::endl;
		}
		catch (const H5::FileIException &e)
		{
			std::cout << "Error Reading File " << _fileName << std::endl;
		}
	}
			
}



HDF5_10X_Loader::HDF5_10X_Loader(hdps::CoreInterface *core)
{
	_core = core;
}

Points * HDF5_10X_Loader::open(const QString &fileName, int conversionIndex, int speedIndex)
{
	Points *points = nullptr;
	try
	{
		bool ok;
		QString dataSetName = QInputDialog::getText(nullptr, "Add New Dataset",
			"Dataset name:", QLineEdit::Normal, "DataSet", &ok);

		if (!ok || dataSetName.isEmpty())
		{
			return nullptr;
		}
		
		const QString name = _core->addData("Points", dataSetName);
		points = &_core->requestData<Points>(name);

		if (points == nullptr)
			return nullptr;

		QGuiApplication::setOverrideCursor(Qt::WaitCursor);
		std::shared_ptr<DataContainerInterface> rawData(new DataContainerInterface(points));

		HDF5_10X::loadFromFile(fileName.toStdString(), rawData, speedIndex, conversionIndex,_core);


		_core->notifyDataAdded(name);
	}
	catch (std::exception &e)
	{
		std::cout << "HDF5 10x Loader: " << e.what() << std::endl;
		return nullptr;
	}

	QGuiApplication::restoreOverrideCursor();
	return points;
}
	

