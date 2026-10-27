#include "H5ADLoader.h"

#include "HDF5_AD_Loader.h"
#include "H5ADImportDialog.h"

#include <CoreInterface.h>

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <memory>

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
    : LoaderPlugin(factory)
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
}

void H5ADLoader::loadData()
{
	if (Hdf5Lock())
	{
		QMessageBox::information(nullptr, "H5AD Loader is busy", "The H5AD Loader is already loading a file. You cannot load another file until loading has completed.");
		return;
	}
	LockGuard lockGuard(Hdf5Lock());

	auto backendType = getSetting(Keys::storageBackendKey, "Tiled").toString();
	auto storageTypeIndex = getSetting(Keys::storageValueKey, 0).toInt();
	_tiledStorageConfiguration = getTiledConfiguration(getSetting(Keys::storageBackendConfigurationKey, QVariantMap()).toMap());

	const auto selectedNameFilterSetting = getSetting(Keys::selectedNameFilterKey, QVariant());
	if (selectedNameFilterSetting.isValid())
		_fileDialog.selectNameFilter(selectedNameFilterSetting.toString());

	const auto fileNameSetting = getSetting(Keys::fileNameKey, QVariant());
	if (fileNameSetting.isValid())
		_fileDialog.selectFile(fileNameSetting.toString());

	if (_fileDialog.exec() != QDialog::Accepted)
		return;

	const auto fileNames = _fileDialog.selectedFiles();
	if (fileNames.empty())
		return;

	setSetting(Keys::fileNameKey, fileNames.constFirst());
	setSetting(Keys::selectedNameFilterKey, _fileDialog.selectedNameFilter());
	const auto defaultStagingDirectory = projects().ensureTemporaryDirPath(AbstractProjectManager::TemporaryDirType::Staging);
	auto stagingDirectory = defaultStagingDirectory;

	for (const auto& fileName : fileNames)
	{
		auto loader = std::make_shared<HDF5_AD_Loader>(_core, this, stagingDirectory);
		if (!loader->open(fileName))
		{
			QMessageBox::critical(nullptr, tr("Error loading file"), tr("Could not open %1. Make sure the file is a valid H5AD file.").arg(fileName));
			continue;
		}

		const auto [rows, columns] = loader->getMatrixShape();
		H5ADImportDialog dialog(fileName, stagingDirectory, defaultStagingDirectory, loader->getOptionalObjects(), rows, columns, storageTypeIndex, backendType, _tiledStorageConfiguration, loader, QApplication::activeWindow());
		if (dialog.exec() != QDialog::Accepted)
			continue;

		const auto options = dialog.options();
		backendType = dialog.backendType();
		storageTypeIndex = dialog.storageTypeIndex();
		stagingDirectory = dialog.stagingDirectory();
		_tiledStorageConfiguration = options.tiledStorageConfiguration;
		setSetting(Keys::storageValueKey, storageTypeIndex);
		setSetting(Keys::storageBackendKey, backendType);
		setSetting(Keys::storageBackendConfigurationKey, toTiledConfigurationVariantMap(_tiledStorageConfiguration));
		loader->load(options);
	}
}

