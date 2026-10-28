#pragma once

#include "HDF5_AD_Loader.h"

#include <PointData/StorageBackendConfiguratorAction.h>
#include <actions/DirectoryPickerAction.h>
#include <actions/HorizontalGroupAction.h>
#include <actions/OptionAction.h>
#include <actions/OptionsAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>
#include <actions/VerticalGroupAction.h>
#include <actions/WidgetAction.h>
#include <util/SeverityLevel.h>

#include <QDialog>
#include <QPointer>

#include <memory>

/**
 * @brief Displays one severity-coded observation and up to two corrective actions.
 *
 * The action keeps the observation state and updates every widget it has
 * created, so it can be used in action groups that create widgets on demand.
 */
class H5ADImportCheckAction : public mv::gui::WidgetAction
{
    Q_OBJECT

public:
    H5ADImportCheckAction(QObject* parent, const QString& category);

    /** Sets the displayed severity, description, and optional buttons in action-index order. */
    void setObservation(mv::util::SeverityLevel severity, const QString& description, const QString& actionText = {}, const QString& secondaryActionText = {});

signals:
    /** Emitted with index 0 or 1 when the corresponding visible corrective button is pressed. */
    void actionTriggered(int actionIndex);

protected:
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;

private:
    void updateWidget(QWidget* widget) const;

    mv::util::SeverityLevel _severity = mv::util::SeverityLevel::Info;
    QString _description;
    QStringList _actionTexts;
    QList<QPointer<QWidget>> _widgets;
};

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
    mv::gui::OptionAction _typeAction;
    mv::gui::TriggerAction _estimateAction;
};

/**
 * @brief Collects import settings and data checks for an opened H5AD file.
 *
 * The dialog shares the loader so it can estimate or fully scan matrix values
 * in a workflow. Accepting the dialog returns choices to the caller; the
 * loader schedules the actual import separately.
 */
class H5ADImportDialog : public QDialog
{
public:
    /** Creates the dialog with file metadata, restored choices, and a shared loader. */
    H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent = nullptr);

    /** Returns current import choices; callers should use them after acceptance. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

    /** Returns the staging directory selected for this import, including for eager staging. */
    QString stagingDirectory() const;

private:
    void updateImportChecks();
    void estimateTargetElementType();
    void analyzeTargetElementType(bool fullScan);
    void updateDataSummaryCard();

    mv::gui::VerticalGroupAction _settingsAction;
    mv::gui::StringAction _fileAction;
    mv::gui::StringAction _matrixAction;
    mv::gui::StringAction _datasetNameAction;
    TargetElementTypeAction _storageTypeAction;
    StorageBackendConfiguratorAction _backendSelector;
    mv::gui::HorizontalGroupAction _stagingActions;
    mv::gui::DirectoryPickerAction _stagingDirectoryAction;
    QString _defaultStagingDirectory;
    mv::gui::VerticalGroupAction _checksAction;
    H5ADImportCheckAction _memoryCheckAction;
    H5ADImportCheckAction _stagingCheckAction;
    H5ADImportCheckAction _precisionCheckAction;
    H5ADImportCheckAction _dataSummaryCheckAction;
    mv::gui::OptionsAction _optionalObjectsAction;
    QMap<QString, std::uint64_t> _metadataSizeEstimates;
    mv::gui::HorizontalGroupAction _buttonsAction;
    mv::gui::TriggerAction _cancelAction;
    mv::gui::TriggerAction _importAction;
    mv::pointdata::storage::StorageBackendConfiguration _tiledConfiguration;
    bool _sourceMayLosePrecision = false;
    mv::util::SeverityLevel _dataSummarySeverity = mv::util::SeverityLevel::Info;
    QString _dataSummaryDescription;
    int _recommendedTypeIndex = -1;
    bool _fullScanComplete = false;
    bool _analysisRunning = false;
    std::shared_ptr<HDF5_AD_Loader> _loader;
    std::uint64_t _rows = 0;
    std::uint64_t _columns = 0;
};
