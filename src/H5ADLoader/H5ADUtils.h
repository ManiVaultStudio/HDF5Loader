#pragma once

#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"
#include "H5Utils.h"
#include "PointData/PointData.h"

#include <functional>

namespace H5AD
{
	/** Reports normalized staging progress in the range [0, 1]. */
	using ProgressCallback = std::function<void(float)>;

	/** Reports normalized metadata progress and the child HDF5 object being processed. */
	using MetadataProgressCallback = std::function<void(float, const QString&)>;

	/**
	 * @brief Shared state passed through H5AD staging and metadata operations.
	 *
	 * The point dataset is owned by the ManiVault data model. The source file
	 * name, staging directory, and discovered names are copied into this state;
	 * the staging directory itself remains externally owned and must stay valid
	 * until consumers no longer need the staged tiles.
	 */
	struct LoaderInfo
	{
		mv::Dataset<Points>          _pointsDataset;             /**< Point dataset being populated. */
		QString                      _sourceFileName;            /**< Source H5AD file path. */
		QString                      _stagingDirectory;         /**< Externally owned packed-tile staging directory. */
		std::vector<QString>         _originalDimensionNames;    /**< Dimension names discovered from var. */
		QVariantList                 _sampleNames;               /**< Sample names discovered from obs. */
		std::vector<bool>            _enabledDimensions;         /**< Optional dimension-selection mask. */
		std::vector<std::ptrdiff_t>  _selectedDimensionsLUT;     /**< Optional lookup from selected to source dimensions. */
	};

	void CreateColorVector(std::size_t nrOfColors, std::vector<QColor>& colors);

	void LoadData(const H5::DataSet& dataset, LoaderInfo& loaderInfo, int storageType);

	void LoadData(H5::Group& group, LoaderInfo& datasetInfo, int storageType);

	std::string LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result);

	std::string LoadIndexStrings(H5::Group& group, std::vector<QString>& result);

	bool ContainsSparseMatrix(H5::Group& group);

	bool LoadSparseMatrix(H5::Group& group, LoaderInfo& loaderInfo);

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>>& categories);

	mv::DataHierarchyItem* GetDerivedDataset(const QString& name, mv::Dataset<Points>& pointsDataset);

	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>>& result);

	/**
	 * Stages the H5AD X matrix as packed tiled storage and reports progress.
	 * @param h5fILE Open HDF5 file; must remain valid for the duration of the call.
	 * @param loaderInfo Import state and destination dataset.
	 * @param storage_type Requested point-data element/storage type.
	 * @param progress Optional callback receiving normalized tile-staging progress.
	 * @return True when X was staged successfully; false when no supported X object was found.
	 * @throws std::exception When HDF5 access or staging-file creation fails.
	 */
	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, LoaderInfo &loaderInfo, int storage_type, ProgressCallback progress = {});

	/**
	 * Loads floating-point sample metadata from one HDF5 dataset.
	 * @param dataset HDF5 dataset containing compound metadata fields.
	 * @param loaderInfo Import state and destination dataset.
	 * @param progress Optional callback receiving field-level progress.
	 */
	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, LoaderInfo &loaderInfo, MetadataProgressCallback progress = {});
	
	/**
	 * Loads floating-point sample metadata from an HDF5 group, including nested
	 * datasets and sparse metadata representations.
	 * @param group HDF5 group to inspect.
	 * @param loaderInfo Import state and destination dataset.
	 * @param progress Optional callback receiving child-object progress.
	 */
	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, LoaderInfo& loaderInfo, MetadataProgressCallback progress = {});
	

} // namespace H5AD

