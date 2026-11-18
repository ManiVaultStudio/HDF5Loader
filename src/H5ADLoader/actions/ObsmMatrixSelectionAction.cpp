#include "ObsmMatrixSelectionAction.h"

#include "../HDF5_AD_Loader.h"

#include <utility>

ObsmMatrixSelectionAction::ObsmMatrixSelectionAction(QObject* parent) :
    mv::gui::VerticalGroupAction(parent, "Matrix selection", true)
{
    setShowLabels(false);
}

void ObsmMatrixSelectionAction::setMatrices(const QStringList& matrices, const QStringList& selectedMatrices, const QMap<QString, mv::pointdata::storage::StorageBackendType>& storageTypes, HDF5_AD_Loader* loader)
{
    removeAllActions();
    _matrixActions.clear();
    _matrixActions.reserve(static_cast<std::size_t>(matrices.size()));

    for (const auto& name : matrices)
    {
        const auto shape = loader ? loader->getObsmMatrixShape(name) : std::pair<std::uint64_t, std::uint64_t>{ 0, 0 };
        auto matrixAction = std::make_unique<ObsmMatrixConfigurationAction>(this, name, shape.first, shape.second, selectedMatrices.contains(name), storageTypes.value(name, mv::pointdata::storage::StorageBackendType::Dense));
        connect(matrixAction.get(), &ObsmMatrixConfigurationAction::storageConfigurationRequested, this, &ObsmMatrixSelectionAction::storageConfigurationRequested);
        connect(matrixAction.get(), &ObsmMatrixConfigurationAction::enabledForImportChanged, this, &ObsmMatrixSelectionAction::selectionChanged);
        addAction(matrixAction.get());
        _matrixActions.push_back(std::move(matrixAction));
    }
}

QStringList ObsmMatrixSelectionAction::selectedMatrices() const
{
    QStringList selected;
    for (const auto& action : _matrixActions)
        if (action->isEnabledForImport())
            selected.append(action->matrixName());
    return selected;
}

QMap<QString, mv::pointdata::storage::StorageBackendType> ObsmMatrixSelectionAction::storageTypes() const
{
    QMap<QString, mv::pointdata::storage::StorageBackendType> storageTypes;
    for (const auto& action : _matrixActions)
        storageTypes.insert(action->matrixName(), action->storageType());
    return storageTypes;
}
