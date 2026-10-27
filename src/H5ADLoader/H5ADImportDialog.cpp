#include "H5ADImportDialog.h"

#include <PointData/PointData.h>
#include <PointData/storage/actions/TiledStorageBackendConversionAction.h>
#include <PointData/storage/widgets/StorageBackendConfigurationDialog.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

H5ADImportDialog::H5ADImportDialog(const QString& fileName, const QStringList& optionalObjects, const std::uint64_t rows, const std::uint64_t columns, const int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, QWidget* parent) :
    QDialog(parent),
    _backendSelector(this, { "Eager", "Tiled" }, "Tiled"),
    _tiledConfiguration(tiledConfiguration),
    _rows(rows),
    _columns(columns)
{
    setWindowTitle(tr("Import H5AD"));
    resize(520, 440);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("File: %1").arg(QFileInfo(fileName).fileName()), this));
    if (rows > 0 && columns > 0)
        layout->addWidget(new QLabel(tr("Matrix: %1 rows × %2 columns").arg(rows).arg(columns), this));

    auto* form = new QFormLayout;
    _datasetNameEdit = new QLineEdit(QFileInfo(fileName).baseName(), this);
    _storageTypeComboBox = new QComboBox(this);
    _storageTypeComboBox->addItem(tr("Optimized (lossless)"), -3);
    _storageTypeComboBox->addItem(tr("Optimized (incl bfloat16)"), -2);
    _storageTypeComboBox->addItem(tr("Original"), -1);
    const auto elementTypeNames = PointData::getElementTypeNames();
    for (int index = 0; index < elementTypeNames.size(); ++index)
        _storageTypeComboBox->addItem(elementTypeNames[index], index);
    _storageTypeComboBox->setCurrentIndex(qBound(0, storageTypeIndex, _storageTypeComboBox->count() - 1));

    form->addRow(tr("Dataset name"), _datasetNameEdit);
    form->addRow(tr("Data type"), _storageTypeComboBox);
    form->addRow(tr("Storage"), _backendSelector.createWidget(this));
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("Optional metadata"), this));
    for (const auto& name : optionalObjects)
    {
        auto* item = new QStandardItem(name);
        item->setCheckable(true);
        item->setCheckState(name.startsWith("obs") ? Qt::Checked : Qt::Unchecked);
        _optionalObjectsModel.appendRow(item);
    }
    auto* optionalObjectsView = new QListView(this);
    optionalObjectsView->setModel(&_optionalObjectsModel);
    layout->addWidget(optionalObjectsView);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    layout->addWidget(buttons);

    _backendSelector.getBackendTypeAction().setCurrentText(backendType);
    _backendSelector.setConfigureEnabled(backendType == "Tiled");
    connect(&_backendSelector, &StorageBackendConfiguratorAction::backendTypeChanged, this, [this](const QString& type) {
        _backendSelector.setConfigureEnabled(type == "Tiled");
    });
    connect(&_backendSelector, &StorageBackendConfiguratorAction::configureRequested, this, [this] {
        auto conversionAction = std::make_unique<mv::gui::TiledStorageBackendConversionAction>(nullptr, _rows, _columns, sizeof(float), _tiledConfiguration, true);
        StorageBackendConfigurationDialog dialog(std::move(conversionAction), false, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        const auto configuration = std::dynamic_pointer_cast<const mv::pointdata::storage::StorageBackendConfiguration>(dialog.getConversionOptions());
        if (!configuration)
            return;

        _tiledConfiguration.rowTileSize = configuration->rowTileSize;
        _tiledConfiguration.columnTileSize = configuration->columnTileSize;
        _tiledConfiguration.cacheCapacity = configuration->cacheCapacity;
        _tiledConfiguration.packedTiles = configuration->packedTiles;
        _tiledConfiguration.superTileRows = configuration->superTileRows;
        _tiledConfiguration.superTileColumns = configuration->superTileColumns;
    });
    connect(_datasetNameEdit, &QLineEdit::textChanged, buttons->button(QDialogButtonBox::Ok), [buttons](const QString& name) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!name.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

H5ADImportOptions H5ADImportDialog::options()
{
    H5ADImportOptions result;
    result.datasetName = _datasetNameEdit->text().trimmed();
    result.storageType = _storageTypeComboBox->currentData().toInt();
    result.useTiledStorage = backendType() == "Tiled";
    result.tiledStorageConfiguration = _tiledConfiguration;
    for (int index = 0; index < _optionalObjectsModel.rowCount(); ++index)
    {
        const auto* item = _optionalObjectsModel.item(index);
        if (item->checkState() == Qt::Checked)
            result.optionalObjects.append(item->text());
    }
    return result;
}

int H5ADImportDialog::storageTypeIndex() const
{
    return _storageTypeComboBox->currentIndex();
}

QString H5ADImportDialog::backendType()
{
    return _backendSelector.getBackendTypeAction().getCurrentText();
}
