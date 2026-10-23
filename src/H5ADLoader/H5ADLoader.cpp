#include "H5ADLoader.h"

#include "HDF5_AD_Loader.h"

#include "PointData/PointData.h"

#include <PointData/storage/widgets/StorageBackendConfigurationDialog.h>
#include <PointData/storage/actions/TiledStorageBackendConversionAction.h>

#include <CoreInterface.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <memory>
#include <utility>

Q_PLUGIN_METADATA(IID "nl.lumc.H5ADLoader")

// =============================================================================
// Loader
// =============================================================================

using namespace mv;

namespace
{
	bool &Hdf5Lock()
	{
		static bool lock = false;
		return lock;
	}
	class LockGuard
	{
		bool& _lock;
		LockGuard() = delete;
	public:
		explicit LockGuard(bool& b)
			:_lock(b)
		{
			_lock = true;
		}
		~LockGuard()
		{
			_lock = false;
		}
	};

	// Alphabetic list of keys used to access settings from QSettings.
	namespace Keys
	{
		const QString storageValueKey("storageValue");
		const QString storageBackendKey("storageBackend");
		const QString storageBackendConfigurationKey("storageBackendConfiguration");
		const QString fileNameKey("fileName");
		const QString selectedNameFilterKey("selectedNameFilter");
	}

	mv::pointdata::storage::StorageBackendConfiguration getTiledConfiguration(const QVariantMap& values)
	{
		mv::pointdata::storage::StorageBackendConfiguration configuration;
		configuration.rowTileSize = values.value("rowTileSize", 256).toULongLong();
		configuration.columnTileSize = values.value("columnTileSize", 256).toULongLong();
		configuration.cacheCapacity = values.value("cacheCapacity", 8).toULongLong();
		configuration.cacheCapacityBytes = values.value("cacheCapacityBytes", 64U * 1024U * 1024U).toULongLong();
		configuration.packedTiles = values.value("packedTiles", true).toBool();
		configuration.superTileRows = values.value("superTileRows", 4).toULongLong();
		configuration.superTileColumns = values.value("superTileColumns", 4).toULongLong();
		return configuration;
	}

	QVariantMap toTiledConfigurationVariantMap(const mv::pointdata::storage::StorageBackendConfiguration& configuration)
	{
		return {
			{ "rowTileSize", QVariant::fromValue<qulonglong>(configuration.rowTileSize) },
			{ "columnTileSize", QVariant::fromValue<qulonglong>(configuration.columnTileSize) },
			{ "cacheCapacity", QVariant::fromValue<qulonglong>(configuration.cacheCapacity) },
			{ "cacheCapacityBytes", QVariant::fromValue<qulonglong>(configuration.cacheCapacityBytes) },
			{ "packedTiles", configuration.packedTiles },
			{ "superTileRows", QVariant::fromValue<qulonglong>(configuration.superTileRows) },
			{ "superTileColumns", QVariant::fromValue<qulonglong>(configuration.superTileColumns) }
		};
	}

}	// Unnamed namespace


H5ADLoader::H5ADLoader(PluginFactory* factory)
    : LoaderPlugin(factory),
	_storageBackendSelector(this, { "Eager", "Tiled" }, "Tiled")
{
	_tiledStorageConfiguration.rowTileSize = 256;
	_tiledStorageConfiguration.columnTileSize = 256;
	_tiledStorageConfiguration.cacheCapacity = 8;
	_tiledStorageConfiguration.cacheCapacityBytes = 64U * 1024U * 1024U;
	_tiledStorageConfiguration.packedTiles = true;
	_tiledStorageConfiguration.superTileRows = 4;
	_tiledStorageConfiguration.superTileColumns = 4;
}

H5ADLoader::~H5ADLoader(void)
{
	
}

void H5ADLoader::init()
{
	QStringList fileTypeOptions;
	fileTypeOptions.append("H5AD (*.h5ad)");
	_fileDialog.setOption(QFileDialog::DontUseNativeDialog);
	_fileDialog.setFileMode(QFileDialog::ExistingFiles);
	_fileDialog.setNameFilters(fileTypeOptions);
	auto* fileDialogLayout = qobject_cast<QGridLayout*>(_fileDialog.layout());
	if (fileDialogLayout)
		fileDialogLayout->addWidget(_storageBackendSelector.createWidget(&_fileDialog), fileDialogLayout->rowCount(), 0, 1, 3);

	_storageBackendSelector.setConfigureEnabled(true);
	connect(&_storageBackendSelector, &StorageBackendConfiguratorAction::backendTypeChanged, this, [this](const QString& backendType) {
		_storageBackendSelector.setConfigureEnabled(backendType == "Tiled");
	});
	connect(&_storageBackendSelector, &StorageBackendConfiguratorAction::configureRequested, this, [this] {
		auto conversionAction = std::make_unique<mv::gui::TiledStorageBackendConversionAction>(nullptr, 1, 1, sizeof(float), _tiledStorageConfiguration, true);
		StorageBackendConfigurationDialog dialog(std::move(conversionAction), false, QApplication::activeWindow());
		if (dialog.exec() != QDialog::Accepted)
			return;

		const auto configuration = std::dynamic_pointer_cast<const mv::pointdata::storage::StorageBackendConfiguration>(dialog.getConversionOptions());
		if (!configuration)
			return;

		_tiledStorageConfiguration.rowTileSize = configuration->rowTileSize;
		_tiledStorageConfiguration.columnTileSize = configuration->columnTileSize;
		_tiledStorageConfiguration.cacheCapacity = configuration->cacheCapacity;
		_tiledStorageConfiguration.packedTiles = configuration->packedTiles;
		_tiledStorageConfiguration.superTileRows = configuration->superTileRows;
		_tiledStorageConfiguration.superTileColumns = configuration->superTileColumns;
		setSetting(Keys::storageBackendConfigurationKey, toTiledConfigurationVariantMap(_tiledStorageConfiguration));
	});
}

void H5ADLoader::loadData()
{
	if (Hdf5Lock())
	{
		QMessageBox::information(nullptr, "H5AD Loader is busy", "The H5AD Loader is already loading a file. You cannot load another file until loading has completed.");
		return;
	};
	LockGuard lockGuard(Hdf5Lock());
	
	QGridLayout* fileDialogLayout = dynamic_cast<QGridLayout*>(_fileDialog.layout());
	_storageBackendSelector.getBackendTypeAction().setCurrentText(getSetting(Keys::storageBackendKey, "Tiled").toString());
	_tiledStorageConfiguration = getTiledConfiguration(getSetting(Keys::storageBackendConfigurationKey, QVariantMap()).toMap());

	int rowCount = fileDialogLayout->rowCount();

	QComboBox *storageTypeComboBox = new QComboBox;
	QLabel* storageTypeLabel = new QLabel("Data Type");

	storageTypeComboBox->addItem("Optimized (lossless)", -3);
	storageTypeComboBox->addItem("Optimized (incl bfloat16)", -2);
	storageTypeComboBox->addItem("Original", -1);
	auto elementTypeNames = PointData::getElementTypeNames();
	QStringList dataTypeList(elementTypeNames.cbegin(), elementTypeNames.cend());
	for (int i = 0; i < dataTypeList.size(); ++i)
	{
		storageTypeComboBox->addItem(dataTypeList[i], i);
	}
	
	storageTypeComboBox->setCurrentIndex([this]
	{
		const auto value = getSetting(Keys::storageValueKey, 0).toInt();
		return value;
	}());
		
	fileDialogLayout->addWidget(storageTypeLabel, rowCount, 0);
	fileDialogLayout->addWidget(storageTypeComboBox, rowCount, 1);

	const auto selectedNameFilterSetting = getSetting(Keys::selectedNameFilterKey, QVariant());
	if (selectedNameFilterSetting.isValid())
		_fileDialog.selectNameFilter(selectedNameFilterSetting.toString());

	const auto fileNameSetting = getSetting(Keys::fileNameKey, QVariant());
	if (fileNameSetting.isValid())
		_fileDialog.selectFile(fileNameSetting.toString());

	storageTypeComboBox->setVisible(true);
	storageTypeLabel->setVisible(true);
	if (_fileDialog.exec())
	{
		QStringList fileNames = _fileDialog.selectedFiles();

		if (fileNames.empty())
		{
			return;
		}
		const QString firstFileName = fileNames.constFirst();

		bool result = true;
		QString selectedNameFilter = _fileDialog.selectedNameFilter();
		
		setSetting(Keys::storageValueKey, storageTypeComboBox->currentIndex());
		setSetting(Keys::storageBackendKey, _storageBackendSelector.getBackendTypeAction().getCurrentText());
		setSetting(Keys::fileNameKey, firstFileName);
		setSetting(Keys::selectedNameFilterKey, selectedNameFilter);
		const auto stagingDirectory = projects().ensureTemporaryDirPath(AbstractProjectManager::TemporaryDirType::Staging);
		
		for (const auto& fileName : fileNames)
		{
			auto loader = std::make_shared<HDF5_AD_Loader>(_core, this, stagingDirectory);
			if (loader->open(fileName))
				loader->load(storageTypeComboBox->currentData().toInt(), _storageBackendSelector.getBackendTypeAction().getCurrentText() == "Tiled", _tiledStorageConfiguration);
			else
			{
				QString mesg = "Could not open " + fileName + ". Make sure the file has the correct file extension and is not corrupted.";
				QMessageBox::critical(nullptr, "Error loading file(s)", mesg);
			}
		}
	}
}

