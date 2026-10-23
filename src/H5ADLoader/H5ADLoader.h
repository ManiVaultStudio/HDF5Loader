#pragma once

#include <LoaderPlugin.h>

#include <PointData/StorageBackendConfiguratorAction.h>
#include <PointData/storage/interface/PointsStorageBackendTypes.h>

#include <QFileDialog>

using namespace mv::plugin;

// =============================================================================
// Loader
// =============================================================================

/**
 * @brief ManiVault plugin for importing H5AD point data.
 *
 * The plugin presents file and storage-type controls. Staging uses the core
 * application-managed staging directory. Each selected file is opened by an
 * HDF5_AD_Loader, which schedules the import workflow and keeps staged packed
 * tiles available for later project serialization. The staged files are
 * removed when their source-backed point-data storage is released.
 */
class H5ADLoader :  public LoaderPlugin
{
public:
	/** Creates the H5AD loader plugin. */
	H5ADLoader(PluginFactory* factory);

	/** Releases the loader plugin and its file-dialog state. */
    ~H5ADLoader() Q_DECL_OVERRIDE;

	/** Initializes the plugin actions and file-dialog settings. */
    void init() Q_DECL_OVERRIDE;

	/** Opens the H5AD import dialog and schedules selected files for loading. */
    void loadData() Q_DECL_OVERRIDE;

private:
	QFileDialog _fileDialog = QFileDialog{};
	StorageBackendConfiguratorAction _storageBackendSelector; /**< Shared backend choice and configuration controls embedded in the file dialog. */
	mv::pointdata::storage::StorageBackendConfiguration _tiledStorageConfiguration; /**< Persisted defaults passed to tiled X staging. */
};
