#include "H510XLoader.h"

#include "DataTransform.h"

#include "HDF5_10X_Loader.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QComboBox>
#include <QDebug>
#include <QLabel>
#include <QCheckBox>
#include <QString>
#include <QStringList>
#include <QSettings>
#include "PointData/PointData.h"
#include "QMessageBox"


Q_PLUGIN_METADATA(IID "nl.lumc.H510XLoader")

// =============================================================================
// Loader

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
		const QLatin1String conversionIndexKey("conversionIndex");
		const QLatin1String transformValueKey("transformValue");
		const QLatin1String storageValueKey("storageValue");
		const QLatin1String fileNameKey("fileName");
		const QLatin1String normalizeKey("normalize");
		const QLatin1String selectedNameFilterKey("selectedNameFilter");

	}


	// Call the specified function on the specified value, if it is valid.
	template <typename TFunction>
	void IfValid(const QVariant& value, const TFunction& function)
	{
		if (value.isValid())
		{
			function(value);
		}
	}

}	// Unnamed namespace



H510XLoader::H510XLoader(PluginFactory* factory)
	: LoaderPlugin(factory)

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

	QSettings settings(QString::fromLatin1("HDPS"), QString::fromLatin1("Plugins/H510XLoader"));
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

	storageTypeComboBox->setCurrentIndex([&settings]
		{
			const auto value = settings.value(Keys::storageValueKey);
			if (value.isValid())return value.toInt();
			return 0;
		}());

	fileDialogLayout->addWidget(storageTypeLabel, rowCount, 0);
	fileDialogLayout->addWidget(storageTypeComboBox, rowCount, 1);


	TRANSFORM::Control transform(fileDialogLayout);



	IfValid(settings.value(Keys::conversionIndexKey), [&transform, &settings](const QVariant& value)
		{
			TRANSFORM::Index index = static_cast<TRANSFORM::Index>(value.toInt());
			if (index == TRANSFORM::ARCSIN5)
			{
				IfValid(settings.value(Keys::transformValueKey), [&transform, index](const QVariant& value)
					{
						TRANSFORM::Type transform_type;
						transform_type.first = index;
						transform_type.second = value.toDouble();
						transform.set(transform_type);
					});
			}
			else
			{
				TRANSFORM::Type type_pair;
				type_pair.first = index;
				type_pair.second = 1.0f;
				transform.set(type_pair);
			}

		});

	QFileDialog& fileDialogRef = _fileDialog;
	IfValid(settings.value(Keys::selectedNameFilterKey), [&fileDialogRef](const QVariant& value)
		{
			fileDialogRef.selectNameFilter(value.toString());
		});
	IfValid(settings.value(Keys::fileNameKey), [&fileDialogRef](const QVariant& value)
		{
			fileDialogRef.selectFile(value.toString());
		});

	const auto onFilterSelected = [ &storageTypeComboBox, &storageTypeLabel, &transform](const QString& nameFilter)
	{
		const bool isTomeSelected{ false};
		const bool isH5ADSelected{ false };

		storageTypeComboBox->setVisible(isH5ADSelected);
		storageTypeLabel->setVisible(isH5ADSelected);

		
		transform.setVisible(!isH5ADSelected);


	};

	QObject::connect(&_fileDialog, &QFileDialog::filterSelected, onFilterSelected);
	onFilterSelected(_fileDialog.selectedNameFilter());
	transform.setTransform(0);
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
		const TRANSFORM::Type transform_setting = transform.get();
		

		settings.setValue(Keys::conversionIndexKey, transform_setting.first);
		settings.setValue(Keys::storageValueKey, storageTypeComboBox->currentIndex());
		settings.setValue(Keys::transformValueKey, transform_setting.second);
		settings.setValue(Keys::fileNameKey, firstFileName);
		settings.setValue(Keys::selectedNameFilterKey, selectedNameFilter);


		for (const auto fileName : fileNames)
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

