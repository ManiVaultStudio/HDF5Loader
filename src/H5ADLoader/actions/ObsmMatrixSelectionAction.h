#pragma once

#include "ObsmMatrixConfigurationAction.h"

#include <actions/VerticalGroupAction.h>

#include <QMap>
#include <QStringList>

#include <memory>
#include <vector>

class HDF5_AD_Loader;

/** @brief Vertically groups the selectable obsm matrix configuration actions. */
class ObsmMatrixSelectionAction : public mv::gui::VerticalGroupAction
{
    Q_OBJECT

public:
    explicit ObsmMatrixSelectionAction(QObject* parent);

    /** Rebuilds the per-matrix actions from the available matrices and saved choices. */
    void setMatrices(const QStringList& matrices, const QStringList& selectedMatrices, const QMap<QString, mv::pointdata::storage::StorageBackendType>& storageTypes, HDF5_AD_Loader* loader);

    /** Returns the names of matrices enabled for import. */
    QStringList selectedMatrices() const;

    /** Returns the selected storage backend for each available matrix. */
    QMap<QString, mv::pointdata::storage::StorageBackendType> storageTypes() const;

signals:
    /** Emitted when a matrix's storage configuration is requested. */
    void storageConfigurationRequested(const QString& matrixName, std::uint64_t rows, std::uint64_t columns, mv::pointdata::storage::StorageBackendType storageType);

    /** Emitted when a matrix is enabled or disabled. */
    void selectionChanged();

private:
    std::vector<std::unique_ptr<ObsmMatrixConfigurationAction>> _matrixActions; /**< Owned per-matrix controls displayed as children. */
};
