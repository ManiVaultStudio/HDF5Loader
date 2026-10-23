#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QString>

class QObject;

#include "H5Utils.h"

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

	/**
	 * @brief Schedules import of the opened file.
	 *
	 * The call returns after the workflow has been submitted. Dataset creation,
	 * tile staging, metadata loading, and hierarchy publication happen on the
	 * workflow executor. A failed workflow removes the dataset it created.
	 *
	 * @param storageType Requested point-data element/storage type.
	 * @param useTiledStorage Whether to stage X in tiled storage; false selects the eager loader.
	 * @param tiledStorageConfiguration Tiled storage geometry and cache configuration used when tiled loading is enabled.
	 * @return True when the import workflow was submitted; false when the loader
	 *         has no usable file contents or scheduling fails synchronously.
	 */
	bool load(int storageType, bool useTiledStorage = true, const mv::pointdata::storage::StorageBackendConfiguration& tiledStorageConfiguration = {});
	
private:
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
