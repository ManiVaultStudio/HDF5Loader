#include "H5ADImportDialog.h"

#include <PointData/PointData.h>
#include <PointData/storage/actions/TiledStorageBackendConfigurationAction.h>
#include <PointData/storage/widgets/StorageBackendConfigurationDialog.h>
#include <util/HardwareSpec.h>
#include <util/Miscellaneous.h>
#include <util/RamComponentSpec.h>
#include <util/StyledIcon.h>

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QSizePolicy>
#include <QStorageInfo>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <Application.h>
#include <actions/WidgetAction.h>
#include <workflow/WorkflowOptions.h>
#include <workflow/WorkflowPlan.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace
{

bool isPotentiallyLossyStorageType(const int storageType, const bool sourceMayLosePrecision)
{
    return sourceMayLosePrecision || storageType == -2 ||
        storageType == static_cast<int>(PointData::ElementTypeSpecifier::bfloat16) ||
        storageType == static_cast<int>(PointData::ElementTypeSpecifier::int8) ||
        storageType == static_cast<int>(PointData::ElementTypeSpecifier::uint8) ||
        storageType == static_cast<int>(PointData::ElementTypeSpecifier::int16) ||
        storageType == static_cast<int>(PointData::ElementTypeSpecifier::uint16);
}

}

TargetElementTypeAction::TargetElementTypeAction(QObject* parent, const QString& title) :
    mv::gui::HorizontalGroupAction(parent, title),
    _typeAction(this, "Type"),
    _estimateAction(this, "Estimate")
{
    setShowLabels(false);
    _typeAction.setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
    _estimateAction.setIconByName("wand-magic-sparkles");
    _estimateAction.setToolTip(tr("Estimate a compact target type from a bounded sample of the matrix"));
    addAction(&_typeAction);
    addAction(&_estimateAction, mv::gui::TriggerAction::Text);
    connect(&_estimateAction, &mv::gui::TriggerAction::triggered, this, &TargetElementTypeAction::estimateRequested);
}

mv::gui::OptionAction& TargetElementTypeAction::getTypeAction()
{
    return _typeAction;
}

const mv::gui::OptionAction& TargetElementTypeAction::getTypeAction() const
{
    return _typeAction;
}

void TargetElementTypeAction::setAnalysisRunning(const bool running, const bool estimating)
{
    _typeAction.setEnabled(!running);
    _estimateAction.setEnabled(!running);
    _estimateAction.setText(running && estimating ? tr("Estimating...") : tr("Estimate"));
}

void TargetElementTypeAction::setRecommendedTypeIndex(const int index)
{
    if (index >= 0 && index < static_cast<int>(_typeAction.getNumberOfOptions()))
        _typeAction.setCurrentIndex(index);
}

H5ADImportDialog::H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, const std::uint64_t rows, const std::uint64_t columns, const int storageTypeIndex, const QString& backendType, const H5ADSparseMatrixInfo& sparseMatrixInfo, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent) :
    QDialog(parent),
    _settingsAction(this, "Import settings"),
    _fileAction(&_settingsAction, "File", QFileInfo(fileName).fileName()),
    _matrixAction(&_settingsAction, "Matrix", rows > 0 && columns > 0 ? tr("%1 rows × %2 columns").arg(rows).arg(columns) : tr("Unknown")),
    _datasetNameAction(&_settingsAction, "Dataset name", QFileInfo(fileName).baseName()),
    _storageTypeAction(&_settingsAction, "Data type"),
    _backendSelector(&_settingsAction, sparseMatrixInfo.available ? QStringList { "Eager", "Tiled", "Sparse" } : QStringList { "Eager", "Tiled" }, "Tiled"),
    _stagingActions(&_settingsAction, "Temporary storage"),
    _stagingDirectoryAction(&_stagingActions, "Location", stagingDirectory),
    _defaultStagingDirectory(defaultStagingDirectory),
    _checksAction(&_settingsAction, "Import checks", true),
    _memoryCheckAction(&_checksAction),
    _stagingCheckAction(&_checksAction),
    _precisionCheckAction(&_checksAction),
    _dataSummaryCheckAction(&_checksAction),
    _optionalObjectsAction(&_settingsAction, "Optional metadata", optionalObjects),
    _metadataSizeEstimates(loader ? loader->getOptionalObjectSizeEstimates() : QMap<QString, std::uint64_t>{}),
    _buttonsAction(this, "Import controls"),
    _cancelAction(&_buttonsAction, "Cancel"),
    _importAction(&_buttonsAction, "Import"),
    _tiledConfiguration(tiledConfiguration),
    _sparseMatrixInfo(sparseMatrixInfo),
    _sourceMayLosePrecision(loader && loader->sourceMayLosePrecision()),
    _loader(std::move(loader)),
    _rows(rows),
    _columns(columns)
{
    setWindowTitle(tr("Import H5AD"));
    setWindowIcon(mv::util::StyledIcon("file-import"));
    resize(600, 600);

    _settingsAction.setShowLabels(true);
    _settingsAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _settingsAction.setLabelSizingType(mv::gui::GroupAction::LabelSizingType::Fixed);
    _settingsAction.setLabelWidthFixed(180);
    _fileAction.setDefaultWidgetFlags(mv::gui::StringAction::Label);
    _matrixAction.setDefaultWidgetFlags(mv::gui::StringAction::Label);
    _datasetNameAction.setDefaultWidgetFlags(mv::gui::StringAction::LineEdit);
    _storageTypeAction.getTypeAction().setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
    _optionalObjectsAction.setDefaultWidgetFlags(mv::gui::OptionsAction::Tags);
    _stagingDirectoryAction.setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    _checksAction.setShowLabels(false);
    _checksAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _buttonsAction.setShowLabels(false);
    _buttonsAction.setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);

    QStringList storageTypeNames { tr("Optimized (lossless)"), tr("Optimized (incl bfloat16)"), tr("Original") };
    const auto elementTypeNames = PointData::getElementTypeNames();
    for (const auto* name : elementTypeNames)
        storageTypeNames.append(QString::fromLatin1(name));
    _storageTypeAction.getTypeAction().initialize(storageTypeNames, storageTypeNames[qBound(0, storageTypeIndex, storageTypeNames.size() - 1)]);

    QStringList selectedObjects;
    for (const auto& name : optionalObjects)
        if (name.startsWith("obs"))
            selectedObjects.append(name);
    _optionalObjectsAction.setSelectedOptions(selectedObjects);
    QMap<QString, QString> metadataToolTips;
    for (const auto& name : optionalObjects)
    {
        if (_metadataSizeEstimates.contains(name))
            metadataToolTips.insert(name, tr("Estimated uncompressed HDF5 dataset payload: %1. Container overhead, variable-length values, and derived metadata may add memory.").arg(mv::util::getNoBytesHumanReadable(_metadataSizeEstimates.value(name))));
        else
            metadataToolTips.insert(name, tr("The dataset payload size could not be estimated."));
    }
    _optionalObjectsAction.setOptionToolTips(metadataToolTips);
    _optionalObjectsAction.setVisible(!optionalObjects.isEmpty());
    updateImportActionState();

    _settingsAction.addAction(&_fileAction);
    _settingsAction.addAction(&_matrixAction);
    _settingsAction.addAction(&_datasetNameAction);
    _settingsAction.addAction(&_storageTypeAction);
    _settingsAction.addAction(&_backendSelector);
    _settingsAction.addAction(&_stagingActions);
    _stagingActions.addAction(&_stagingDirectoryAction);
    _settingsAction.addAction(&_optionalObjectsAction, -1, [this](mv::gui::WidgetAction*, QWidget* widget) {
        auto* tableView = widget->findChild<QTableView*>(QStringLiteral("TableView"));
        if (!tableView)
            return;

        constexpr int maximumVisibleOptions = 8;
        const auto visibleRows = std::min<int>(_optionalObjectsAction.getNumberOfOptions(), maximumVisibleOptions);
        const auto rowHeight = tableView->verticalHeader()->defaultSectionSize();
        tableView->setFixedHeight(visibleRows * rowHeight + 2 * tableView->frameWidth());

        widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        widget->setMaximumHeight(widget->sizeHint().height());
    });
    _settingsAction.addAction(&_checksAction);
    _checksAction.addAction(&_memoryCheckAction);
    _checksAction.addAction(&_stagingCheckAction);
    _checksAction.addAction(&_precisionCheckAction);
    _checksAction.addAction(&_dataSummaryCheckAction);
    _buttonsAction.addStretch(1);
    _buttonsAction.addAction(&_cancelAction, mv::gui::TriggerAction::Text);
    _buttonsAction.addAction(&_importAction, mv::gui::TriggerAction::Text);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_settingsAction.createWidget(this));
    layout->addStretch(1);
    layout->addWidget(_buttonsAction.createWidget(this));

    _backendSelector.getConfigureAction().setIconByName("gear");
    connect(&_backendSelector, &StorageBackendConfiguratorAction::backendTypeChanged, this, [this](const QString& type) {
        _backendSelector.setConfigureEnabled(type == "Tiled");
        _stagingDirectoryAction.setEnabled(type == "Tiled");
        updateImportChecks();
    });
    const auto initialBackendType = sparseMatrixInfo.available || backendType != "Sparse" ? backendType : QStringLiteral("Tiled");
    _backendSelector.setBackendType(initialBackendType);
    _backendSelector.setConfigureEnabled(initialBackendType == "Tiled");
    connect(&_backendSelector, &StorageBackendConfiguratorAction::configureRequested, this, [this] {
        auto configurationAction = std::make_unique<mv::gui::TiledStorageBackendConfigurationAction>(nullptr, _rows, _columns, sizeof(float), _tiledConfiguration, true);
        auto* configurationActionPtr = configurationAction.get();
        StorageBackendConfigurationDialog dialog(std::move(configurationAction), StorageBackendConfigurationDialog::Purpose::Import, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        _tiledConfiguration = configurationActionPtr->getConfiguration();
        updateImportChecks();
    });
    connect(&_datasetNameAction, &mv::gui::StringAction::stringChanged, this, [this] {
        updateImportActionState();
    });
    connect(&_optionalObjectsAction, &mv::gui::OptionsAction::selectedOptionsChanged, this, [this] {
        updateImportChecks();
    });
    connect(&_storageTypeAction.getTypeAction(), &mv::gui::OptionAction::currentIndexChanged, this, [this] {
        updateImportChecks();
    });
    connect(&_storageTypeAction, &TargetElementTypeAction::estimateRequested, this, &H5ADImportDialog::estimateTargetElementType);
    connect(&_stagingDirectoryAction, &mv::gui::DirectoryPickerAction::directoryChanged, this, [this](const QString&) {
        updateImportChecks();
    });
    connect(&_importAction, &mv::gui::TriggerAction::triggered, this, &QDialog::accept);
    connect(&_cancelAction, &mv::gui::TriggerAction::triggered, this, &QDialog::reject);

    _stagingDirectoryAction.setEnabled(initialBackendType == "Tiled");
    _dataSummarySeverity = mv::util::SeverityLevel::Warning;
    _dataSummaryDescription = tr("Estimating target type and sparsity from a bounded sample...");
    updateDataSummaryCard();
    updateImportChecks();
    QTimer::singleShot(0, this, [this] { estimateTargetElementType(); });
}

H5ADImportOptions H5ADImportDialog::options()
{
    H5ADImportOptions result;
    result.datasetName = _datasetNameAction.getString().trimmed();
    result.storageType = _storageTypeAction.getTypeAction().getCurrentIndex() - 3;
    result.storageBackendType = mv::pointdata::storage::getStorageBackendType(backendType()).value_or(mv::pointdata::storage::StorageBackendType::Tiled);
    result.tiledStorageConfiguration = _tiledConfiguration;
    result.optionalObjects = _optionalObjectsAction.getSelectedOptions();
    result.stagingDirectory = stagingDirectory();
    return result;
}

int H5ADImportDialog::storageTypeIndex() const
{
    return _storageTypeAction.getTypeAction().getCurrentIndex();
}

void H5ADImportDialog::estimateTargetElementType()
{
    analyzeTargetElementType(false);
}

void H5ADImportDialog::analyzeTargetElementType(const bool fullScan)
{
    if (!_loader)
        return;

    _storageTypeAction.setAnalysisRunning(true, !fullScan);
    const auto analysisDescription = fullScan ? tr("Scanning every matrix value to determine a compact target type. This may take some time.") : tr("Analyzing a bounded sample of the matrix to estimate a compact target type.");
    updatePrecisionCheck(mv::util::SeverityLevel::Info, analysisDescription);
    _analysisRunning = true;
    updateImportActionState();
    if (!fullScan)
        _fullScanComplete = false;
    _dataSummarySeverity = fullScan ? mv::util::SeverityLevel::Info : mv::util::SeverityLevel::Warning;
    _dataSummaryDescription = fullScan ? tr("Scanning every matrix value. This may take some time.") : tr("Estimating target type and sparsity from a bounded sample...");
    updateDataSummaryCard();

    const QPointer<H5ADImportDialog> guardedDialog(this);
    const auto loader = _loader;
    const auto scanResult = std::make_shared<H5ADMatrixScanResult>();
    auto workflow = std::make_unique<mv::workflow::WorkflowPlan>(fullScan ? tr("Scan H5AD data") : tr("Estimate H5AD element type"));
    workflow->addSequentialStage(fullScan ? tr("Scan matrix values") : tr("Sample matrix values"), [loader, scanResult, fullScan](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& context) {
        if (fullScan)
            *scanResult = loader->determineTargetElementType([context](const double progress) { context->setProgress(progress); });
        else
            *scanResult = loader->estimateTargetElementType();
    }, mv::workflow::WorkflowPlan::JobThreadAffinity::CurrentWorkerThread, 1);

    mv::workflow::WorkflowOptions options;
    options.cancellation.enabled = true;
    options.reporting.progress = true;
    options.reporting.finishedNotification = false;
    auto future = mv::Application::getWorkflowPlanExecutor().execute(std::move(workflow), nullptr, options);
    future.onFinished(this, [guardedDialog, scanResult, fullScan](const mv::workflow::SharedWorkflowResult& result) {
        if (!guardedDialog)
            return;

        guardedDialog->_storageTypeAction.setAnalysisRunning(false);
        guardedDialog->_analysisRunning = false;
        guardedDialog->updateImportActionState();
        if (!result || result->getStatus() != mv::workflow::WorkflowResultBase::Status::Success || scanResult->recommendedTypeIndex < 0)
        {
            guardedDialog->updatePrecisionCheck(mv::util::SeverityLevel::Warning, guardedDialog->tr("The target element type could not be analyzed from this file."));
            guardedDialog->_dataSummarySeverity = mv::util::SeverityLevel::Warning;
            guardedDialog->_dataSummaryDescription = guardedDialog->tr("The data summary could not be determined.");
            guardedDialog->updateDataSummaryCard();
            return;
        }

        guardedDialog->_recommendedTypeIndex = scanResult->recommendedTypeIndex;
        guardedDialog->_fullScanComplete = fullScan;
        guardedDialog->_storageTypeAction.setRecommendedTypeIndex(scanResult->recommendedTypeIndex);
        guardedDialog->updateImportChecks();
        const auto selectedType = scanResult->recommendedTypeIndex - 3;
        if (!fullScan && scanResult->sparsityAvailable)
        {
            const auto zeroPercent = 100.0 * scanResult->zeroFraction;
            const auto typeName = guardedDialog->_storageTypeAction.getTypeAction().getCurrentText();
            const auto description = scanResult->sparseInput ?
                guardedDialog->tr("Estimated type: %1. About %2% zeros; sample: %3 stored values (%4 total entries).")
                    .arg(typeName, QString::number(zeroPercent, 'f', 2)).arg(scanResult->analyzedValueCount).arg(scanResult->storedEntryCount) :
                guardedDialog->tr("Estimated type: %1. About %2% zeros from a sample of %3 values.")
                    .arg(typeName, QString::number(zeroPercent, 'f', 2)).arg(scanResult->analyzedValueCount);
            guardedDialog->_dataSummarySeverity = mv::util::SeverityLevel::Warning;
            guardedDialog->_dataSummaryDescription = description;
            guardedDialog->updateDataSummaryCard();
        }
        else if (fullScan)
        {
            QString summary = guardedDialog->tr("Type: %1.").arg(guardedDialog->_storageTypeAction.getTypeAction().getCurrentText());
            if (scanResult->rangeAvailable)
                summary += guardedDialog->tr(" Finite range: %1 to %2.").arg(QString::number(scanResult->minimum, 'g', 8), QString::number(scanResult->maximum, 'g', 8));
            if (scanResult->sparsityAvailable)
                summary += guardedDialog->tr("\nZero positions: %1%.").arg(QString::number(100.0 * scanResult->zeroFraction, 'f', 2));
            if (scanResult->sparseInput)
                summary += guardedDialog->tr(" Stored entries: %1 of %2.").arg(scanResult->storedEntryCount).arg(scanResult->elementCount);
            guardedDialog->_dataSummarySeverity = mv::util::SeverityLevel::Info;
            guardedDialog->_dataSummaryDescription = summary;
            guardedDialog->updateDataSummaryCard();
        }
        else
        {
            guardedDialog->_dataSummarySeverity = mv::util::SeverityLevel::Warning;
            guardedDialog->_dataSummaryDescription = guardedDialog->tr("The bounded sample could not estimate sparsity.");
            guardedDialog->updateDataSummaryCard();
        }
        const auto precisionSeverity = isPotentiallyLossyStorageType(selectedType, guardedDialog->_sourceMayLosePrecision) ? mv::util::SeverityLevel::Warning : mv::util::SeverityLevel::Info;
        const auto precisionDescription = fullScan ?
            guardedDialog->tr("The recommendation was determined by scanning all matrix values. Selecting a narrower type may still discard precision or truncate values.") :
            guardedDialog->tr("The recommendation is based on a bounded sample. Values outside the sample may require a wider type.");
        guardedDialog->updatePrecisionCheck(precisionSeverity, precisionDescription);
    });
}

void H5ADImportDialog::updateDataSummaryCard()
{
    mv::gui::MessageCardAction::ButtonActions buttonActions;
    if (!_analysisRunning && !_fullScanComplete)
        buttonActions.append({ tr("Full scan"), [this] { analyzeTargetElementType(true); } });

    _dataSummaryCheckAction.setMessage(_dataSummarySeverity, tr("Data summary"), _dataSummaryDescription, buttonActions);
}

void H5ADImportDialog::updateMemoryCheck(const mv::util::SeverityLevel severity, const QString& description, const bool offerTiledStorage)
{
    mv::gui::MessageCardAction::ButtonActions buttonActions;
    if (offerTiledStorage)
        buttonActions.append({ tr("Use tiled storage"), [this] { _backendSelector.setBackendType("Tiled"); } });

    _memoryCheckAction.setMessage(severity, tr("Memory"), description, buttonActions);
}

void H5ADImportDialog::updateStagingCheck(const mv::util::SeverityLevel severity, const QString& description, const bool offerChooseDirectory, const bool offerResetDirectory)
{
    mv::gui::MessageCardAction::ButtonActions buttonActions;
    if (offerChooseDirectory)
    {
        buttonActions.append({ tr("Choose directory"), [this] {
            const auto pickerActions = _stagingDirectoryAction.getChildren<mv::gui::TriggerAction>();
            if (!pickerActions.isEmpty())
                pickerActions.constFirst()->trigger();
        } });
    }

    if (offerResetDirectory)
        buttonActions.append({ tr("Reset to default"), [this] { _stagingDirectoryAction.setDirectory(_defaultStagingDirectory); } });

    _stagingCheckAction.setMessage(severity, tr("Disk space"), description, buttonActions);
}

void H5ADImportDialog::updatePrecisionCheck(const mv::util::SeverityLevel severity, const QString& description)
{
    mv::gui::MessageCardAction::ButtonActions buttonActions;
    if (_recommendedTypeIndex >= 0 && _storageTypeAction.getTypeAction().getCurrentIndex() != _recommendedTypeIndex)
        buttonActions.append({ tr("Reset type"), [this] { _storageTypeAction.setRecommendedTypeIndex(_recommendedTypeIndex); } });

    _precisionCheckAction.setMessage(severity, tr("Precision"), description, buttonActions);
}

QString H5ADImportDialog::backendType()
{
    return _backendSelector.getBackendType();
}

QString H5ADImportDialog::stagingDirectory() const
{
    return _stagingDirectoryAction.getDirectory();
}

void H5ADImportDialog::updateImportActionState()
{
    _importAction.setEnabled(!_analysisRunning && !_eagerMemoryLimitExceeded && !_datasetNameAction.getString().trimmed().isEmpty());
}

void H5ADImportDialog::updateImportChecks()
{
    std::size_t elementSize = sizeof(float);
    switch (_storageTypeAction.getTypeAction().getCurrentIndex() - 3)
    {
        case 1: elementSize = 2; break; // bfloat16
        case 4: elementSize = 2; break; // int16
        case 5: elementSize = 2; break; // uint16
        case 6: elementSize = 1; break; // int8
        case 7: elementSize = 1; break; // uint8
        default: break; // Optimized, original, and 32-bit types use a conservative four-byte estimate.
    }

    const auto selectedStorageType = _storageTypeAction.getTypeAction().getCurrentIndex() - 3;
    const auto precisionIsLossy = isPotentiallyLossyStorageType(selectedStorageType, _sourceMayLosePrecision);
    const auto precisionSeverity = precisionIsLossy ? mv::util::SeverityLevel::Warning : mv::util::SeverityLevel::Info;
    const auto precisionDescription = precisionIsLossy ?
        tr("The selected type or source format may discard precision or truncate values. This can be irreversible; choose a wider type to preserve more information.") :
        tr("The selected element type does not introduce a known precision reduction for this source format.");
    updatePrecisionCheck(precisionSeverity, precisionDescription);

    bool hasMatrixEstimate = _rows > 0 && _columns > 0;
    std::uint64_t matrixBytes = 0;
    if (hasMatrixEstimate && _rows <= std::numeric_limits<std::uint64_t>::max() / _columns)
    {
        const auto elementCount = _rows * _columns;
        if (elementCount <= std::numeric_limits<std::uint64_t>::max() / elementSize)
            matrixBytes = elementCount * elementSize;
        else
            hasMatrixEstimate = false;
    }
    else
    {
        hasMatrixEstimate = false;
    }

    const auto selectedBackend = backendType();
    const auto tiled           = selectedBackend == "Tiled";
    const auto eager           = selectedBackend == "Eager";
    const auto sparse          = selectedBackend == "Sparse";
    if (!hasMatrixEstimate)
    {
        _eagerMemoryLimitExceeded = false;
        updateImportActionState();
        updateMemoryCheck(mv::util::SeverityLevel::Info, tr("The matrix size could not be estimated from the file dimensions."));
        updateStagingCheck(mv::util::SeverityLevel::Info, tr("The required staging space could not be estimated."), false, false);
        return;
    }

    constexpr std::uint64_t eagerRecommendationThreshold = std::uint64_t { 4 } * 1024 * 1024 * 1024;
    const auto& systemHardware = mv::util::HardwareSpec::getSystemHardwareSpec();
    const auto ramSpec = systemHardware.getHardwareComponentSpec<mv::util::RamComponentSpec>(QStringLiteral("RAM"));
    const auto installedRamBytes = ramSpec && ramSpec->isInitialized() ? ramSpec->getNumberOfBytes() : 0;
    const auto matrixSize = mv::util::getNoBytesHumanReadable(matrixBytes);
    std::uint64_t metadataBytes = 0;
    bool metadataEstimateComplete = true;
    for (const auto& name : _optionalObjectsAction.getSelectedOptions())
    {
        if (!_metadataSizeEstimates.contains(name) || metadataBytes > std::numeric_limits<std::uint64_t>::max() - _metadataSizeEstimates.value(name))
        {
            metadataEstimateComplete = false;
            continue;
        }

        metadataBytes += _metadataSizeEstimates.value(name);
    }

    std::uint64_t storageBytes = matrixBytes;
    bool storageEstimateAvailable = true;
    if (sparse)
    {
        const auto compressedCount = _sparseMatrixInfo.columnCompressed ? _columns : _rows;
        const auto pointerCount    = compressedCount == std::numeric_limits<std::uint64_t>::max() ? 0 : compressedCount + 1;
        const auto bytesPerEntry   = sizeof(std::uint64_t) + elementSize + sizeof(float);
        if (pointerCount == 0 || pointerCount > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint64_t) ||
            _sparseMatrixInfo.storedEntryCount > (std::numeric_limits<std::uint64_t>::max() - pointerCount * sizeof(std::uint64_t)) / bytesPerEntry)
            storageEstimateAvailable = false;
        else
            storageBytes = _sparseMatrixInfo.storedEntryCount * bytesPerEntry + pointerCount * sizeof(std::uint64_t);
    }

    const auto combinedEstimateAvailable = storageEstimateAvailable && metadataBytes <= std::numeric_limits<std::uint64_t>::max() - storageBytes;
    const auto combinedBytes             = combinedEstimateAvailable ? storageBytes + metadataBytes : storageBytes;
    const auto metadataSize              = mv::util::getNoBytesHumanReadable(metadataBytes);
    const auto storageSize               = mv::util::getNoBytesHumanReadable(storageBytes);
    const auto combinedSize              = mv::util::getNoBytesHumanReadable(combinedBytes);
    const auto estimateQualifier         = metadataEstimateComplete && combinedEstimateAvailable ? QString() : tr(" at least");
    const auto safeMemoryBudgetBytes = installedRamBytes - installedRamBytes / 4;
    _eagerMemoryLimitExceeded = (eager || sparse) && installedRamBytes > 0 && combinedBytes > safeMemoryBudgetBytes;
    updateImportActionState();
    const auto memorySummary = sparse ?
        tr("Sparse import working-memory estimate: %1. Selected metadata payload:%2 %3. Combined payload estimate:%2 %4. Values, indices, pointers, and a temporary conversion buffer are estimated; runtime allocations and metadata may add memory.")
            .arg(storageSize, estimateQualifier, metadataSize, combinedSize) :
        tr("Matrix (dense-equivalent): %1. Selected metadata payload:%2 %3. Combined payload estimate:%2 %4. This estimates uncompressed dataset bytes; runtime allocations, variable-length strings, tile cache, viewer, and derived data may add memory.")
            .arg(matrixSize, estimateQualifier, metadataSize, combinedSize);

    if (tiled)
    {
        updateMemoryCheck(mv::util::SeverityLevel::Info, memorySummary);
    }
    else if (_eagerMemoryLimitExceeded)
    {
        const auto memoryDescription = tr("%1 The selected backend's lower-bound payload estimate exceeds the conservative memory budget of %2 (75% of installed RAM). Switch to tiled storage to continue.")
            .arg(memorySummary, mv::util::getNoBytesHumanReadable(safeMemoryBudgetBytes));
        updateMemoryCheck(mv::util::SeverityLevel::Error, memoryDescription, true);
    }
    else if (eager && combinedBytes > eagerRecommendationThreshold)
    {
        const auto memoryDescription = tr("%1 Eager import may need additional working memory; tiled storage is recommended above 4 GiB.").arg(memorySummary);
        updateMemoryCheck(mv::util::SeverityLevel::Warning, memoryDescription, true);
    }
    else
    {
        updateMemoryCheck(mv::util::SeverityLevel::Info, memorySummary);
    }

    if (!tiled)
    {
        updateStagingCheck(mv::util::SeverityLevel::Info, tr("Staging space is not required for %1 storage.").arg(selectedBackend.toLower()), false, false);
        return;
    }

    const auto isDefaultStagingDirectory = QDir::cleanPath(stagingDirectory()) == QDir::cleanPath(_defaultStagingDirectory);
    const auto unavailableSpaceDescription = tr("Free space could not be checked for the selected staging directory.");
    QStorageInfo stagingStorage(_stagingDirectoryAction.getDirectory());
    stagingStorage.refresh();
    if (!stagingStorage.isValid() || !stagingStorage.isReady())
    {
        updateStagingCheck(mv::util::SeverityLevel::Warning, unavailableSpaceDescription, true, !isDefaultStagingDirectory);
        return;
    }

    const auto availableBytesSigned = stagingStorage.bytesAvailable();
    if (availableBytesSigned < 0)
    {
        updateStagingCheck(mv::util::SeverityLevel::Warning, unavailableSpaceDescription, true, !isDefaultStagingDirectory);
        return;
    }

    const auto availableBytes = static_cast<std::uint64_t>(availableBytesSigned);
    const auto availableSpace = mv::util::getNoBytesHumanReadable(availableBytes);
    if (availableBytes < matrixBytes)
    {
        const auto stagingDescription = tr("Potential disk-space issue: %1 is available, below the %2 dense-equivalent estimate. Sparse data and compression may reduce actual staging use.")
            .arg(availableSpace, matrixSize);
        updateStagingCheck(mv::util::SeverityLevel::Warning, stagingDescription, true, !isDefaultStagingDirectory);
    }
    else
    {
        const auto stagingDescription = tr("%1 is available; the dense-equivalent matrix estimate is %2. Sparse data and compression may reduce actual staging use.")
            .arg(availableSpace, matrixSize);
        updateStagingCheck(mv::util::SeverityLevel::Info, stagingDescription, false, !isDefaultStagingDirectory);
    }
}
