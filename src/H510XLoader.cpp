#include "H510XLoader.h"

#include "DataTransform.h"
#include "HDF5_10X_Loader.h"

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

Q_PLUGIN_METADATA(IID "nl.lumc.H510XLoader")

using namespace mv;

namespace
{
	bool& Hdf5Lock()
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
		const QString conversionIndexKey("conversionIndex");
		const QString transformValueKey("transformValue");
		const QString storageValueKey("storageValue");
		const QString fileNameKey("fileName");
		const QString normalizeKey("normalize");
		const QString selectedNameFilterKey("selectedNameFilter");
	}

}	// Unnamed namespace


// =============================================================================
// Loader
// =============================================================================

H510XLoader::H510XLoader(PluginFactory* factory)
	: LoaderPlugin(factory),
	_fileDialog()
{

}

H510XLoader::~H510XLoader(void)
{

}

void H510XLoader::init()
{
	QStringList fileTypeOptions;
	
	fileTypeOptions.append("10X (*.h5)");
	
	_fileDialog.setOption(QFileDialog::DontUseNativeDialog);
	_fileDialog.setFileMode(QFileDialog::ExistingFiles);
	_fileDialog.setNameFilters(fileTypeOptions);
}

void H510XLoader::loadData()
{
	if (Hdf5Lock())
	{
		QMessageBox::information(nullptr, "HDF5 Loader is busy", "The HDF5 Loader is already loading a file. You cannot load another file until loading has completed.");
		return;
	};
	LockGuard lockGuard(Hdf5Lock());

	QGridLayout* fileDialogLayout = dynamic_cast<QGridLayout*>(_fileDialog.layout());

	int rowCount = fileDialogLayout->rowCount();

	QComboBox* storageTypeComboBox = new QComboBox;
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
			const auto value = getSetting(Keys::storageValueKey, 0);
			return value.toInt();
		}());

	fileDialogLayout->addWidget(storageTypeLabel, rowCount, 0);
	fileDialogLayout->addWidget(storageTypeComboBox, rowCount, 1);

	TRANSFORM::Control transform(fileDialogLayout);

	const auto conversionIndexSetting = getSetting(Keys::conversionIndexKey, QVariant::QVariant());
	if (conversionIndexSetting.isValid())
	{
		TRANSFORM::Index index = static_cast<TRANSFORM::Index>(conversionIndexSetting.toInt());
		if (index == TRANSFORM::ARCSIN5)
		{
			const auto transformValueSetting = getSetting(Keys::transformValueKey, QVariant::QVariant());

			if (transformValueSetting.isValid())
			{
				TRANSFORM::Type transform_type;
				transform_type.first = index;
				transform_type.second = transformValueSetting.toDouble();
				transform.set(transform_type);
			}
		}
		else
		{
			TRANSFORM::Type type_pair;
			type_pair.first = index;
			type_pair.second = 1.0f;
			transform.set(type_pair);
		}
	}

	const auto selectedNameFilterSetting = getSetting(Keys::selectedNameFilterKey, QVariant::QVariant());
	if (selectedNameFilterSetting.isValid())
		_fileDialog.selectNameFilter(selectedNameFilterSetting.toString());

	const auto fileNameSetting = getSetting(Keys::fileNameKey, QVariant::QVariant());
	if (fileNameSetting.isValid())
		_fileDialog.selectFile(fileNameSetting.toString());

	// keeping storageTypeComboBox for now since we probably should implement this functionality
	storageTypeComboBox->setVisible(false);
	storageTypeLabel->setVisible(false);

	transform.setVisible(true);
	transform.setTransform(0);

	if (_fileDialog.exec())
	{
		QStringList fileNames = _fileDialog.selectedFiles();

		if (fileNames.empty())
		{
			return;
		}
		const QString firstFileName = fileNames.constFirst();

		QString selectedNameFilter = _fileDialog.selectedNameFilter();
		const TRANSFORM::Type transform_setting = transform.get();
		
		setSetting(Keys::conversionIndexKey, transform_setting.first);
		setSetting(Keys::storageValueKey, storageTypeComboBox->currentIndex());
		setSetting(Keys::transformValueKey, transform_setting.second);
		setSetting(Keys::fileNameKey, firstFileName);
		setSetting(Keys::selectedNameFilterKey, selectedNameFilter);

		for (const auto& fileName : fileNames)
		{
			HDF5_10X_Loader loader(_core);
			if (loader.open(fileName))
			{
				loader.load(transform_setting, 0);
			}
			else
			{
				QString mesg = "Could not open " + fileName + ". Make sure the file has the correct file extension and is not corrupted.";
				QMessageBox::critical(nullptr, "Error loading file(s)", mesg);
			}
		}
		
	}
}

