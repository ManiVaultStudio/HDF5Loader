#include "TOMELoader.h"

#include "DataTransform.h"
#include "HDF5_TOME_Loader.h"
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


Q_PLUGIN_METADATA(IID "nl.lumc.TOMELoader")

// =============================================================================
// Loader

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



TOMELoader::TOMELoader(PluginFactory* factory)
    : LoaderPlugin(factory)
	
{
	
}

TOMELoader::~TOMELoader(void)
{
	
}


void TOMELoader::init()
{
	
	QStringList fileTypeOptions;
	fileTypeOptions.append("TOME (*.tome)");
	_fileDialog.setOption(QFileDialog::DontUseNativeDialog);
	_fileDialog.setFileMode(QFileDialog::ExistingFiles);
	_fileDialog.setNameFilters(fileTypeOptions);

	

	
}


void TOMELoader::loadData()
{
	if (Hdf5Lock())
	{
		QMessageBox::information(nullptr, "TOME Loader is busy", "The TOME Loader is already loading a file. You cannot load another file until loading has completed.");
		return;
	};
	LockGuard lockGuard(Hdf5Lock());
	
	QSettings settings(QString::fromLatin1("HDPS"), QString::fromLatin1("Plugins/TOMELoader"));
	QGridLayout* fileDialogLayout = dynamic_cast<QGridLayout*>(_fileDialog.layout());

	int rowCount = fileDialogLayout->rowCount();

	
	

	TRANSFORM::Control transform(fileDialogLayout);

	
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

	const auto onFilterSelected = [&normalizeCheck, &normalizeLabel, &transform](const QString& nameFilter)
	{
		const bool isTomeSelected{ true };
		

		normalizeCheck.setVisible(isTomeSelected);
		normalizeLabel.setVisible(isTomeSelected);
		

		
		
		
		transform.setVisible(true);

		
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
		
	}
}

