#pragma once

#include <PointData/storage/interface/PointsStorageBackendTypes.h>
#include <actions/HorizontalGroupAction.h>
#include <actions/OptionAction.h>
#include <actions/ToggleAction.h>
#include <actions/TriggerAction.h>

#include <cstdint>

/** @brief Displays import and storage settings for one obsm matrix. */
class ObsmMatrixConfigurationAction : public mv::gui::HorizontalGroupAction
{
    Q_OBJECT

public:
    ObsmMatrixConfigurationAction(QObject* parent, const QString& matrixName, std::uint64_t rows, std::uint64_t columns, bool enabled, mv::pointdata::storage::StorageBackendType storageType);

    /** Returns the HDF5 matrix name. */
    QString matrixName() const;

    /** Returns the matrix row count. */
    std::uint64_t rows() const;

    /** Returns the matrix column count. */
    std::uint64_t columns() const;

    /** Returns whether this matrix is selected for import. */
    bool isEnabledForImport() const;

    /** Returns the selected storage backend. */
    mv::pointdata::storage::StorageBackendType storageType() const;

signals:
    /** Emitted when the user requests configuration of this matrix's storage backend. */
    void storageConfigurationRequested(const QString& matrixName, std::uint64_t rows, std::uint64_t columns, mv::pointdata::storage::StorageBackendType storageType);

    /** Emitted when the user enables or disables this matrix. */
    void enabledForImportChanged();

private:
    QString _matrixName; /**< HDF5 matrix name. */
    std::uint64_t _rows = 0; /**< Matrix row count. */
    std::uint64_t _columns = 0; /**< Matrix column count. */
    mv::gui::ToggleAction _enabledAction; /**< Enables this matrix for import. */
    mv::gui::OptionAction _storageTypeAction; /**< Selects dense or tiled storage. */
    mv::gui::TriggerAction _configureStorageAction; /**< Opens configuration for the selected backend. */
};
