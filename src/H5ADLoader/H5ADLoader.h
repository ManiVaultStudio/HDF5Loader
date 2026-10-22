#pragma once

#include <LoaderPlugin.h>

#include <QFileDialog>

using namespace mv::plugin;

// =============================================================================
// Loader
// =============================================================================

/**
 * @brief ManiVault plugin for importing H5AD point data.
 *
 * The plugin presents file, storage-type, and staging-directory controls.
 * Each selected file is opened by an HDF5_AD_Loader, which schedules the
 * import workflow and keeps staged packed tiles available for later project
 * serialization.
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
};
