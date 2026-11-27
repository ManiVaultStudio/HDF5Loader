#pragma once

#include "HDF5_AD_Loader.h"
#include "actions/ClusterSetSelectionAction.h"
#include "actions/DimensionSelectionAction.h"
#include "actions/ObsmMatrixSelectionAction.h"
#include "actions/TargetElementTypeAction.h"

#include <PointData/StorageBackendConfiguratorAction.h>
#include <actions/HorizontalGroupAction.h>
#include <actions/MessageCardAction.h>
#include <actions/OptionAction.h>
#include <actions/OptionsAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>
#include <actions/VerticalGroupAction.h>
#include <util/SeverityLevel.h>

#include <QDialog>
#include <QMap>

#include <cstddef>
#include <memory>

/**
 * @brief Collects import settings and data checks for an opened H5AD file.
 *
 * The dialog shares the loader so it can estimate or fully scan matrix values
 * in a workflow. Accepting the dialog returns choices to the caller; the
 * loader schedules the actual import separately. Core message-card actions
 * display the import checks. Optional callbacks capture the dialog and remain
 * stored by their member actions until replaced or destroyed with the dialog.
 */
class H5ADImportDialog : public QDialog
{
public:
    /**
     * Creates the dialog with file metadata, restored choices, sparse-matrix
     * availability, and a shared loader.
     * @param fileName H5AD file whose X and metadata will be imported.
     * @param stagingDirectory App-managed staging directory for tiled imports.
     * @param optionalObjects Importable optional HDF5 objects.
     * @param selectedOptionalObjects Optional objects selected in the previous import dialog.
     * @param rows Logical row count of X.
     * @param columns Logical column count of X.
     * @param storageTypeIndex Initial data-type option index.
     * @param backendType Initial storage backend name.
     * @param sparseMatrixInfo Result of metadata inspection used to determine whether Sparse can be selected.
     * @param tiledConfiguration Initial tile geometry and cache settings.
     * @param bucketingThreadCount Sparse CSR bucketing worker limit, or zero to use the default.
     * @param stagingThreadCount Packed tile staging worker limit, or zero to use the default.
     * @param rowChunkTileFactor Number of tile rows grouped into sparse staging batches, or zero to use the default.
     * @param zstdCompressionLevel Zstandard compression level in [1, 22], with lower levels favoring encoding speed.
     * @param loader Shared loader retained for matrix analysis and import checks.
     * @param parent Optional parent dialog.
     */
    H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QStringList& optionalObjects, const QStringList& selectedOptionalObjects, const QString& benchmarkDirectory, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const H5ADSparseMatrixInfo& sparseMatrixInfo, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, const mv::pointdata::storage::SparseStorageBackendConfiguration& sparseConfiguration, std::size_t bucketingThreadCount, std::size_t stagingThreadCount, std::size_t rowChunkTileFactor, int zstdCompressionLevel, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent = nullptr);

    /** Returns current import choices; callers should use them after acceptance. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

private:
    /** Enables import only when analysis, memory, and dataset-name checks permit it. */
    void updateImportActionState();

    /** Recomputes the import preflight messages for the current settings. */
    void updateImportChecks();

    /** Updates the obsm estimate card and refreshes the combined import memory card. */
    void updateObsmImportInfo(const QStringList& selectedMatrices);

    /** Updates the memory card and optionally offers switching to tiled storage. */
    void updateMemoryCheck(mv::util::SeverityLevel severity, const QString& description, bool offerTiledStorage = false);

    /** Updates the precision guidance shown inside the data summary card. */
    void updatePrecisionCheck(mv::util::SeverityLevel severity, const QString& description);

    /** Starts the bounded sample analysis requested by the estimate control. */
    void estimateTargetElementType();

    /** Opens tiled storage configuration and its staging-space observation. */
    void configureTiledStorage();

    /** Opens the pre-import matrix dimension picker. */
    void selectDimensions();

    /** Opens sparse storage configuration for an HDF5 sparse matrix. */
    void configureSparseStorage();

    /** Opens matrix selection for the obsm metadata group. */
    void configureObsmMatrices();

    /** Opens cluster-set selection for categorical obs metadata. */
    void configureObsClusterSets();

    /** Refreshes the cluster-count pre-flight card from the current selection. */
    void updateClusterSetPreflightCheck();

    /** Opens backend configuration for the requested obsm matrix. */
    void configureObsmMatrixStorage(const QString& matrixName, std::uint64_t rows, std::uint64_t columns, mv::pointdata::storage::StorageBackendType storageType);

    /** Starts a bounded sample or full scan workflow for the target element type. */
    void analyzeTargetElementType(bool fullScan);

    /** Updates the data summary card and exposes full scanning when available. */
    void updateDataSummaryCard();

    mv::gui::VerticalGroupAction     _settingsAction;           /**< Top-level group containing import settings and checks. */
    mv::gui::StringAction            _fileAction;               /**< Displays the selected H5AD file name. */
    mv::gui::StringAction            _matrixAction;             /**< Displays the dimensions of the H5AD X matrix. */
    mv::gui::StringAction            _datasetNameAction;        /**< Editable name assigned to the imported dataset. */
    mv::gui::VerticalGroupAction     _dimensionSelectionGroup; /**< Collapsible source-dimension selection. */
    DimensionSelectionAction         _dimensionSelectionAction; /**< HDF5-owned table selector for X dimensions. */
    mv::gui::TriggerAction            _dimensionSelectionTrigger; /**< Opens the dimension-selection dialog. */
    TargetElementTypeAction          _storageTypeAction;        /**< Selects the target element type and requests its estimate. */
    StorageBackendConfiguratorAction _backendSelector;          /**< Selects the destination storage backend and opens its configuration. */
    mv::gui::OptionAction            _projectOpenFileStagingAction; /**< Dataset policy for file staging when a saved project is opened. */
    QString                          _stagingDirectory;         /**< App-managed staging directory used by this import. */
    std::size_t                      _bucketingThreadCount = 4; /**< Maximum parallel workers used to bucket sparse entries. */
    std::size_t                      _stagingThreadCount = 4;   /**< Maximum parallel workers used to materialize and encode output tiles. */
    std::size_t                      _rowChunkTileFactor = 3;   /**< Tile-height multiple used to batch sparse CSR staging work. */
    int                              _zstdCompressionLevel = 3;  /**< Zstandard level restored from and returned to import settings. */
    mv::gui::VerticalGroupAction _checksAction;            /**< Vertical group containing the import-check cards. */
    mv::gui::MessageCardAction   _memoryCheckAction;       /**< Memory estimate and optional tiled-storage callback. */
    mv::gui::MessageCardAction   _dataSummaryCheckAction;  /**< Matrix analysis summary and optional full-scan callback. */

    mv::gui::OptionsAction       _optionalObjectsAction;  /**< Optional HDF5 metadata objects selected for import. */
    QString _benchmarkDirectory; /**< Initial directory for choosing import-benchmark output files. */
    QString _benchmarkFilePath; /**< Selected import-benchmark output file path; empty disables report output. */
    mv::gui::VerticalGroupAction _clusterSetImportAction; /**< Root action for the obs cluster-set configuration dialog. */
    ClusterSetSelectionAction _clusterSetSelectionAction; /**< Selects categorical fields under obs for cluster creation. */
    mv::gui::VerticalGroupAction _clusterSetDialogChecksAction; /**< Pre-flight checks shown beneath the cluster-set table. */
    mv::gui::MessageCardAction _clusterSetCountInfoAction; /**< Summary of the selected cluster sets' available category counts. */
    QMap<QString, H5ADClusterSetInfo> _availableClusterSets; /**< Discovered categorical obs fields and counts when available. */
    QStringList _selectedClusterSets; /**< Cluster sets selected in the configuration dialog. */
    bool _clusterSetSelectionConfigured = false; /**< Whether the user accepted a cluster-set selection. */
    mv::gui::VerticalGroupAction _obsmMatrixImportAction; /**< Root action for the obsm matrix import dialog. */
    ObsmMatrixSelectionAction _obsmMatrixSelectionAction; /**< Vertical group of per-matrix import configuration actions. */
    QStringList _obsmMatrices; /**< Names of numeric dense matrices inside obsm. */
    QStringList _selectedObsmMatrices; /**< Matrices selected for import in the obsm configuration dialog. */
    QMap<QString, mv::pointdata::storage::StorageBackendType> _obsmStorageTypes; /**< Selected storage backend per obsm matrix. */
    QMap<QString, int> _obsmElementTypes; /**< Explicit target element type per configured obsm matrix. */
    QMap<QString, mv::pointdata::storage::StorageBackendConfiguration> _obsmTiledConfigurations; /**< Tiled settings per obsm matrix. */
    QMap<QString, std::uint64_t> _metadataSizeEstimates;  /**< Estimated payload bytes for optional objects, keyed by HDF5 object name. */

    mv::gui::VerticalGroupAction _importInfoAction; /**< Information cards describing estimated import size and data checks. */
    mv::gui::MessageCardAction _obsmMemoryInfoAction; /**< Estimated memory required by selected obsm matrices. */
    mv::gui::VerticalGroupAction _obsmDialogMatrixAction; /**< Matrix table and obsm size estimate in the configuration dialog. */
    mv::gui::VerticalGroupAction _obsmDialogChecksAction; /**< Import preflight cards in the configuration dialog. */

    mv::gui::HorizontalGroupAction _buttonsAction;  /**< Bottom-row dialog buttons and leading stretch. */
    mv::gui::TriggerAction         _cancelAction;   /**< Closes the dialog without accepting its settings. */
    mv::gui::TriggerAction         _importAction;   /**< Accepts the dialog when the current preflight allows import. */

    mv::pointdata::storage::StorageBackendConfiguration _tiledConfiguration;  /**< Tiled-storage settings selected for this import. */
    mv::pointdata::storage::SparseStorageBackendConfiguration _sparseConfiguration; /**< Sparse CSR/CSC indexes selected for this import. */

    H5ADSparseMatrixInfo _sparseMatrixInfo;  /**< Sparse-X layout snapshot used to expose only a compatible backend choice. */

    bool                    _sourceMayLosePrecision   = false;                          /**< Whether conversion from the source representation may lose precision. */
    mv::util::SeverityLevel _dataSummarySeverity      = mv::util::SeverityLevel::Info;  /**< Severity currently assigned to the data summary. */
    QString                 _dataSummaryDescription;                                    /**< Current matrix type and sparsity summary. */
    mv::util::SeverityLevel _precisionSeverity        = mv::util::SeverityLevel::Info;  /**< Severity currently assigned to precision guidance. */
    QString                 _precisionDescription;                                      /**< Current precision and sample-bound guidance. */
    mv::util::SeverityLevel _memoryCheckSeverity      = mv::util::SeverityLevel::Info;  /**< Base severity for the main matrix memory estimate. */
    QString                 _memoryCheckDescription;                                    /**< Base memory estimate description. */
    bool                    _memoryCheckOfferTiledStorage = false;                     /**< Whether the combined memory card offers tiled storage. */
    mv::util::SeverityLevel _obsmMemoryInfoSeverity   = mv::util::SeverityLevel::Info;  /**< Severity assigned to the selected obsm matrices' memory estimate. */
    QString                 _obsmMemoryInfoDescription;                                 /**< Selected obsm matrices' memory estimate description. */
    int                     _recommendedTypeIndex     = -1;                             /**< Recommended option index, or -1 before analysis completes. */
    bool                    _fullScanComplete         = false;                          /**< Whether an exhaustive matrix-value scan completed successfully. */
    bool                    _analysisRunning          = false;                          /**< Whether a matrix sampling or full-scan workflow is active. */
    bool                    _denseMemoryLimitExceeded = false;                          /**< Whether the selected dense or sparse payload exceeds the configured RAM budget. */

    std::shared_ptr<HDF5_AD_Loader> _loader;       /**< Shared loader retained while estimating and checking this import. */
    std::uint64_t                   _rows    = 0;  /**< Row count of the H5AD X matrix. */
    std::uint64_t                   _columns = 0;  /**< Column count of the H5AD X matrix. */
};
