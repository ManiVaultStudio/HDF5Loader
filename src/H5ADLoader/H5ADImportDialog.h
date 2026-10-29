#pragma once

#include "HDF5_AD_Loader.h"

#include <PointData/StorageBackendConfiguratorAction.h>
#include <actions/DirectoryPickerAction.h>
#include <actions/HorizontalGroupAction.h>
#include <actions/MessageCardAction.h>
#include <actions/OptionAction.h>
#include <actions/OptionsAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>
#include <actions/VerticalGroupAction.h>
#include <util/SeverityLevel.h>

#include <QDialog>
#include <QPointer>

#include <memory>

/**
 * @brief Combines element-type selection with an estimate request.
 *
 * Estimation is performed by the dialog's workflow; this action only exposes
 * the controls and emits a request signal.
 */
class TargetElementTypeAction : public mv::gui::HorizontalGroupAction
{
    Q_OBJECT

public:
    TargetElementTypeAction(QObject* parent, const QString& title);

    mv::gui::OptionAction& getTypeAction();
    const mv::gui::OptionAction& getTypeAction() const;
    /** Disables the controls during analysis and updates the estimate-button label. */
    void setAnalysisRunning(bool running, bool estimating = false);

    /** Selects the recommended option when index is within the available choices. */
    void setRecommendedTypeIndex(int index);

signals:
    /** Emitted when the user requests a bounded estimate; the dialog owns the scan workflow. */
    void estimateRequested();

private:
    mv::gui::OptionAction  _typeAction;      /**< Target element-type selector. */
    mv::gui::TriggerAction _estimateAction;  /**< Requests analysis of the matrix values. */
};

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
     * @param stagingDirectory Initial staging directory for tiled imports.
     * @param defaultStagingDirectory Directory restored by the reset action.
     * @param optionalObjects Importable optional HDF5 objects.
     * @param rows Logical row count of X.
     * @param columns Logical column count of X.
     * @param storageTypeIndex Initial data-type option index.
     * @param backendType Initial storage backend name.
     * @param sparseMatrixInfo Result of metadata inspection used to determine whether Sparse can be selected.
     * @param tiledConfiguration Initial tile geometry and cache settings.
     * @param loader Shared loader retained for matrix analysis and import checks.
     * @param parent Optional parent dialog.
     */
    H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const H5ADSparseMatrixInfo& sparseMatrixInfo, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent = nullptr);

    /** Returns current import choices; callers should use them after acceptance. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

    /** Returns the staging directory selected for this import, including for eager staging. */
    QString stagingDirectory() const;

private:
    /** Enables import only when analysis, memory, and dataset-name checks permit it. */
    void updateImportActionState();

    /** Recomputes the import preflight messages for the current settings. */
    void updateImportChecks();

    /** Updates the memory card and optionally offers switching to tiled storage. */
    void updateMemoryCheck(mv::util::SeverityLevel severity, const QString& description, bool offerTiledStorage = false);

    /**
     * Updates the staging card with the requested directory actions.
     * @param offerChooseDirectory Adds an action that opens the staging directory picker.
     * @param offerResetDirectory Adds an action that restores the default staging directory.
     */
    void updateStagingCheck(mv::util::SeverityLevel severity, const QString& description, bool offerChooseDirectory, bool offerResetDirectory);

    /** Updates the precision card and offers resetting to the recommended type when applicable. */
    void updatePrecisionCheck(mv::util::SeverityLevel severity, const QString& description);

    /** Starts the bounded sample analysis requested by the estimate control. */
    void estimateTargetElementType();

    /** Starts a bounded sample or full scan workflow for the target element type. */
    void analyzeTargetElementType(bool fullScan);

    /** Updates the data summary card and exposes full scanning when available. */
    void updateDataSummaryCard();

    mv::gui::VerticalGroupAction     _settingsAction;           /**< Top-level group containing import settings and checks. */
    mv::gui::StringAction            _fileAction;               /**< Displays the selected H5AD file name. */
    mv::gui::StringAction            _matrixAction;             /**< Displays the dimensions of the H5AD X matrix. */
    mv::gui::StringAction            _datasetNameAction;        /**< Editable name assigned to the imported dataset. */
    TargetElementTypeAction          _storageTypeAction;        /**< Selects the target element type and requests its estimate. */
    StorageBackendConfiguratorAction _backendSelector;          /**< Selects the destination storage backend and opens its configuration. */
    mv::gui::HorizontalGroupAction   _stagingActions;           /**< Groups staging-directory controls. */
    mv::gui::DirectoryPickerAction   _stagingDirectoryAction;   /**< Staging directory used when importing with tiled storage. */
    QString                          _defaultStagingDirectory;  /**< Directory restored by the staging reset action. */

    mv::gui::VerticalGroupAction _checksAction;            /**< Vertical group containing the import-check cards. */
    mv::gui::MessageCardAction   _memoryCheckAction;       /**< Memory estimate and optional tiled-storage callback. */
    mv::gui::MessageCardAction   _stagingCheckAction;      /**< Staging-space estimate and optional directory callbacks. */
    mv::gui::MessageCardAction   _precisionCheckAction;    /**< Precision warning and optional type-reset callback. */
    mv::gui::MessageCardAction   _dataSummaryCheckAction;  /**< Matrix analysis summary and optional full-scan callback. */

    mv::gui::OptionsAction       _optionalObjectsAction;  /**< Optional HDF5 metadata objects selected for import. */
    QMap<QString, std::uint64_t> _metadataSizeEstimates;  /**< Estimated payload bytes for optional objects, keyed by HDF5 object name. */

    mv::gui::HorizontalGroupAction _buttonsAction;  /**< Bottom-row dialog buttons and leading stretch. */
    mv::gui::TriggerAction         _cancelAction;   /**< Closes the dialog without accepting its settings. */
    mv::gui::TriggerAction         _importAction;   /**< Accepts the dialog when the current preflight allows import. */

    mv::pointdata::storage::StorageBackendConfiguration _tiledConfiguration;  /**< Tiled-storage settings selected for this import. */

    H5ADSparseMatrixInfo _sparseMatrixInfo;  /**< Sparse-X layout snapshot used to expose only a compatible backend choice. */

    bool                    _sourceMayLosePrecision   = false;                          /**< Whether conversion from the source representation may lose precision. */
    mv::util::SeverityLevel _dataSummarySeverity      = mv::util::SeverityLevel::Info;  /**< Severity currently assigned to the data summary. */
    QString                 _dataSummaryDescription;                                    /**< Current matrix type and sparsity summary. */
    int                     _recommendedTypeIndex     = -1;                             /**< Recommended option index, or -1 before analysis completes. */
    bool                    _fullScanComplete         = false;                          /**< Whether an exhaustive matrix-value scan completed successfully. */
    bool                    _analysisRunning          = false;                          /**< Whether a matrix sampling or full-scan workflow is active. */
    bool                    _eagerMemoryLimitExceeded = false;                          /**< Whether the selected eager or sparse payload exceeds the configured RAM budget. */

    std::shared_ptr<HDF5_AD_Loader> _loader;       /**< Shared loader retained while estimating and checking this import. */
    std::uint64_t                   _rows    = 0;  /**< Row count of the H5AD X matrix. */
    std::uint64_t                   _columns = 0;  /**< Column count of the H5AD X matrix. */
};
