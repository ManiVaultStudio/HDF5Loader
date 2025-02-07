#include "TOMELoader.h"

#include "PointData/PointData.h"

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
#include <QMessageBox>

Q_PLUGIN_METADATA(IID "nl.lumc.TOMELoader")

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
		const QString conversionIndexKey("conversionIndex");
		const QString transformValueKey("transformValue");
		const QString fileNameKey("fileName");
		const QString normalizeKey("normalize");
		const QString selectedNameFilterKey("selectedNameFilter");
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

#ifdef USE_HDF5_TRANSFORM
	QCheckBox normalizeCheck("yes");
	QLabel normalizeLabel(QString("Normalize to CPM: "));
	fileDialogLayout->addWidget(&normalizeLabel, rowCount, 0);
	fileDialogLayout->addWidget(&normalizeCheck, rowCount++, 1);
	normalizeCheck.setChecked([&settings]
	{
		const auto value = settings.value(Keys::normalizeKey);
		return value.isValid() && value.toBool();
	}());
#endif	

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

	transform.setVisible(true);

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
#ifdef USE_HDF5_TRANSFORM
		const bool normalize = normalizeCheck.isChecked();
#else
		const bool normalize = false;
#endif

		setSetting(Keys::conversionIndexKey, transform_setting.first);
		setSetting(Keys::transformValueKey, transform_setting.second);
		setSetting(Keys::fileNameKey, firstFileName);
		setSetting(Keys::normalizeKey, normalize);
		setSetting(Keys::selectedNameFilterKey, selectedNameFilter);
		
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

