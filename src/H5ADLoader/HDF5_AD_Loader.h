#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QString>
#include <QStringList>

class QObject;

#include "H5Utils.h"
#include <PointData/storage/interface/PointsStorageBackendTypes.h>

namespace mv
{
	class CoreInterface;
}

/** Options collected by the H5AD import dialog and consumed by the import workflow. */
struct H5ADImportOptions
{
	QString datasetName; /**< Name assigned to the imported point dataset. */
	QStringList optionalObjects; /**< Optional top-level metadata objects selected for import. */
	QString stagingDirectory; /**< Directory used for staged matrix data; it must remain available while source-backed tiles are used. */
	int storageType = -3; /**< X type choice: -3 optimized lossless, -2 optimized with bfloat16, -1 source type, or a PointData element-type index. */
	bool useTiledStorage = true; /**< Whether X is staged in tiled storage rather than eager storage. */
	mv::pointdata::storage::StorageBackendConfiguration tiledStorageConfiguration; /**< Tile geometry and cache configuration used when tiled storage is selected. */
};

/** Summary of target type, range, and sparsity calculated from sampled or complete matrix values. */
struct H5ADMatrixScanResult
{
	int recommendedTypeIndex = -1; /**< PointData element-type option index, or -1 when no recommendation is available. */
	std::uint64_t elementCount = 0; /**< Total logical matrix entries, including implicit sparse zeros. */
	std::uint64_t zeroCount = 0; /**< Number of zeros represented by the analyzed values. */
	std::uint64_t storedEntryCount = 0; /**< Number of explicitly stored entries for sparse input. */
	std::uint64_t analyzedValueCount = 0; /**< Number of values inspected; smaller than elementCount for a bounded estimate. */
	double zeroFraction = 0.0; /**< Fraction of logical entries that are zero, when sparsityAvailable is true. */
	double minimum = 0.0; /**< Minimum analyzed finite value, when rangeAvailable is true. */
	double maximum = 0.0; /**< Maximum analyzed finite value, when rangeAvailable is true. */
	bool sparsityAvailable = false; /**< Whether zeroFraction and the related counts describe available input data. */
	bool rangeAvailable = false; /**< Whether minimum and maximum describe at least one finite value. */
	bool sparseInput = false; /**< Whether X uses a sparse HDF5 representation. */
};

/**
 * @brief Opens an H5AD file and imports its point data through a workflow.
 *
 * The loader owns the open HDF5 file and creates a concrete point dataset for
 * the import. Loading is scheduled asynchronously: the initial stage reads
 * dense or sparse X data into the selected staging directory when tiled
 * storage is selected, followed by metadata loading and dataset publication.
 * The staging directory is externally owned and must remain available while
 * the resulting dataset may read its staged tiles or until project
 * serialization copies them.
 *
 * The workflow receiver is not owned by this class. It is used as the
 * receiver for completion handling and must outlive the scheduled workflow.
 */
class HDF5_AD_Loader : public std::enable_shared_from_this<HDF5_AD_Loader>
{
public:
	/**
	 * @brief Constructs an H5AD loader.
	 * @param core Core interface used to create and publish datasets.
	 * @param workflowReceiver QObject that receives asynchronous completion callbacks; not owned.
	 * @param stagingDirectory Directory in which packed tile staging files are created.
	 */
	HDF5_AD_Loader(mv::CoreInterface* core, QObject* workflowReceiver, const QString& stagingDirectory);
	
	/**
	 * @brief Opens an H5AD file and discovers its X, var, and obs objects.
	 * @param fileName H5AD file to open.
	 * @return True when the file can be opened and contains a supported data object; false otherwise.
	 */
	bool open(const QString& fileName);

	/** @return Dimension names discovered from the file's var index. */
	const std::vector<QString> &getDimensionNames() const;

	/** Returns importable optional top-level objects, excluding X, uns, var, varm, and varp. */
	QStringList getOptionalObjects() const;

	/** Returns the logical rows and columns of X, or zeroes when its shape is unavailable. */
	std::pair<std::uint64_t, std::uint64_t> getMatrixShape() const;

	/** Estimates target element type and sparsity from a bounded sample of X. */
	H5ADMatrixScanResult estimateTargetElementType() const;
	/**
	 * Determines target type, range, and sparsity by scanning X and reports
	 * normalized progress in the range [0, 1]. Sparse implicit zeros are included
	 * in logical counts; progress may be empty.
	 * @param progress Optional callback for normalized scan progress.
	 */
	H5ADMatrixScanResult determineTargetElementType(const std::function<void(double)>& progress) const;

	/** Reports whether the source HDF5 type may lose precision in the supported point-data types. */
	bool sourceMayLosePrecision() const;

	/**
	 * @brief Schedules import of the opened file.
	 *
	 * The call returns after the workflow has been submitted. Dataset creation,
	 * tile staging, metadata loading, and hierarchy publication happen on the
	 * workflow executor. A failed workflow removes the dataset it created.
	 *
	 * @param options Dataset name, selected metadata, element type, and storage settings.
	 * @return True when the import workflow was submitted; false when the loader
	 *         has no usable file contents or scheduling fails synchronously.
	 */
	bool load(const H5ADImportOptions& options);
	
private:
	H5ADMatrixScanResult analyzeTargetElementType(bool fullScan, const std::function<void(double)>& progress) const;

	mv::CoreInterface* _core = nullptr;
	QObject* _workflowReceiver = nullptr;
	QString _stagingDirectory = {};
	std::unique_ptr<H5::H5File> _file = nullptr;
	std::vector<QString> _dimensionNames = {};
	std::vector<QString> _sampleNames = {};

	QString _fileName = {};

	std::string _var_indexName = {};
	std::string _obs_indexName = {};
};
