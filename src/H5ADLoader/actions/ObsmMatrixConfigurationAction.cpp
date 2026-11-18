#include "ObsmMatrixConfigurationAction.h"

#include <util/Miscellaneous.h>

#include <limits>

namespace
{

QString getStorageBackendLabel(const mv::pointdata::storage::StorageBackendType type)
{
    const auto capabilities = mv::pointdata::storage::getStorageBackendCapabilities(type);
    QStringList accessModes;

    if (mv::pointdata::storage::hasStorageBackendCapability(capabilities, mv::pointdata::storage::StorageBackendCapability::RandomAccess))
        accessModes.append(QObject::tr("read"));
    if (mv::pointdata::storage::hasStorageBackendCapability(capabilities, mv::pointdata::storage::StorageBackendCapability::Writable))
        accessModes.append(QObject::tr("write"));

    const auto backendName = mv::pointdata::storage::getStorageBackendTypeName(type);
    return accessModes.isEmpty() ? backendName : QString("%1 (%2)").arg(backendName, accessModes.join(" | "));
}

QString getMatrixDimensionsAndSize(const std::uint64_t rows, const std::uint64_t columns)
{
    constexpr auto estimatedElementSize = sizeof(float);
    const auto sizeAvailable = columns == 0 || rows <= std::numeric_limits<std::uint64_t>::max() / columns / estimatedElementSize;
    const auto estimatedBytes = sizeAvailable ? rows * columns * estimatedElementSize : std::numeric_limits<std::uint64_t>::max();
    const auto sizeText = sizeAvailable ? mv::util::getNoBytesHumanReadable(estimatedBytes) : QObject::tr("at least %1").arg(mv::util::getNoBytesHumanReadable(estimatedBytes));

    return QObject::tr("(%1 × %2, %3)").arg(rows).arg(columns).arg(sizeText);
}

}

ObsmMatrixConfigurationAction::ObsmMatrixConfigurationAction(QObject* parent, const QString& matrixName, const std::uint64_t rows, const std::uint64_t columns, const bool enabled, const mv::pointdata::storage::StorageBackendType storageType) :
    mv::gui::HorizontalGroupAction(parent, matrixName),
    _matrixName(matrixName),
    _rows(rows),
    _columns(columns),
    _enabledAction(this, QString("%1 %2").arg(matrixName, getMatrixDimensionsAndSize(rows, columns)), enabled),
    _storageTypeAction(this, "Storage type", { getStorageBackendLabel(mv::pointdata::storage::StorageBackendType::Dense), getStorageBackendLabel(mv::pointdata::storage::StorageBackendType::Tiled) }, getStorageBackendLabel(storageType == mv::pointdata::storage::StorageBackendType::Tiled ? mv::pointdata::storage::StorageBackendType::Tiled : mv::pointdata::storage::StorageBackendType::Dense)),
    _configureStorageAction(this, "Configure storage")
{
    setShowLabels(false);

    _enabledAction.setDefaultWidgetFlags(mv::gui::ToggleAction::CheckBox);
    _enabledAction.setToolTip(tr("Import %1; dimensions and estimated dense size: %2").arg(matrixName, getMatrixDimensionsAndSize(rows, columns)));
    _enabledAction.setStretch(2);
    _storageTypeAction.setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
    _configureStorageAction.setIconByName("gear");
    _configureStorageAction.setToolTip(tr("Configure storage for %1").arg(matrixName));

    const auto updateStorageControls = [this](const bool enabled) {
        _storageTypeAction.setEnabled(enabled);
        _configureStorageAction.setEnabled(enabled);
    };
    updateStorageControls(enabled);

    addAction(&_enabledAction, mv::gui::ToggleAction::CheckBox);
    addAction(&_storageTypeAction, mv::gui::OptionAction::ComboBox);
    addAction(&_configureStorageAction, mv::gui::TriggerAction::Icon);

    connect(&_enabledAction, &QAction::toggled, this, &ObsmMatrixConfigurationAction::enabledForImportChanged);
    connect(&_enabledAction, &QAction::toggled, this, updateStorageControls);
    connect(&_configureStorageAction, &mv::gui::TriggerAction::triggered, this, [this] {
        emit storageConfigurationRequested(_matrixName, _rows, _columns, this->storageType());
    });
}

QString ObsmMatrixConfigurationAction::matrixName() const
{
    return _matrixName;
}

std::uint64_t ObsmMatrixConfigurationAction::rows() const
{
    return _rows;
}

std::uint64_t ObsmMatrixConfigurationAction::columns() const
{
    return _columns;
}

bool ObsmMatrixConfigurationAction::isEnabledForImport() const
{
    return _enabledAction.isChecked();
}

mv::pointdata::storage::StorageBackendType ObsmMatrixConfigurationAction::storageType() const
{
    return _storageTypeAction.getCurrentIndex() == 1 ? mv::pointdata::storage::StorageBackendType::Tiled : mv::pointdata::storage::StorageBackendType::Dense;
}
