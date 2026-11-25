#include "H5ADImportDialog.h"

#include <PointData/PointData.h>
#include <PointData/storage/dense/DenseStorageBackendConversionAction.h>
#include <PointData/storage/interface/StorageBackendConfigurationDialog.h>
#include <PointData/storage/tiled/TiledStorageBackendConfigurationAction.h>
#include <PointData/storage/interface/PointsStorageBackendTypes.h>
#include <actions/GroupAction.h>
#include <actions/MessageCardAction.h>
#include <util/HardwareSpec.h>
#include <util/Miscellaneous.h>
#include <util/RamComponentSpec.h>
#include <util/StyledIcon.h>

#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSize>
#include <QSizePolicy>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <Application.h>
#include <CoreInterface.h>
#include <Project.h>
#include <ProjectCompressionAction.h>
#include <workflow/WorkflowOptions.h>
#include <workflow/WorkflowPlan.h>
#include <util/BlobCodec.h>
#include <util/CodecRegistry.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace
{

class ObsmTiledStorageBackendConfigurationAction final : public mv::gui::TiledStorageBackendConfigurationAction
{
public:
    ObsmTiledStorageBackendConfigurationAction(QObject* parent, const std::uint64_t rows, const std::uint64_t columns, const std::size_t elementSize, const mv::pointdata::storage::StorageBackendConfiguration& configuration, const bool initializeFromConfiguration, const std::uint32_t elementTypeIndex) :
        TiledStorageBackendConfigurationAction(parent, rows, columns, elementSize, configuration, initializeFromConfiguration, elementTypeIndex)
    {
        for (auto* action : getActions())
        {
            if (!action->isVisible() && dynamic_cast<mv::gui::MessageCardAction*>(action))
            {
                removeAction(action);
                break;
            }
        }
    }
};

std::int32_t getDefaultStagingWorkerCount()
{
    const auto logicalProcessorCount = std::max(1u, std::thread::hardware_concurrency());
    return static_cast<std::int32_t>(std::clamp(logicalProcessorCount / 2, 1u, 8u));
}

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

H5ADImportDialog::H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QStringList& optionalObjects, const QStringList& selectedOptionalObjects, const QString& benchmarkDirectory, const std::uint64_t rows, const std::uint64_t columns, const int storageTypeIndex, const QString& backendType, const H5ADSparseMatrixInfo& sparseMatrixInfo, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, const std::size_t bucketingThreadCount, const std::size_t stagingThreadCount, const std::size_t rowChunkTileFactor, const int zstdCompressionLevel, std::shared_ptr<HDF5_AD_Loader> loader, QWidget* parent) :
    QDialog(parent),
    _settingsAction(this, "Import settings"),
    _fileAction(&_settingsAction, "File", QFileInfo(fileName).fileName()),
    _matrixAction(&_settingsAction, "Matrix", rows > 0 && columns > 0 ? tr("%1 rows × %2 columns").arg(rows).arg(columns) : tr("Unknown")),
    _datasetNameAction(&_settingsAction, "Dataset name", QFileInfo(fileName).baseName()),
    _storageTypeAction(&_settingsAction, "Data type"),
    _backendSelector(&_settingsAction, sparseMatrixInfo.available ? QStringList { "Dense", "Tiled", "Sparse" } : QStringList { "Dense", "Tiled" }, "Tiled"),
    _zstdCompressionLevelAction(&_settingsAction, "Zstandard compression level", 1, 22, zstdCompressionLevel),
    _stagingDirectory(stagingDirectory),
    _bucketingThreadCount(bucketingThreadCount > 0 ? bucketingThreadCount : static_cast<std::size_t>(getDefaultStagingWorkerCount())),
    _stagingThreadCount(stagingThreadCount > 0 ? stagingThreadCount : static_cast<std::size_t>(getDefaultStagingWorkerCount())),
    _rowChunkTileFactor(rowChunkTileFactor > 0 ? rowChunkTileFactor : 3),
    _checksAction(&_settingsAction, "Pre-flight checks", true),
    _memoryCheckAction(&_checksAction),
    _precisionCheckAction(&_checksAction),
    _dataSummaryCheckAction(&_checksAction),
    _optionalObjectsAction(&_settingsAction, "Optional metadata", optionalObjects),
    _benchmarkOutputAction(this, "Import benchmark (optional)"),
    _benchmarkPathAction(&_benchmarkOutputAction, "Report file", tr("Not selected")),
    _chooseBenchmarkPathAction(&_benchmarkOutputAction, "Choose JSON file..."),
    _benchmarkDirectory(benchmarkDirectory),
    _clusterSetImportAction(nullptr, "obs cluster-set import"),
    _clusterSetSelectionAction(this),
    _clusterSetDialogChecksAction(nullptr, "Pre-flight checks", true),
    _clusterSetCountInfoAction(&_clusterSetDialogChecksAction),
    _availableClusterSets(loader ? loader->getObsClusterSets() : QMap<QString, H5ADClusterSetInfo>{}),
    _obsmMatrixImportAction(nullptr, "obsm matrix import"),
    _obsmMatrixSelectionAction(this),
    _obsmMatrices(loader ? loader->getObsmMatrices() : QStringList{}),
    _metadataSizeEstimates(loader ? loader->getOptionalObjectSizeEstimates() : QMap<QString, std::uint64_t>{}),
    _importInfoAction(&_settingsAction, "Import information"),
    _obsmMemoryInfoAction(&_importInfoAction),
    _obsmDialogMatrixAction(nullptr, "Matrix selection"),
    _obsmDialogChecksAction(nullptr, "Pre-flight checks"),
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

    _settingsAction.setShowLabels(true);
    _settingsAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _settingsAction.setLabelSizingType(mv::gui::GroupAction::LabelSizingType::Fixed);
    _settingsAction.setLabelWidthFixed(180);
    _memoryCheckAction.setDisplayMode(mv::gui::MessageCardAction::DisplayMode::Compact);
    _precisionCheckAction.setDisplayMode(mv::gui::MessageCardAction::DisplayMode::Compact);
    _dataSummaryCheckAction.setDisplayMode(mv::gui::MessageCardAction::DisplayMode::Compact);
    _obsmMemoryInfoAction.setDisplayMode(mv::gui::MessageCardAction::DisplayMode::Compact);
    _fileAction.setDefaultWidgetFlags(mv::gui::StringAction::Label);
    _matrixAction.setDefaultWidgetFlags(mv::gui::StringAction::Label);
    _datasetNameAction.setDefaultWidgetFlags(mv::gui::StringAction::LineEdit);
    _datasetNameAction.setToolTip(tr("Tiled imports append the selected tile, supertile, strip, cache, codec, and Zstandard level settings to this dataset name for comparison."));
    _storageTypeAction.getTypeAction().setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
    _zstdCompressionLevelAction.setDefaultWidgetFlags(mv::gui::IntegralAction::SpinBox);
    _zstdCompressionLevelAction.setToolTip(tr("Zstandard levels range from 1 (faster, larger output) to 22 (slower, smaller output). Level 3 is the codec default."));
    _optionalObjectsAction.setDefaultWidgetFlags(mv::gui::OptionsAction::Tags);
    _benchmarkPathAction.setDefaultWidgetFlags(mv::gui::StringAction::Label);
    _benchmarkOutputAction.setToolTip(tr("Optionally save import timings, staging layout, codec and Zstandard level, queue limits, and observed encoder concurrency to a JSON report."));
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
        if (selectedOptionalObjects.contains(name))
            selectedObjects.append(name);
    _optionalObjectsAction.setSelectedOptions(selectedObjects);
    _selectedClusterSets = _availableClusterSets.keys();
    _clusterSetCountInfoAction.setDisplayMode(mv::gui::MessageCardAction::DisplayMode::Compact);
    _clusterSetDialogChecksAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _clusterSetDialogChecksAction.setShowLabels(true);
    _clusterSetDialogChecksAction.setLabelSizingType(mv::gui::GroupAction::LabelSizingType::Fixed);
    _clusterSetDialogChecksAction.setLabelWidthFixed(140);
    connect(&_clusterSetSelectionAction, &ClusterSetSelectionAction::selectionChanged, this, &H5ADImportDialog::updateClusterSetPreflightCheck);
    QMap<QString, QString> metadataToolTips;
    for (const auto& name : optionalObjects)
    {
        if (_metadataSizeEstimates.contains(name))
            metadataToolTips.insert(name, tr("Estimated uncompressed HDF5 dataset payload: %1. Container overhead, variable-length values, and derived metadata may add memory.").arg(mv::util::getNoBytesHumanReadable(_metadataSizeEstimates.value(name))));
        else
            metadataToolTips.insert(name, tr("The dataset payload size could not be estimated."));
    }
    if (_optionalObjectsAction.getOptions().contains("obsm"))
        _optionalObjectsAction.setOptionConfigurationCallback("obsm", [this] { configureObsmMatrices(); });
    if (_optionalObjectsAction.getOptions().contains("obs") && !_availableClusterSets.isEmpty())
        _optionalObjectsAction.setOptionConfigurationCallback("obs", [this] { configureObsClusterSets(); });
    if (_optionalObjectsAction.getOptions().contains("obsm"))
        metadataToolTips.insert("obsm", tr("Choose which obsm matrices to import"));
    if (_optionalObjectsAction.getOptions().contains("obs") && !_availableClusterSets.isEmpty())
        metadataToolTips.insert("obs", tr("Choose which categorical obs fields to import as cluster sets"));
    _optionalObjectsAction.setOptionToolTips(metadataToolTips);
    _optionalObjectsAction.setVisible(!optionalObjects.isEmpty());
    _selectedObsmMatrices = _obsmMatrices;
    for (const auto& name : _obsmMatrices)
        _obsmStorageTypes.insert(name, mv::pointdata::storage::StorageBackendType::Dense);
    updateImportActionState();

    _settingsAction.addAction(&_fileAction);
    _settingsAction.addAction(&_matrixAction);
    _settingsAction.addAction(&_datasetNameAction);
    _settingsAction.addAction(&_storageTypeAction);
    _settingsAction.addAction(&_backendSelector);
    _settingsAction.addAction(&_zstdCompressionLevelAction);
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
    _benchmarkOutputAction.setShowLabels(false);
    _benchmarkOutputAction.addAction(&_benchmarkPathAction);
    _benchmarkOutputAction.addAction(&_chooseBenchmarkPathAction, mv::gui::TriggerAction::Text);
    _settingsAction.addAction(&_benchmarkOutputAction);
    _importInfoAction.setShowLabels(false);
    _importInfoAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _importInfoAction.addAction(&_obsmMemoryInfoAction);
    _checksAction.addAction(&_memoryCheckAction);
    _checksAction.addAction(&_precisionCheckAction);
    _checksAction.addAction(&_dataSummaryCheckAction);
    _importInfoAction.addAction(&_checksAction);
    _settingsAction.addAction(&_importInfoAction);
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
        updateCompressionLevelAvailability();
        updateImportChecks();
    });
    const auto initialBackendType = sparseMatrixInfo.available || backendType != "Sparse" ? backendType : QStringLiteral("Tiled");
    _backendSelector.setBackendType(initialBackendType);
    _backendSelector.setConfigureEnabled(initialBackendType == "Tiled");
    updateCompressionLevelAvailability();
    connect(&_backendSelector, &StorageBackendConfiguratorAction::configureRequested, this, &H5ADImportDialog::configureTiledStorage);
    connect(&_datasetNameAction, &mv::gui::StringAction::stringChanged, this, [this] {
        updateImportActionState();
    });
    connect(&_optionalObjectsAction, &mv::gui::OptionsAction::selectedOptionsChanged, this, [this] {
        updateImportChecks();
        updateObsmImportInfo(_selectedObsmMatrices);
    });
    connect(&_storageTypeAction.getTypeAction(), &mv::gui::OptionAction::currentIndexChanged, this, [this] {
        updateImportChecks();
    });
    connect(&_storageTypeAction, &TargetElementTypeAction::estimateRequested, this, &H5ADImportDialog::estimateTargetElementType);
    connect(&_importAction, &mv::gui::TriggerAction::triggered, this, &QDialog::accept);
    connect(&_cancelAction, &mv::gui::TriggerAction::triggered, this, &QDialog::reject);
    connect(&_chooseBenchmarkPathAction, &mv::gui::TriggerAction::triggered, this, [this, fileName] {
        const auto timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const auto suggestedName = QStringLiteral("%1-import-benchmark-%2.json").arg(QFileInfo(fileName).completeBaseName(), timestamp);
        const auto suggestedPath = QDir(_benchmarkDirectory.isEmpty() ? QDir::homePath() : _benchmarkDirectory).filePath(suggestedName);
        const auto selectedPath = QFileDialog::getSaveFileName(this, tr("Save H5AD import benchmark"), suggestedPath, tr("JSON files (*.json)"));
        if (selectedPath.isEmpty())
            return;

        _benchmarkFilePath = selectedPath;
        _benchmarkDirectory = QFileInfo(selectedPath).absolutePath();
        _benchmarkPathAction.setString(QFileInfo(selectedPath).fileName());
        _benchmarkPathAction.setToolTip(selectedPath);
    });

    _dataSummarySeverity = mv::util::SeverityLevel::Warning;
    _dataSummaryDescription = tr("Estimating target type and sparsity from a bounded sample...");
    updateDataSummaryCard();
    updateImportChecks();
    updateObsmImportInfo(_selectedObsmMatrices);
    QTimer::singleShot(0, this, [this] { estimateTargetElementType(); });
    resize(600, 450);
}

H5ADImportOptions H5ADImportDialog::options()
{
    H5ADImportOptions result;
    result.datasetName = _datasetNameAction.getString().trimmed();
    result.storageType = _storageTypeAction.getTypeAction().getCurrentIndex() - 3;
    result.metadataStorageType = result.storageType;
    result.storageBackendType = mv::pointdata::storage::getStorageBackendType(backendType()).value_or(mv::pointdata::storage::StorageBackendType::Tiled);
    result.tiledStorageConfiguration = _tiledConfiguration;
    result.projectOpenStagingPolicy = _projectOpenStagingPolicy;
    result.bucketingThreadCount = _bucketingThreadCount;
    result.stagingThreadCount = _stagingThreadCount;
    result.rowChunkTileFactor = _rowChunkTileFactor;
    result.zstdCompressionLevel = _zstdCompressionLevelAction.getValue();
    result.importBenchmarkFilePath = _benchmarkFilePath;
    result.optionalObjects = _optionalObjectsAction.getSelectedOptions();
    result.availableClusterSets = _availableClusterSets;
    result.selectedClusterSets = _selectedClusterSets;
    result.filterClusterSets = _clusterSetSelectionConfigured && _optionalObjectsAction.isOptionSelected("obs");
    result.optionalObjects.removeAll("obsm");
    if (_optionalObjectsAction.isOptionSelected("obsm"))
    {
        result.optionalObjects.append("obsm");
        result.optionalMatrixObjects = _selectedObsmMatrices;
        for (const auto& name : _selectedObsmMatrices)
        {
            if (_obsmElementTypes.contains(name))
                result.optionalMatrixElementTypes.insert(name, _obsmElementTypes.value(name));

            if (_obsmStorageTypes.value(name, mv::pointdata::storage::StorageBackendType::Dense) == mv::pointdata::storage::StorageBackendType::Tiled)
            {
                result.optionalMatrixStorageTypes.insert(name, mv::pointdata::storage::StorageBackendType::Tiled);
                if (_obsmTiledConfigurations.contains(name))
                    result.optionalMatrixTiledConfigurations.insert(name, _obsmTiledConfigurations.value(name));
            }
        }
    }
    result.stagingDirectory = _stagingDirectory;

    if (result.storageBackendType == mv::pointdata::storage::StorageBackendType::Tiled)
    {
        const auto& configuration = result.tiledStorageConfiguration;
        QStringList settings {
            QStringLiteral("tile %1x%2").arg(configuration.rowTileSize).arg(configuration.columnTileSize),
            QStringLiteral("cache %1 tiles/%2 MiB").arg(configuration.cacheCapacity).arg(configuration.cacheCapacityBytes / (1024ull * 1024ull))
        };

        if (configuration.packedTiles)
            settings.append(QStringLiteral("super %1x%2").arg(configuration.superTileRows).arg(configuration.superTileColumns));

        if (configuration.experimentalStripStorage)
            settings.append(QStringLiteral("strips %1x%2").arg(configuration.rowStripHeight).arg(configuration.columnStripWidth));

        auto codecName = configuration.tileCodecName;
        if (codecName.isEmpty())
        {
            if (auto* project = mv::projects().getCurrentProject())
            {
                const auto codecType = mv::util::codecRegistry().typeFromDisplayName(project->getCompressionAction().getCodecTypeAction().getCurrentText());
                codecName = mv::util::BlobCodec::typeToString(codecType);
            }
            else
                codecName = QStringLiteral("default");
        }
        if (codecName == QStringLiteral("zstd"))
            settings.append(QStringLiteral("zstd level %1").arg(_zstdCompressionLevelAction.getValue()));
        else
            settings.append(codecName);
        result.datasetName += QStringLiteral(" [%1]").arg(settings.join(QStringLiteral(", ")));
    }

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

void H5ADImportDialog::updateObsmImportInfo(const QStringList& selectedMatrices)
{
    std::uint64_t totalBytes = 0;
    bool sizeAvailable = true;
    for (const auto& name : selectedMatrices)
    {
        if (!_optionalObjectsAction.isOptionSelected("obsm"))
            continue;

        const auto shape = _loader ? _loader->getObsmMatrixShape(name) : std::pair<std::uint64_t, std::uint64_t>{ 0, 0 };
        if (shape.second != 0 && shape.first > std::numeric_limits<std::uint64_t>::max() / shape.second)
        {
            sizeAvailable = false;
            break;
        }

        const auto elementCount = shape.first * shape.second;
        constexpr std::uint64_t estimatedElementSize = sizeof(float);
        if (elementCount > std::numeric_limits<std::uint64_t>::max() / estimatedElementSize || totalBytes > std::numeric_limits<std::uint64_t>::max() - elementCount * estimatedElementSize)
        {
            sizeAvailable = false;
            break;
        }
        totalBytes += elementCount * estimatedElementSize;
    }

    const auto description = !_optionalObjectsAction.isOptionSelected("obsm") || selectedMatrices.isEmpty() ?
        tr("No obsm matrices selected for import.") :
        tr("Selected obsm matrices have an estimated uncompressed size of %1. Dense storage may use additional working memory during import.").arg(sizeAvailable ? mv::util::getNoBytesHumanReadable(totalBytes) : tr("at least the maximum representable size"));
    _obsmMemoryInfoAction.setMessage(sizeAvailable ? mv::util::SeverityLevel::Info : mv::util::SeverityLevel::Warning, tr("obsm memory estimate"), description);
}

void H5ADImportDialog::updateMemoryCheck(const mv::util::SeverityLevel severity, const QString& description, const bool offerTiledStorage)
{
    mv::gui::MessageCardAction::ButtonActions buttonActions;
    if (offerTiledStorage)
        buttonActions.append({ tr("Use tiled storage"), [this] { _backendSelector.setBackendType("Tiled"); } });

    _memoryCheckAction.setMessage(severity, tr("Memory"), description, buttonActions);
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

void H5ADImportDialog::configureObsmMatrices()
{
    if (!_loader || _obsmMatrices.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Configure obsm matrices"));
    dialog.resize(700, 380);
    dialog.setMinimumSize(700, 380);
    auto* layout = new QVBoxLayout(&dialog);
    _obsmDialogMatrixAction.removeAllActions();
    _obsmDialogChecksAction.removeAllActions();
    _obsmMatrixImportAction.removeAllActions();
    _obsmDialogMatrixAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _obsmDialogChecksAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _obsmDialogMatrixAction.setShowLabels(false);
    _obsmDialogMatrixAction.setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup, true);
    _obsmDialogChecksAction.setShowLabels(false);
    _obsmDialogChecksAction.setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup, true);
    _obsmMatrixImportAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _obsmMatrixImportAction.setShowLabels(true);
    _obsmMatrixImportAction.setLabelSizingType(mv::gui::GroupAction::LabelSizingType::Fixed);
    _obsmMatrixImportAction.setLabelWidthFixed(120);
    _obsmDialogChecksAction.setExpanded(false);
    _obsmMatrixImportAction.setText(tr("obsm matrix import"));
    _obsmDialogMatrixAction.setText(tr("Matrix selection"));
    _obsmDialogChecksAction.setText(tr("Pre-flight checks"));
    _obsmDialogMatrixAction.addAction(&_obsmMatrixSelectionAction);
    _obsmDialogChecksAction.addAction(&_obsmMemoryInfoAction);
    _obsmMatrixImportAction.addAction(&_obsmDialogMatrixAction);
    _obsmMatrixImportAction.addAction(&_obsmDialogChecksAction);
    auto* preflightStretch = _obsmMatrixImportAction.addStretch(1);
    preflightStretch->setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    _obsmMatrixSelectionAction.setMatrices(_obsmMatrices, _selectedObsmMatrices, _obsmStorageTypes, _loader.get());
    connect(&_obsmMatrixSelectionAction, &ObsmMatrixSelectionAction::storageConfigurationRequested, this, &H5ADImportDialog::configureObsmMatrixStorage);
    connect(&_obsmMatrixSelectionAction, &ObsmMatrixSelectionAction::selectionChanged, &dialog, [this] {
        updateObsmImportInfo(_obsmMatrixSelectionAction.selectedMatrices());
    });
    auto* rootWidget = _obsmMatrixImportAction.createWidget(&dialog);
    layout->addWidget(rootWidget, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const auto result = dialog.exec();
    if (result != QDialog::Accepted)
        return;

    _selectedObsmMatrices.clear();
    _selectedObsmMatrices = _obsmMatrixSelectionAction.selectedMatrices();
    _obsmStorageTypes = _obsmMatrixSelectionAction.storageTypes();
    updateObsmImportInfo(_selectedObsmMatrices);
}

void H5ADImportDialog::configureObsClusterSets()
{
    if (_availableClusterSets.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Configure obs cluster sets"));
    dialog.resize(620, 470);
    dialog.setMinimumSize(620, 470);

    auto* layout = new QVBoxLayout(&dialog);
    _clusterSetImportAction.removeAllActions();
    _clusterSetImportAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _clusterSetImportAction.setShowLabels(true);
    _clusterSetImportAction.setLabelSizingType(mv::gui::GroupAction::LabelSizingType::Fixed);
    _clusterSetImportAction.setLabelWidthFixed(140);
    _clusterSetImportAction.setText(tr("obs cluster-set import"));
    _clusterSetSelectionAction.setClusterSets(_availableClusterSets, _selectedClusterSets);
    _clusterSetDialogChecksAction.removeAllActions();
    _clusterSetDialogChecksAction.addAction(&_clusterSetCountInfoAction);
    _clusterSetImportAction.addAction(&_clusterSetSelectionAction);
    _clusterSetImportAction.addAction(&_clusterSetDialogChecksAction);
    auto* preflightStretch = _clusterSetImportAction.addStretch(1);
    preflightStretch->setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    updateClusterSetPreflightCheck();
    layout->addWidget(_clusterSetImportAction.createWidget(&dialog), 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    _selectedClusterSets = _clusterSetSelectionAction.selectedClusterSets();
    _clusterSetSelectionConfigured = true;
}

void H5ADImportDialog::updateClusterSetPreflightCheck()
{
    const auto selectedClusterSets = _clusterSetSelectionAction.selectedClusterSets();
    std::uint64_t encodedCategoryCount = 0;
    std::size_t unavailableCount = 0;

    for (const auto& name : selectedClusterSets)
    {
        const auto info = _availableClusterSets.value(name);
        if (info.clusterCountAvailable)
            encodedCategoryCount += info.clusterCount;
        else
            ++unavailableCount;
    }

    QString description;
    if (selectedClusterSets.isEmpty())
        description = tr("No cluster sets are selected.");
    else if (unavailableCount == 0)
        description = tr("Selected cluster sets contain %1 encoded categories across %2 sets. Unused categories may not produce clusters.").arg(encodedCategoryCount).arg(selectedClusterSets.size());
    else if (encodedCategoryCount == 0)
        description = tr("The cluster count requires scanning values in %1 selected set(s).").arg(unavailableCount);
    else
        description = tr("Selected sets contain %1 encoded categories; the count for %2 set(s) requires scanning values. Unused categories may not produce clusters.").arg(encodedCategoryCount).arg(unavailableCount);

    _clusterSetCountInfoAction.setMessage(mv::util::SeverityLevel::Info, tr("Cluster count"), description);
}

void H5ADImportDialog::configureObsmMatrixStorage(const QString& matrixName, const std::uint64_t rows, const std::uint64_t columns, const mv::pointdata::storage::StorageBackendType storageType)
{
    const auto selectedTypeIndex = _storageTypeAction.getTypeAction().getCurrentIndex() - 3;
    const auto fallbackTypeIndex = selectedTypeIndex >= 0 ? selectedTypeIndex : (_recommendedTypeIndex >= 0 ? _recommendedTypeIndex : 0);
    const auto elementTypeIndex = static_cast<std::uint32_t>(_obsmElementTypes.value(matrixName, fallbackTypeIndex));

    if (storageType == mv::pointdata::storage::StorageBackendType::Dense)
    {
        auto configurationAction = std::make_unique<mv::gui::DenseStorageBackendConversionAction>(nullptr, rows, columns, elementTypeIndex);
        auto* configurationActionPtr = configurationAction.get();
        ::StorageBackendConfigurationDialog configurationDialog(std::move(configurationAction), ::StorageBackendConfigurationDialog::Purpose::Import, this);

        if (configurationDialog.exec() == QDialog::Accepted)
            if (const auto configuredTypeIndex = configurationActionPtr->getTargetElementTypeIndex())
                _obsmElementTypes.insert(matrixName, static_cast<int>(*configuredTypeIndex));

        return;
    }

    const auto configuration = _obsmTiledConfigurations.value(matrixName);
    const auto elementSize = PointData::getElementTypeSize(elementTypeIndex);
    auto configurationAction = std::make_unique<ObsmTiledStorageBackendConfigurationAction>(this, rows, columns, elementSize, configuration, _obsmTiledConfigurations.contains(matrixName), elementTypeIndex);
    auto* configurationActionPtr = configurationAction.get();
    ::StorageBackendConfigurationDialog configurationDialog(std::move(configurationAction), ::StorageBackendConfigurationDialog::Purpose::Import, this);

    if (configurationDialog.exec() == QDialog::Accepted)
    {
        _obsmTiledConfigurations.insert(matrixName, configurationActionPtr->getConfiguration());
        if (const auto configuredTypeIndex = configurationActionPtr->getTargetElementTypeIndex())
            _obsmElementTypes.insert(matrixName, static_cast<int>(*configuredTypeIndex));
    }
}

void H5ADImportDialog::configureTiledStorage()
{
    std::size_t elementSize = sizeof(float);
    switch (_storageTypeAction.getTypeAction().getCurrentIndex() - 3)
    {
        case 1: elementSize = 2; break;
        case 4: elementSize = 2; break;
        case 5: elementSize = 2; break;
        case 6: elementSize = 1; break;
        case 7: elementSize = 1; break;
        default: break;
    }

    auto configurationAction = std::make_unique<mv::gui::TiledStorageBackendConfigurationAction>(nullptr, _rows, _columns, elementSize, _tiledConfiguration, true, std::nullopt, true, _bucketingThreadCount, _stagingThreadCount, _rowChunkTileFactor, true, _stagingDirectory, _projectOpenStagingPolicy);
    auto* configurationActionPtr = configurationAction.get();
    StorageBackendConfigurationDialog dialog(std::move(configurationAction), StorageBackendConfigurationDialog::Purpose::Import, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    _tiledConfiguration = configurationActionPtr->getConfiguration();
    _bucketingThreadCount = configurationActionPtr->getBucketingThreadCount();
    _stagingThreadCount = configurationActionPtr->getStagingThreadCount();
    _rowChunkTileFactor = configurationActionPtr->getRowChunkTileFactor();
    _projectOpenStagingPolicy = configurationActionPtr->getProjectResourceStagingPolicy();
    updateCompressionLevelAvailability();
    updateImportChecks();
}

void H5ADImportDialog::updateCompressionLevelAvailability()
{
    auto codecName = _tiledConfiguration.tileCodecName;
    if (codecName.isEmpty())
    {
        if (auto* project = mv::projects().getCurrentProject())
        {
            const auto codecType = mv::util::codecRegistry().typeFromDisplayName(project->getCompressionAction().getCodecTypeAction().getCurrentText());
            codecName = mv::util::BlobCodec::typeToString(codecType);
        }
    }

    _zstdCompressionLevelAction.setEnabled(backendType() == "Tiled" && codecName == QStringLiteral("zstd"));
}

void H5ADImportDialog::updateImportActionState()
{
    _importAction.setEnabled(!_analysisRunning && !_denseMemoryLimitExceeded && !_datasetNameAction.getString().trimmed().isEmpty());
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
    const auto dense           = selectedBackend == "Dense";
    const auto sparse          = selectedBackend == "Sparse";
    if (!hasMatrixEstimate)
    {
        _denseMemoryLimitExceeded = false;
        updateImportActionState();
        updateMemoryCheck(mv::util::SeverityLevel::Info, tr("The matrix size could not be estimated from the file dimensions."));
        return;
    }

    constexpr std::uint64_t denseRecommendationThreshold = std::uint64_t { 4 } * 1024 * 1024 * 1024;
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
    _denseMemoryLimitExceeded = (dense || sparse) && installedRamBytes > 0 && combinedBytes > safeMemoryBudgetBytes;
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
    else if (_denseMemoryLimitExceeded)
    {
        const auto memoryDescription = tr("%1 The selected backend's lower-bound payload estimate exceeds the conservative memory budget of %2 (75% of installed RAM). Switch to tiled storage to continue.")
            .arg(memorySummary, mv::util::getNoBytesHumanReadable(safeMemoryBudgetBytes));
        updateMemoryCheck(mv::util::SeverityLevel::Error, memoryDescription, true);
    }
    else if (dense && combinedBytes > denseRecommendationThreshold)
    {
        const auto memoryDescription = tr("%1 Dense import may need additional working memory; tiled storage is recommended above 4 GiB.").arg(memorySummary);
        updateMemoryCheck(mv::util::SeverityLevel::Warning, memoryDescription, true);
    }
    else
    {
        updateMemoryCheck(mv::util::SeverityLevel::Info, memorySummary);
    }

}
