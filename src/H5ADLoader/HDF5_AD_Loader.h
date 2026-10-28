#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QString>
#include <QStringList>
#include <QMap>

class QObject;

#include "H5Utils.h"
#include <PointData/storage/interface/PointsStorageBackendTypes.h>

namespace mv
{
	class CoreInterface;
}

/**
 * @brief Options collected by the H5AD import dialog and consumed by the import workflow.
 *
 * Sparse storage is available only for an encoded CSR or CSC X matrix. It
 * retains the compressed representation in memory and does not use the tiled
 * staging directory. Dense X matrices can be loaded eagerly or into tiled
 * storage.
 */
struct H5ADImportOptions
{
	QString datasetName; /**< Name assigned to the imported point dataset. */
	QStringList optionalObjects; /**< Optional top-level metadata objects selected for import. */
	QString stagingDirectory; /**< Directory used for packed tiled data; it must remain available while source-backed tiles are used or until serialization copies them. */
	int storageType = -3; /**< X type choice: -3 optimized lossless, -2 optimized with bfloat16, -1 source type, or a PointData element-type index. Applied to eager and sparse imports as well as tiled imports. */
	mv::pointdata::storage::StorageBackendType storageBackendType = mv::pointdata::storage::StorageBackendType::Tiled; /**< Backend for X. Sparse retains CSR/CSC arrays in memory and requires a supported sparse X group. */
	mv::pointdata::storage::StorageBackendConfiguration tiledStorageConfiguration; /**< Tile geometry and cache configuration used when tiled storage is selected. */
};

/**
 * @brief Summary of target type, value range, and sparsity analysis for X.
 *
 * Sampling may leave the counts and range approximate; a full scan analyzes
 * the complete logical matrix. Sparse implicit zeros contribute to logical
 * element and zero counts, while storedEntryCount counts only encoded values.
 */
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
 * @brief Describes an encoded sparse X matrix before import.
 *
 * This inspects HDF5 structure and dataset shapes without reading matrix
 * values. When available is false, X did not have a recognized CSR or CSC
 * group structure or its structural metadata could not be read. The layout and
 * count fields are meaningful only when available is true; compressed pointer
 * and index contents are validated during import.
 */
struct H5ADSparseMatrixInfo
{
	bool available = false; /**< Whether X is an encoded CSR or CSC sparse matrix. */
	bool columnCompressed = false; /**< Whether X uses CSC storage; otherwise the supported layout is CSR. */
	std::uint64_t storedEntryCount = 0; /**< Number of explicitly stored entries, including any explicitly stored zeros. */
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

	/**
	 * Estimates uncompressed fixed-size dataset payload bytes for optional
	 * top-level objects. Returns only objects that could be inspected; HDF5
	 * container overhead and variable-length values are not included.
	 */
	QMap<QString, std::uint64_t> getOptionalObjectSizeEstimates() const;

	/** Returns the logical rows and columns of X, or zeroes when its shape is unavailable. */
	std::pair<std::uint64_t, std::uint64_t> getMatrixShape() const;

	/**
	 * Inspects X metadata to determine whether it has a supported CSR or CSC
	 * representation and returns its layout and stored-entry count. This does
	 * not read matrix values; an incomplete or unsupported sparse-group
	 * structure, or unreadable structural metadata, produces an unavailable
	 * result. Pointer and index contents are checked during import.
	 */
	H5ADSparseMatrixInfo getSparseMatrixInfo() const;

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
	 * @param options Dataset name, selected metadata, element type, backend, and
	 *        storage settings. Sparse storage requires supported encoded sparse
	 *        X data and reads its compressed arrays into memory.
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
