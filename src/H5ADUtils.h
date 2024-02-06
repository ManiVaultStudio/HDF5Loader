#pragma once
#include "H5Utils.h"
#include "PointData/PointData.h"
#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"
#include <set>
namespace H5AD
{
	struct DatasetInfo
	{
		Dataset<Points> _pointsDataset;
		std::vector<QString> _originalDimensionNames;
		std::vector<bool> _enabledDimensions;
		std::vector<std::ptrdiff_t> _selectedDimensionsLUT;
	};

	void CreateColorVector(std::size_t nrOfColors, std::vector<QColor>& colors);

	void LoadData(const H5::DataSet& dataset, Dataset<Points> pointsDataset, int storageType);

	std::vector<std::ptrdiff_t> LoadData(H5::Group& group, Dataset<Points>& pointsDataset, int storageType);

	std::string LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result);

	std::string LoadIndexStrings(H5::Group& group, std::vector<QString>& result);


	bool LoadSparseMatrix(H5::Group& group, Dataset<Points>& pointsDataset, CoreInterface* _core);

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>>& categories);

	DataHierarchyItem* GetDerivedDataset(const QString& name, Dataset<Points>& pointsDataset);

	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>>& result);

	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, DatasetInfo &datasetInfo, int storage_type);

	
	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, Dataset<Points> pointsDataset, int storage_type);
	
	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, Dataset<Points>  pointsDataset, int storage_type);
	

} // namespace H5AD

