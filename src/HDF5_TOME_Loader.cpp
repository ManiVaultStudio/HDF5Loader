#include "HDF5_TOME_Loader.h"

//#include "util/Dataset.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>

#include "H5Utils.h"
#include "DataContainerInterface.h"
#include <iostream>

#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"
#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace hdps;

namespace TOME
{

	struct descType
	{
		std::string base;
		std::string name;
	};
	struct descType2
	{
		char* base;
		char* name;
	};
	struct tsneType
	{
		std::string sample_name;
		double x;
		double y;
	};


	bool LoadDesc(const H5::DataSet &dataset, TOME::descType &desc)
	{
		try
		{

			H5Utils::CompoundExtractor ce(dataset);
			desc.base = ce.extractString("base");
			desc.name = ce.extractString("name");
		}
		catch (...)
		{
			return false;
		}

		return true;
	}
	enum { Exons = 1, Introns = 2, Exons_T, Introns_T };

	void LoadData(H5::Group &group, std::shared_ptr<DataContainerInterface>&rawData, TRANSFORM::Type transformType, bool normalize_and_cpm, int test)
	{
#ifndef HIDE_CONSOLE
		std::cout << "Loading Data" << std::endl;
#endif
		hsize_t nrOfGroupObjects = group.getNumObjs();
		hsize_t exon_available = nrOfGroupObjects;
		hsize_t intron_available = nrOfGroupObjects;
		hsize_t transposed_exon_available = nrOfGroupObjects;
		hsize_t transposed_intron_available = nrOfGroupObjects;
		for (auto go = 0; go < nrOfGroupObjects; ++go)
		{
			H5G_obj_t objectType2 = group.getObjTypeByIdx(go);
			std::string objectName2 = group.getObjnameByIdx(go);
			if (objectType2 == H5G_GROUP) // skipping the other types for the first pass
			{

				if (objectName2 == "exon")
					exon_available = go;
				else if (objectName2 == "intron")
					intron_available = go;
				else if (objectName2 == "t_exon")
					transposed_exon_available = go;
				else if (objectName2 == "t_intron")
					transposed_intron_available = go;
			}
		}

		bool data_read = true;
		
		if ((transposed_exon_available < nrOfGroupObjects) && (transposed_intron_available < nrOfGroupObjects))
		{
			std::cout << "load t_exon and t_intron\n";
			// transposed is more similar to HDF5 and more optimal for our internal data structure
			for (int step = 0; step < 2; step++)
			{
				H5::Group exon_or_intron = (step == 0) ? group.openGroup("t_exon") : group.openGroup("t_intron");

				std::vector<std::int32_t> vector_dims;
				std::vector<uint64_t> vector_i;
				std::vector<uint32_t> vector_p;
				std::vector<float> vector_x;
				H5Utils::read_vector(exon_or_intron, "dims", &vector_dims, H5::PredType::NATIVE_INT32);
				H5Utils::read_vector(exon_or_intron, "i", &vector_i, H5::PredType::NATIVE_UINT64);
				H5Utils::read_vector(exon_or_intron, "p", &vector_p, H5::PredType::NATIVE_UINT32);
				H5Utils::read_vector(exon_or_intron, "x", &vector_x, H5::PredType::NATIVE_FLOAT);

				if (step == 0)
				{
					
					rawData->resize(vector_dims[1], vector_dims[0]);
					rawData->set_sparse_row_data(vector_i, vector_p, vector_x, TRANSFORM::None());
				}
				else
					rawData->increase_sparse_row_data(vector_i, vector_p, vector_x, TRANSFORM::None());
			}
		}
		else if ((exon_available < nrOfGroupObjects) && (intron_available < nrOfGroupObjects))
		{
			std::cout << "load exon and intron\n";
			// transposed is more similar to HDF5 and more optimal for our internal data structure
			for (int step = 0; step < 2; step++)
			{
				H5::Group exon_or_intron = (step == 0) ? group.openGroup("exon") : group.openGroup("intron");

				std::vector<std::int32_t> vector_dims;
				std::vector<uint64_t> vector_i;
				std::vector<uint32_t> vector_p;
				std::vector<float> vector_x;
				H5Utils::read_vector(exon_or_intron, "dims", &vector_dims, H5::PredType::NATIVE_INT32);
				
				H5Utils::read_vector(exon_or_intron, "i", &vector_i, H5::PredType::NATIVE_UINT64);
				
				H5Utils::read_vector(exon_or_intron, "p", &vector_p, H5::PredType::NATIVE_UINT32);
				
				H5Utils::read_vector(exon_or_intron, "x", &vector_x, H5::PredType::NATIVE_FLOAT);
				
				if (step == 0)
				{
					
					rawData->resize(vector_dims[0], vector_dims[1]);
					rawData->set_sparse_column_data(vector_i, vector_p, vector_x, TRANSFORM::None());
				}
				else
					rawData->increase_sparse_column_data(vector_i, vector_p, vector_x, TRANSFORM::None());
			}
		}
		else
			data_read = false;

		if (data_read)
			rawData->applyTransform(transformType, normalize_and_cpm);
	}

	void LoadGeneNames(H5::DataSet &dataset, Dataset<Points> pointsDataset)
	{
#ifndef HIDE_CONSOLE
		std::cout << "Loading Gene Names" << std::endl;
#endif
		std::vector<std::string> gene_names;
		std::vector<QString> dimensionNames;
		H5Utils::read_vector_string(dataset, dimensionNames);

		
		pointsDataset->setDimensionNames(dimensionNames);
	}

	void LoadSampleNames(H5::DataSet &dataset, Dataset<Points> points)
	{
#ifndef HIDE_CONSOLE
		std::cout << "Loading Sample Names" << std::endl;
#endif
		std::vector<QString> sample_names;
		H5Utils::read_vector_string(dataset, sample_names);

		
		points.setProperty("Sample Names", QList<QVariant>(sample_names.cbegin(), sample_names.cend()));
		
	}

	bool LoadSampleMeta(H5::Group &group, Dataset<Points> points, hdps::CoreInterface* _core)
	{
#ifndef HIDE_CONSOLE
		std::cout << "Loading MetaData" << std::endl;
#endif
		const int numFiles = 1; // assuming only 1 file for now
		const int fileID = 0;

		try
		{
			typedef float numericMetaDataType;
			std::vector<numericMetaDataType> numericalMetaData;
			std::vector<QString> numericalMetaDataDimensionNames;
			


			hsize_t nrOfGroupObjects = group.getNumObjs();
			for (auto go = 0; go < nrOfGroupObjects; ++go)
			{
				H5G_obj_t objectType2 = group.getObjTypeByIdx(go);
				std::string objectName2 = group.getObjnameByIdx(go);
				if (objectType2 == H5G_GROUP && objectName2 == "anno")
				{
					H5::Group anno = group.openGroup(objectName2);
					hsize_t nrOfAnnoObjects = anno.getNumObjs();
					for (auto ao = 0; ao < nrOfAnnoObjects; ++ao)
					{
						std::string name = anno.getObjnameByIdx(ao);
						std::size_t found_pos = name.rfind("_color");
						if (found_pos < name.length())
						{
							std::string label(name.begin(), name.begin() + found_pos);
							std::vector<QString> colorVector;
							H5Utils::read_vector_string(anno.openDataSet(name), colorVector);
							std::string labelDatasetString = label + "_label";
							if (!anno.exists(labelDatasetString))
							{
								std::cout << labelDatasetString << " expected but not found" << std::endl;
								if (anno.exists(label))
								{
									labelDatasetString = label;
									std::cout << labelDatasetString << " found instead" << std::endl;
								}
								else
									labelDatasetString.clear();
							}

							if (labelDatasetString.empty())
								continue;
							H5::DataSet labelDataSet = anno.openDataSet(labelDatasetString);
							std::vector<QString> labelVector;

							auto labelDataSetDataType = labelDataSet.getDataType();
							bool numericalValues = false;
							if (labelDataSetDataType.getClass() == H5T_STRING)
							{
								
								if(H5Utils::read_vector_string(labelDataSet, labelVector))
								{
									std::map<QString, std::vector<unsigned>> indices;
									std::map<QString, QColor> colors;
									bool all_ok = true;
									for (std::size_t i = 0; i < labelVector.size(); ++i)
									{
										QString current_label = labelVector[i];
										QColor current_color = colorVector[i];
										indices[current_label].push_back(i);
										if(colors.find(current_label) == colors.cend())
										{
											colors[current_label] = current_color;
										}
										else
										{
											all_ok = false;
											assert(colors[current_label] == current_color);
										}
									}

									if(all_ok)
									{
										H5Utils::addClusterMetaData(_core, indices, label.c_str(), points);
									}
								}
							}
							else
							{
								std::vector<double> labelVector;
								if(H5Utils::read_vector(anno, label + "_label", &labelVector, H5::PredType::NATIVE_DOUBLE))
								{
									std::map<double, std::vector<unsigned>> indices;
									std::map<double, QColor> colors;
									bool all_ok = true;
									for (std::size_t i = 0; i < labelVector.size(); ++i)
									{
										auto current_label = labelVector[i];//QString::number(labelVector[i],'f',12);
										indices[current_label].push_back(i);
										QColor current_color = colorVector[i];
										if (colors.find(current_label) == colors.cend())
										{
											colors[current_label] = current_color;
										}
										else
										{
											all_ok = false;
											assert(colors[current_label] == current_color);
										}
									}
									std::size_t threshold = 0.75 * labelVector.size();
									if (all_ok && (indices.size() < threshold))
									{
										
										int precision = 10;
										bool precision_ok = true;
										do
										{
											std::map<QString, std::vector<unsigned>> indicesS;
											std::map<QString, QColor> colorsS;
											precision_ok = true;
											for (auto it = indices.cbegin(); it != indices.cend(); ++it)
											{
												QString x = QString::number(it->first, 'f', precision);
												indicesS[x] = it->second;
												if (colorsS.find(x) == colorsS.cend())
												{
													colorsS[x] = colors[it->first];
												}
												else
												{
													precision_ok = false;
													++precision;
													std::cout << "increasing precision to " << precision << std::endl;
												}
											}
											if (precision_ok)
											{
												H5Utils::addClusterMetaData(_core, indicesS, label.c_str(), points, colorsS);
											}
										} while (precision_ok == false);
									}
									else
									{
										numericalMetaData.insert(numericalMetaData.end(), labelVector.cbegin(), labelVector.cend());
										numericalMetaDataDimensionNames.push_back(label.c_str());
									}
									
									numericalValues = true;
								}
							}
						}
					}
				}
			}
			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, points);
			return true;
		}
		catch (std::exception &)
		{
			return false;
		}
	}

}// namespace

HDF5_TOME_Loader::HDF5_TOME_Loader(hdps::CoreInterface *core)
{
	_core = core;
}

bool HDF5_TOME_Loader::open(const QString &fileName, TRANSFORM::Type conversionIndex, bool normalize)
{
	try
	{
		bool ok;
		QString dataSetName = QInputDialog::getText(nullptr, "Add New Dataset",
			"Dataset name:", QLineEdit::Normal, "DataSet", &ok);

		if (!ok || dataSetName.isEmpty())
		{
			return false;
		}

		auto points = _core->addDataset<Points>("Points", dataSetName);

		if (!points.isValid())
			return false;

		std::shared_ptr<DataContainerInterface> rawData(new DataContainerInterface(points.get<Points>()));

		H5::H5File file(fileName.toLatin1().constData(), H5F_ACC_RDONLY);

		auto nrOfObjects = file.getNumObjs();

		

		// first read mandatory stuff
		
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = file.getObjnameByIdx(fo);

			H5G_obj_t objectType1 = file.getObjTypeByIdx(fo);
			if (objectType1 == H5G_GROUP)
			{

				if (objectName1 == "data")
				{
					H5::Group group = file.openGroup(objectName1);
					TOME::LoadData(group, rawData, conversionIndex, normalize,0);
				}
				else if (objectName1 == "sample_meta")
				{
					H5::Group group = file.openGroup(objectName1);
					TOME::LoadSampleMeta(group, rawData->points(),_core);
				}
			}
			else if (objectType1 == H5G_DATASET)
			{

				if (objectName1 == "gene_names")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					TOME::LoadGeneNames(dataset, rawData->points());
				}
				else if (objectName1 == "sample_names")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					TOME::LoadSampleNames(dataset, rawData->points());
				}
			}
		}

// 		if (file.exists("projection"))
// 		{
// 			H5::Group group = file.openGroup("projection");
// 			TOME::LoadProjections(group, header, cytometryData()->metaData());
// 		}


		events().notifyDatasetAdded(points);


		return points.get();
	}
	catch (std::exception &e)
	{
		std::cout << "TOME Loader: " << e.what() << std::endl;
		return false;
	}

	return false;
}
	

