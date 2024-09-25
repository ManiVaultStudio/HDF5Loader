#pragma once
#include "H5Utils.h"

#include "PointData/PointData.h"
#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"

namespace H5AD
{
	struct LoaderInfo
	{
		Dataset<Points> _pointsDataset;
		std::vector<QString> _originalDimensionNames;
		QVariantList _sampleNames;
		std::vector<bool> _enabledDimensions;
		std::vector<std::ptrdiff_t> _selectedDimensionsLUT;
	};

	void CreateColorVector(std::size_t nrOfColors, std::vector<QColor>& colors);

	void LoadData(const H5::DataSet& dataset, LoaderInfo& loaderInfo, int storageType);

	void LoadData(H5::Group& group, LoaderInfo& datasetInfo, int storageType);

	std::string LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result);

	std::string LoadIndexStrings(H5::Group& group, std::vector<QString>& result);

	bool LoadSparseMatrix(H5::Group& group, LoaderInfo& loaderInfo);

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>>& categories);

	DataHierarchyItem* GetDerivedDataset(const QString& name, Dataset<Points>& pointsDataset);

	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>>& result);

	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, LoaderInfo &loaderInfo, int storage_type);

	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, LoaderInfo &loaderInfo);
	
	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, LoaderInfo& loaderInfo);
	

} // namespace H5AD

