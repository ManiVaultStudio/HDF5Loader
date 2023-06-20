#pragma once
#include "H5Utils.h"
#include "PointData/PointData.h"
#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"
#include <set>
namespace H5AD
{
	void CreateColorVector(std::size_t nrOfColors, std::vector<QColor>& colors);

	void LoadData(const H5::DataSet& dataset, Dataset<Points> pointsDataset);

	void LoadData(H5::Group& group, Dataset<Points>& pointsDataset);

	void LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result);

	void LoadIndexStrings(H5::Group& group, std::vector<QString>& result);


	bool LoadSparseMatrix(H5::Group& group, Dataset<Points>& pointsDataset, CoreInterface* _core);

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>>& categories);

	DataHierarchyItem* GetDerivedDataset(const QString& name, Dataset<Points>& pointsDataset);

	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>>& result);

	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, Dataset<Points> pointsDataset);

	
	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core);
	void LoadSampleNamesAndMetaDataBFloat16(H5::DataSet& dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core);
	
	
	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, Dataset<Points>  pointsDataset, hdps::CoreInterface* _core);
	void LoadSampleNamesAndMetaDataBFloat16(H5::Group& group, Dataset<Points>  pointsDataset, hdps::CoreInterface* _core);

} // namespace H5AD

