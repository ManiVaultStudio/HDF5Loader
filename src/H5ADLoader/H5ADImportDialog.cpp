#include "H5ADImportDialog.h"

#include <PointData/PointData.h>
#include <PointData/storage/actions/TiledStorageBackendConfigurationAction.h>
#include <PointData/storage/widgets/StorageBackendConfigurationDialog.h>
#include <util/HardwareSpec.h>
#include <util/Miscellaneous.h>
#include <util/RamComponentSpec.h>
#include <util/StyledIcon.h>

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStorageInfo>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <memory>

namespace
{

QColor getSeverityColor(const mv::util::SeverityLevel severity)
{
    switch (severity)
    {
        case mv::util::SeverityLevel::Warning: return QColor(196, 150, 20);
        case mv::util::SeverityLevel::Error: return QColor(196, 32, 32);
        case mv::util::SeverityLevel::Fatal: return QColor(150, 0, 0);
        default: return QColor(80, 130, 190);
    }
}

}

H5ADImportCheckAction::H5ADImportCheckAction(QObject* parent, const QString& category) :
    WidgetAction(parent, category)
{
    setConfigurationFlag(ConfigurationFlag::NoLabelInGroup);
}

void H5ADImportCheckAction::setObservation(const mv::util::SeverityLevel severity, const QString& description, const QString& actionText, const QString& secondaryActionText)
{
    _severity = severity;
    _description = description;
    _actionTexts = { actionText, secondaryActionText };

    for (const auto& widget : _widgets)
        if (widget)
            updateWidget(widget);
}

QWidget* H5ADImportCheckAction::getWidget(QWidget* parent, const std::int32_t&)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("ImportCheckRow"));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    auto* icon = new QLabel(row);
    icon->setObjectName(QStringLiteral("SeverityIcon"));
    icon->setFixedSize(20, 20);
    icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    layout->addWidget(icon);

    auto* textLayoutWidget = new QWidget(row);
    auto* textLayout = new QVBoxLayout(textLayoutWidget);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);

    auto* headingLayoutWidget = new QWidget(textLayoutWidget);
    auto* headingLayout = new QHBoxLayout(headingLayoutWidget);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(6);

    auto* severityLabel = new QLabel(headingLayoutWidget);
    severityLabel->setObjectName(QStringLiteral("SeverityLabel"));
    headingLayout->addWidget(severityLabel);

    auto* categoryLabel = new QLabel(text(), headingLayoutWidget);
    categoryLabel->setObjectName(QStringLiteral("CategoryLabel"));
    headingLayout->addWidget(categoryLabel);
    headingLayout->addStretch(1);
    textLayout->addWidget(headingLayoutWidget);

    auto* descriptionLabel = new QLabel(textLayoutWidget);
    descriptionLabel->setObjectName(QStringLiteral("DescriptionLabel"));
    descriptionLabel->setWordWrap(true);
    textLayout->addWidget(descriptionLabel);
    layout->addWidget(textLayoutWidget, 1);

    auto* actionButtonsWidget = new QWidget(row);
    actionButtonsWidget->setObjectName(QStringLiteral("ActionButtons"));
    auto* actionButtonsLayout = new QVBoxLayout(actionButtonsWidget);
    actionButtonsLayout->setContentsMargins(0, 0, 0, 0);
    actionButtonsLayout->setSpacing(4);
    for (int actionIndex = 0; actionIndex < 2; ++actionIndex)
    {
        auto* actionButton = new QPushButton(actionButtonsWidget);
        actionButton->setObjectName(QStringLiteral("ActionButton%1").arg(actionIndex));
        connect(actionButton, &QPushButton::clicked, this, [this, actionIndex] { emit actionTriggered(actionIndex); });
        actionButtonsLayout->addWidget(actionButton);
    }
    layout->addWidget(actionButtonsWidget, 0, Qt::AlignVCenter);

    _widgets.append(row);
    updateWidget(row);
    return row;
}

void H5ADImportCheckAction::updateWidget(QWidget* widget) const
{
    const auto color = getSeverityColor(_severity);
    const auto severityName = mv::util::getSeverityLevelName(_severity);
    auto* icon = widget->findChild<QLabel*>(QStringLiteral("SeverityIcon"));
    auto* severityLabel = widget->findChild<QLabel*>(QStringLiteral("SeverityLabel"));
    auto* descriptionLabel = widget->findChild<QLabel*>(QStringLiteral("DescriptionLabel"));

    icon->setPixmap(mv::util::getSeverityLevelIcon(_severity).pixmap(18, 18));
    icon->setAccessibleName(severityName);
    severityLabel->setText(severityName);
    severityLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(color.name()));
    descriptionLabel->setText(_description);
    for (int actionIndex = 0; actionIndex < 2; ++actionIndex)
    {
        auto* actionButton = widget->findChild<QPushButton*>(QStringLiteral("ActionButton%1").arg(actionIndex));
        const auto actionText = _actionTexts.value(actionIndex);
        actionButton->setText(actionText);
        actionButton->setVisible(!actionText.isEmpty());
    }

    const auto tint = QStringLiteral("rgba(%1, %2, %3, 24)").arg(color.red()).arg(color.green()).arg(color.blue());
    widget->setStyleSheet(QStringLiteral("QFrame#ImportCheckRow { background-color: %1; border-left: 3px solid %2; border-radius: 3px; }")
        .arg(tint, color.name()));
    widget->setAccessibleDescription(QStringLiteral("%1: %2").arg(severityName, _description));
}

H5ADImportDialog::H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, const std::uint64_t rows, const std::uint64_t columns, const int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, QWidget* parent) :
    QDialog(parent),
    _settingsAction(this, "Import settings"),
    _fileAction(&_settingsAction, "File", QFileInfo(fileName).fileName()),
    _matrixAction(&_settingsAction, "Matrix", rows > 0 && columns > 0 ? tr("%1 rows × %2 columns").arg(rows).arg(columns) : tr("Unknown")),
    _datasetNameAction(&_settingsAction, "Dataset name", QFileInfo(fileName).baseName()),
    _storageTypeAction(&_settingsAction, "Data type"),
    _backendSelector(&_settingsAction, { "Eager", "Tiled" }, "Tiled"),
    _stagingActions(&_settingsAction, "Temporary storage"),
    _stagingDirectoryAction(&_stagingActions, "Location", stagingDirectory),
    _defaultStagingDirectory(defaultStagingDirectory),
    _checksAction(&_settingsAction, "Import checks", true),
    _memoryCheckAction(&_checksAction, "Memory"),
    _stagingCheckAction(&_checksAction, "Disk space"),
    _optionalObjectsAction(&_settingsAction, "Optional metadata", optionalObjects),
    _buttonsAction(this, "Import controls"),
    _cancelAction(&_buttonsAction, "Cancel"),
    _importAction(&_buttonsAction, "Import"),
    _tiledConfiguration(tiledConfiguration),
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
    _storageTypeAction.setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
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
    _storageTypeAction.initialize(storageTypeNames, storageTypeNames[qBound(0, storageTypeIndex, storageTypeNames.size() - 1)]);

    QStringList selectedObjects;
    for (const auto& name : optionalObjects)
        if (name.startsWith("obs"))
            selectedObjects.append(name);
    _optionalObjectsAction.setSelectedOptions(selectedObjects);
    _optionalObjectsAction.setVisible(!optionalObjects.isEmpty());
    _importAction.setEnabled(!_datasetNameAction.getString().trimmed().isEmpty());

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
    _buttonsAction.addStretch(1);
    _buttonsAction.addAction(&_cancelAction, mv::gui::TriggerAction::Text);
    _buttonsAction.addAction(&_importAction, mv::gui::TriggerAction::Text);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_settingsAction.createWidget(this));
    layout->addStretch(1);
    layout->addWidget(_buttonsAction.createWidget(this));

    _backendSelector.getBackendTypeAction().setCurrentText(backendType);
    _backendSelector.getConfigureAction().setIconByName("gear");
    _backendSelector.setConfigureEnabled(backendType == "Tiled");
    connect(&_backendSelector, &StorageBackendConfiguratorAction::backendTypeChanged, this, [this](const QString& type) {
        _backendSelector.setConfigureEnabled(type == "Tiled");
        _stagingDirectoryAction.setEnabled(type == "Tiled");
        updateImportChecks();
    });
    connect(&_backendSelector, &StorageBackendConfiguratorAction::configureRequested, this, [this] {
        auto configurationAction = std::make_unique<mv::gui::TiledStorageBackendConfigurationAction>(nullptr, _rows, _columns, sizeof(float), _tiledConfiguration, true);
        auto* configurationActionPtr = configurationAction.get();
        StorageBackendConfigurationDialog dialog(std::move(configurationAction), StorageBackendConfigurationDialog::Purpose::Import, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        _tiledConfiguration = configurationActionPtr->getConfiguration();
        updateImportChecks();
    });
    connect(&_datasetNameAction, &mv::gui::StringAction::stringChanged, this, [this](const QString& name) {
        _importAction.setEnabled(!name.trimmed().isEmpty());
    });
    connect(&_storageTypeAction, &mv::gui::OptionAction::currentIndexChanged, this, [this] { updateImportChecks(); });
    connect(&_stagingDirectoryAction, &mv::gui::DirectoryPickerAction::directoryChanged, this, [this](const QString&) {
        updateImportChecks();
    });
    connect(&_memoryCheckAction, &H5ADImportCheckAction::actionTriggered, this, [this](int actionIndex) {
        if (actionIndex == 0)
            _backendSelector.getBackendTypeAction().setCurrentText("Tiled");
    });
    connect(&_stagingCheckAction, &H5ADImportCheckAction::actionTriggered, this, [this](int actionIndex) {
        if (actionIndex == 0)
        {
            const auto pickerActions = _stagingDirectoryAction.getChildren<mv::gui::TriggerAction>();
            if (!pickerActions.isEmpty())
                pickerActions.constFirst()->trigger();
        }
        else if (actionIndex == 1)
        {
            _stagingDirectoryAction.setDirectory(_defaultStagingDirectory);
        }
    });
    connect(&_importAction, &mv::gui::TriggerAction::triggered, this, &QDialog::accept);
    connect(&_cancelAction, &mv::gui::TriggerAction::triggered, this, &QDialog::reject);

    _stagingDirectoryAction.setEnabled(backendType == "Tiled");
    updateImportChecks();
}

H5ADImportOptions H5ADImportDialog::options()
{
    H5ADImportOptions result;
    result.datasetName = _datasetNameAction.getString().trimmed();
    result.storageType = _storageTypeAction.getCurrentIndex() - 3;
    result.useTiledStorage = backendType() == "Tiled";
    result.tiledStorageConfiguration = _tiledConfiguration;
    result.optionalObjects = _optionalObjectsAction.getSelectedOptions();
    result.stagingDirectory = stagingDirectory();
    return result;
}

int H5ADImportDialog::storageTypeIndex() const
{
    return _storageTypeAction.getCurrentIndex();
}

QString H5ADImportDialog::backendType()
{
    return _backendSelector.getBackendTypeAction().getCurrentText();
}

QString H5ADImportDialog::stagingDirectory() const
{
    return _stagingDirectoryAction.getDirectory();
}

void H5ADImportDialog::updateImportChecks()
{
    std::size_t elementSize = sizeof(float);
    switch (_storageTypeAction.getCurrentIndex())
    {
        case 4: elementSize = 2; break; // bfloat16
        case 7: elementSize = 2; break; // int16
        case 8: elementSize = 2; break; // uint16
        case 9: elementSize = 1; break; // int8
        case 10: elementSize = 1; break; // uint8
        default: break; // Optimized, original, and 32-bit types use a conservative four-byte estimate.
    }

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

    const auto tiled = backendType() == "Tiled";
    if (!hasMatrixEstimate)
    {
        _memoryCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("The matrix size could not be estimated from the file dimensions."));
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("The required staging space could not be estimated."));
        return;
    }

    constexpr std::uint64_t eagerRecommendationThreshold = std::uint64_t { 4 } * 1024 * 1024 * 1024;
    const auto& systemHardware = mv::util::HardwareSpec::getSystemHardwareSpec();
    const auto ramSpec = systemHardware.getHardwareComponentSpec<mv::util::RamComponentSpec>(QStringLiteral("RAM"));
    const auto installedRamBytes = ramSpec && ramSpec->isInitialized() ? ramSpec->getNumberOfBytes() : 0;
    const auto matrixSize = mv::util::getNoBytesHumanReadable(matrixBytes);

    if (tiled)
    {
        _memoryCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("The matrix is estimated at %1 in dense-equivalent memory. Tiled storage loads bounded tiles, though the viewer, tile cache, metadata, and derived data also use memory.").arg(matrixSize));
    }
    else if (installedRamBytes > 0 && matrixBytes > installedRamBytes)
    {
        _memoryCheckAction.setObservation(mv::util::SeverityLevel::Warning, tr("The dense-equivalent estimate (%1) exceeds installed RAM (%2). Eager import is at risk for dense input; sparse input may use much less memory. Tiled storage is recommended.")
            .arg(matrixSize, mv::util::getNoBytesHumanReadable(installedRamBytes)), tr("Use tiled storage"));
    }
    else if (matrixBytes > eagerRecommendationThreshold)
    {
        _memoryCheckAction.setObservation(mv::util::SeverityLevel::Warning, tr("The dense-equivalent estimate is %1. Eager import may need additional working memory; tiled storage is recommended above 4 GiB.").arg(matrixSize), tr("Use tiled storage"));
    }
    else
    {
        _memoryCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("The dense-equivalent matrix estimate is %1. This excludes other datasets, metadata, and derived data.").arg(matrixSize));
    }

    if (!tiled)
    {
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("Staging space is not required for eager storage."));
        return;
    }

    const auto isDefaultStagingDirectory = QDir::cleanPath(stagingDirectory()) == QDir::cleanPath(_defaultStagingDirectory);
    const auto resetActionText = isDefaultStagingDirectory ? QString() : tr("Reset to default");
    QStorageInfo stagingStorage(_stagingDirectoryAction.getDirectory());
    stagingStorage.refresh();
    if (!stagingStorage.isValid() || !stagingStorage.isReady())
    {
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Warning, tr("Free space could not be checked for the selected staging directory."), tr("Choose directory"), resetActionText);
        return;
    }

    const auto availableBytesSigned = stagingStorage.bytesAvailable();
    if (availableBytesSigned < 0)
    {
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Warning, tr("Free space could not be checked for the selected staging directory."), tr("Choose directory"), resetActionText);
        return;
    }

    const auto availableBytes = static_cast<std::uint64_t>(availableBytesSigned);
    const auto availableSpace = mv::util::getNoBytesHumanReadable(availableBytes);
    if (availableBytes < matrixBytes)
    {
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Warning, tr("Potential disk-space issue: %1 is available, below the %2 dense-equivalent estimate. Sparse data and compression may reduce actual staging use.")
            .arg(availableSpace, matrixSize), tr("Choose directory"), resetActionText);
    }
    else
    {
        _stagingCheckAction.setObservation(mv::util::SeverityLevel::Info, tr("%1 is available; the dense-equivalent matrix estimate is %2. Sparse data and compression may reduce actual staging use.")
            .arg(availableSpace, matrixSize), {}, resetActionText);
    }
}
