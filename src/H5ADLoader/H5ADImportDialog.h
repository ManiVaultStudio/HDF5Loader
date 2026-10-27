#pragma once

#include "HDF5_AD_Loader.h"

#include <PointData/StorageBackendConfiguratorAction.h>

#include <QDialog>
#include <QStandardItemModel>

class QComboBox;
class QLineEdit;

/** Collects H5AD import choices after a file has been selected and opened. */
class H5ADImportDialog : public QDialog
{
public:
    H5ADImportDialog(const QString& fileName, const QStringList& optionalObjects, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, QWidget* parent = nullptr);

    /** Returns the selected import options after the dialog is accepted. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

private:
    StorageBackendConfiguratorAction _backendSelector;
    mv::pointdata::storage::StorageBackendConfiguration _tiledConfiguration;
    QStandardItemModel _optionalObjectsModel;
    QLineEdit* _datasetNameEdit = nullptr;
    QComboBox* _storageTypeComboBox = nullptr;
    std::uint64_t _rows = 0;
    std::uint64_t _columns = 0;
};
