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

	int loadFromFile(std::string _fileName, std::shared_ptr<DataContainerInterface> &rawData, int optimization, int conversionOption)
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
				else
					result = false;

				if (!result)
					return false;

				std::size_t rows = barcodes.size();
				assert(indptr.size() == (rows + 1));
				std::size_t columns = genes.size();
				rawData->resize(rows, columns);
				rawData->set_sparse_row_data(indices, indptr, data, conversionOption);
				PointsPlugin *pointsPlugin = rawData->pointsPlugin();
				
				std::vector<QString> dimensionNames(genes.size());
				#pragma omp parallel for
				for (int64_t i = 0; i < genes.size(); ++i)
				{
					dimensionNames[i] = genes[i].c_str();
				};
				pointsPlugin->setDimensionNames(dimensionNames);

				QList<QVariant> sample_names;
				for (int64_t i = 0; i < barcodes.size(); ++i)
				{
					sample_names.append(barcodes[i].c_str());
				}
				pointsPlugin->setProperty("Sample Names", sample_names);
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

PointsPlugin * HDF5_10X_Loader::open(const QString &fileName, int conversionIndex, int speedIndex)
{


	
	
	PointsPlugin *pointsPlugin = nullptr;
	try
	{
		bool ok;
		QString dataSetName = QInputDialog::getText(nullptr, "Add New Dataset",
			"Dataset name:", QLineEdit::Normal, "DataSet", &ok);

		if (ok && !dataSetName.isEmpty()) {
			QString name = _core->addData("Points", dataSetName);
			const IndexSet& set = dynamic_cast<const IndexSet&>(_core->requestSet(name));
			pointsPlugin = &(set.getData());
		}
		if (pointsPlugin == nullptr)
			return nullptr;
		QGuiApplication::setOverrideCursor(Qt::WaitCursor);
		std::shared_ptr<DataContainerInterface> rawData(new DataContainerInterface(pointsPlugin));

		HDF5_10X::loadFromFile(fileName.toStdString(), rawData, speedIndex, conversionIndex);


		_core->notifyDataAdded(pointsPlugin->getName());
	}
	catch (std::exception &e)
	{
		std::cout << "HDF5 10x Loader: " << e.what() << std::endl;
		return nullptr;
	}

	QGuiApplication::restoreOverrideCursor();
	return pointsPlugin;
}
	

