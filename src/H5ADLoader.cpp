#include "H5ADLoader.h"

#include "HDF5_AD_Loader.h"

#include "PointData/PointData.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QComboBox>
#include <QDebug>
#include <QLabel>
#include <QCheckBox>
#include <QString>
#include <QStringList>
#include <QMessageBox>

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
	}

}	// Unnamed namespace


H5ADLoader::H5ADLoader(PluginFactory* factory)
    : LoaderPlugin(factory),
	_fileDialog()
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

	const auto selectedNameFilterSetting = getSetting(Keys::selectedNameFilterKey, QVariant::QVariant());
	if (selectedNameFilterSetting.isValid())
		_fileDialog.selectNameFilter(selectedNameFilterSetting.toString());

	const auto fileNameSetting = getSetting(Keys::fileNameKey, QVariant::QVariant());
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
		setSetting(Keys::fileNameKey, firstFileName);
		setSetting(Keys::selectedNameFilterKey, selectedNameFilter);
		
		HDF5_AD_Loader loader(_core);
		for (const auto& fileName : fileNames)
		{
			if (loader.open(fileName))
				loader.load(storageTypeComboBox->currentData().toInt());
			else
			{
				QString mesg = "Could not open " + fileName + ". Make sure the file has the correct file extension and is not corrupted.";
				QMessageBox::critical(nullptr, "Error loading file(s)", mesg);
			}
		}
	}
}

