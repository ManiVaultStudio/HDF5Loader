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

/** Displays one severity-coded import observation and an optional corrective action. */
class H5ADImportCheckAction : public mv::gui::WidgetAction
{
    Q_OBJECT

public:
    H5ADImportCheckAction(QObject* parent, const QString& category);

    void setObservation(mv::util::SeverityLevel severity, const QString& description, const QString& actionText = {}, const QString& secondaryActionText = {});

signals:
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

/** Combines target element type selection with a data-driven recommendation request. */
class TargetElementTypeAction : public mv::gui::HorizontalGroupAction
{
    Q_OBJECT

public:
    TargetElementTypeAction(QObject* parent, const QString& title);

    mv::gui::OptionAction& getTypeAction();
    const mv::gui::OptionAction& getTypeAction() const;
    void setAnalysisRunning(bool running, bool estimating = false);
    void setRecommendedTypeIndex(int index);

signals:
    void estimateRequested();

private:
    mv::gui::OptionAction _typeAction;
    mv::gui::TriggerAction _estimateAction;
};

/** Collects H5AD import choices after a file has been selected and opened. */
class H5ADImportDialog : public QDialog
{
public:
    H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent = nullptr);

    /** Returns the selected import options after the dialog is accepted. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

    /** Returns the staging directory selected for this import. */
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
