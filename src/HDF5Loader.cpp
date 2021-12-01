#include "HDF5Loader.h"

#include "DataTransform.h"
#include "HDF5_TOME_Loader.h"
#include "HDF5_10X_Loader.h"
#include "HDF5_AD_Loader.h"
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
#include "PointData.h"
#include "QMessageBox"


Q_PLUGIN_METADATA(IID "nl.lumc.HDF5Loader")

// =============================================================================
// Loader

namespace
{
	// Alphabetic list of keys used to access settings from QSettings.
	namespace Keys
	{
		const QLatin1String conversionIndexKey("conversionIndex");
		const QLatin1String transformValueKey("transformValue");
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



HDF5Loader::HDF5Loader(PluginFactory* factory)
    : QObject()
	, LoaderPlugin(factory)
{

}

HDF5Loader::~HDF5Loader(void)
{
    
}


void HDF5Loader::init()
{
	
	QStringList fileTypeOptions;
	fileTypeOptions.append("TOME (*.tome)");
	fileTypeOptions.append("10X (*.h5)");
	fileTypeOptions.append("H5AD (*.h5ad)");
	_fileDialog.setOption(QFileDialog::DontUseNativeDialog);
	_fileDialog.setFileMode(QFileDialog::ExistingFiles);
	_fileDialog.setNameFilters(fileTypeOptions);

	
}


void HDF5Loader::loadData()
{
	QSettings settings(QString::fromLatin1("HDPS"), QString::fromLatin1("Plugins/HDF5Loader"));
	QGridLayout* fileDialogLayout = dynamic_cast<QGridLayout*>(_fileDialog.layout());
	TRANSFORM::Control transform(fileDialogLayout);

	int rowCount = fileDialogLayout->rowCount();
	QCheckBox normalizeCheck("yes");
	QLabel normalizeLabel(QString("Normalize to CPM: "));
	fileDialogLayout->addWidget(&normalizeLabel, rowCount, 0);
	fileDialogLayout->addWidget(&normalizeCheck, rowCount++, 1);
	normalizeCheck.setChecked([&settings]
	{
		const auto value = settings.value(Keys::normalizeKey);
		return value.isValid() && value.toBool();
	}());
	

	IfValid(settings.value(Keys::conversionIndexKey), [&transform, &settings](const QVariant& value)
	{
		TRANSFORM::Index index = static_cast<TRANSFORM::Index>(value.toInt());
		if(index == TRANSFORM::ARCSIN5)
		{
			IfValid(settings.value(Keys::transformValueKey), [&transform, index](const QVariant& value)
				{

					transform.set(std::make_pair(index, value.toDouble()));
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

	const auto onFilterSelected = [&normalizeCheck, &normalizeLabel](const QString& nameFilter)
	{
		const bool isTomeSelected{ nameFilter == "TOME (*.tome)" };

		normalizeCheck.setVisible(isTomeSelected);
		normalizeLabel.setVisible(isTomeSelected);
	};

	QObject::connect(&_fileDialog, &QFileDialog::filterSelected, onFilterSelected);
	onFilterSelected(_fileDialog.selectedNameFilter());

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
		const bool normalize = normalizeCheck.isChecked();

		settings.setValue(Keys::conversionIndexKey, transform_setting.first);
		settings.setValue(Keys::transformValueKey, transform_setting.second);
		settings.setValue(Keys::fileNameKey, firstFileName);
		settings.setValue(Keys::normalizeKey, normalize);
		settings.setValue(Keys::selectedNameFilterKey, selectedNameFilter);

		if (selectedNameFilter == "TOME (*.tome)")
		{
			for (const auto fileName : fileNames)
			{
				HDF5_TOME_Loader loader(_core);
				loader.open(firstFileName, transform_setting, normalize);
			}
		}
		else if (selectedNameFilter == "10X (*.h5)")
		{
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
		else if (selectedNameFilter == "H5AD (*.h5ad)")
		{
			HDF5_AD_Loader loader(_core);
			for (const auto fileName : fileNames)
			{
				if (loader.open(fileName))
					loader.load();
				else
				{
					QString mesg = "Could not open " + fileName + ". Make sure the file has the correct file extension and is not corrupted.";
					QMessageBox::critical(nullptr, "Error loading file(s)", mesg);
				}
			}
		
		}
	}
}

