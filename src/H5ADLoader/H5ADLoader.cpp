#include "H5ADLoader.h"

#include "HDF5_AD_Loader.h"

#include "PointData/PointData.h"

#include <CoreInterface.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>

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
		const QString fileNameKey("fileName");
		const QString selectedNameFilterKey("selectedNameFilter");
		const QString stagingDirectoryKey("stagingDirectory");
	}

}	// Unnamed namespace


H5ADLoader::H5ADLoader(PluginFactory* factory)
    : LoaderPlugin(factory)
{
	
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
	};
	LockGuard lockGuard(Hdf5Lock());
	
	QGridLayout* fileDialogLayout = dynamic_cast<QGridLayout*>(_fileDialog.layout());

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

	const auto defaultStagingDirectory = projects().ensureTemporaryDirPath(AbstractProjectManager::TemporaryDirType::Staging);
	auto* stagingDirectoryLabel = new QLabel("Staging directory");
	auto* stagingDirectoryEdit = new QLineEdit(getSetting(Keys::stagingDirectoryKey, defaultStagingDirectory).toString());
	auto* stagingDirectoryButton = new QPushButton("Browse...");
	auto* stagingDirectoryLayout = new QHBoxLayout;
	stagingDirectoryLayout->setContentsMargins(0, 0, 0, 0);
	stagingDirectoryLayout->addWidget(stagingDirectoryEdit);
	stagingDirectoryLayout->addWidget(stagingDirectoryButton);
	fileDialogLayout->addWidget(stagingDirectoryLabel, rowCount + 1, 0);
	fileDialogLayout->addLayout(stagingDirectoryLayout, rowCount + 1, 1);
	connect(stagingDirectoryButton, &QPushButton::clicked, stagingDirectoryButton, [stagingDirectoryEdit] {
		const auto directory = QFileDialog::getExistingDirectory(nullptr, "Select H5AD staging directory", stagingDirectoryEdit->text());
		if (!directory.isEmpty())
			stagingDirectoryEdit->setText(directory);
	});

	const auto selectedNameFilterSetting = getSetting(Keys::selectedNameFilterKey, QVariant());
	if (selectedNameFilterSetting.isValid())
		_fileDialog.selectNameFilter(selectedNameFilterSetting.toString());

	const auto fileNameSetting = getSetting(Keys::fileNameKey, QVariant());
	if (fileNameSetting.isValid())
		_fileDialog.selectFile(fileNameSetting.toString());

	storageTypeComboBox->setVisible(true);
	storageTypeLabel->setVisible(true);
	stagingDirectoryLabel->setVisible(true);
	stagingDirectoryEdit->setVisible(true);
	stagingDirectoryButton->setVisible(true);

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
		setSetting(Keys::fileNameKey, firstFileName);
		setSetting(Keys::selectedNameFilterKey, selectedNameFilter);
		const auto stagingDirectory = QDir::cleanPath(stagingDirectoryEdit->text());
		if (stagingDirectory.isEmpty() || !QDir().mkpath(stagingDirectory))
		{
			QMessageBox::critical(nullptr, "Error loading file(s)", "The selected H5AD staging directory could not be created.");
			return;
		}
		setSetting(Keys::stagingDirectoryKey, stagingDirectory);
		
		for (const auto& fileName : fileNames)
		{
			auto loader = std::make_shared<HDF5_AD_Loader>(_core, this, stagingDirectory);
			if (loader->open(fileName))
				loader->load(storageTypeComboBox->currentData().toInt());
			else
			{
				QString mesg = "Could not open " + fileName + ". Make sure the file has the correct file extension and is not corrupted.";
				QMessageBox::critical(nullptr, "Error loading file(s)", mesg);
			}
		}
	}
}

