#pragma once

#include <memory>
#include <functional>
#include <cstdint>
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

/**
 * @brief Opens an H5AD file and imports its point data through a workflow.
 *
 * The loader owns the open HDF5 file and creates a concrete point dataset for
 * the import. Loading is scheduled asynchronously: the initial stage reads
 * dense or sparse X data into packed, tiled staging files, followed by a
 * metadata stage and a final publication stage. The staging directory is
 * externally owned and must remain available while the resulting dataset may
 * read its staged tiles or until project serialization copies them.
 *
 * The workflow receiver is not owned by this class. It is used as the
 * receiver for completion handling and must outlive the scheduled workflow.
 */
struct H5ADImportOptions
{
    QString datasetName;
    QStringList optionalObjects;
    QString stagingDirectory;
    int storageType = -3;
    bool useTiledStorage = true;
    mv::pointdata::storage::StorageBackendConfiguration tiledStorageConfiguration;
};

/** Type and sparsity summary produced by a bounded estimate or complete matrix scan. */
struct H5ADMatrixScanResult
{
	int recommendedTypeIndex = -1;
	std::uint64_t elementCount = 0;
	std::uint64_t zeroCount = 0;
	std::uint64_t storedEntryCount = 0;
	std::uint64_t analyzedValueCount = 0;
	double zeroFraction = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	bool sparsityAvailable = false;
	bool rangeAvailable = false;
	bool sparseInput = false;
};

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
	bool open(const QString &);

	/** @return Dimension names discovered from the file's var index. */
	const std::vector<QString> &getDimensionNames() const;

	/** Returns importable optional top-level objects, excluding X, uns, var, varm, and varp. */
	QStringList getOptionalObjects() const;

	/** Returns the logical rows and columns of X, or zeroes when its shape is unavailable. */
	std::pair<std::uint64_t, std::uint64_t> getMatrixShape() const;

	/** Estimates target element type and sparsity from a bounded sample of X. */
	H5ADMatrixScanResult estimateTargetElementType() const;
	/** Determines target element type, range, and sparsity by scanning every stored value in X. */
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
