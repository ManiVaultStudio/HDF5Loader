#include "HDF5Loader.h"

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
#include <QStringList>
#include <QSettings>
#include "PointData.h"

Q_PLUGIN_METADATA(IID "nl.lumc.HDF5Loader")

// =============================================================================
// Loader

namespace
{
	// Alphabetic list of keys used to access settings from QSettings.
	namespace Keys
	{
		const QLatin1String conversionIndexKey("conversionIndex");
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

namespace local 
{
	
	enum {H5TOME, H510X , H5AD};
	QComboBox * addConversionComboBox(QGridLayout *fileDialogLayout)
	{
		QComboBox *conversionComboBox = new QComboBox();
		conversionComboBox->addItem("None");
		conversionComboBox->addItem("2Log(Value+1)");
		conversionComboBox->addItem("Sqrt(Value)");
		conversionComboBox->addItem("Arcsinh(Value/5)");
		int rowCount = fileDialogLayout->rowCount();
		fileDialogLayout->addWidget(new QLabel(QString("Conversion: ")), rowCount, 0);
		fileDialogLayout->addWidget(conversionComboBox, rowCount++, 1);
		return conversionComboBox;
	}
}



HDF5Loader::HDF5Loader()
    : QObject()
	, LoaderPlugin("HDF5 Loader")
    , _fileName()
	,_openFileDialog(nullptr)
{

}

HDF5Loader::~HDF5Loader(void)
{
    
}

void HDF5Loader::init()
{

}


void HDF5Loader::nameFilterChanged(int index)
{
	QGridLayout* openFileDialogLayout = dynamic_cast<QGridLayout*>(_openFileDialog->layout());
	switch (index)
	{
		case local::H5TOME:
		{

			_normalizeCheck->setVisible(true);
			_normalizeLabel->setVisible(true);
			
			break;
		}
		case local::H510X:
		{
			_normalizeCheck->setVisible(false);
			_normalizeLabel->setVisible(false);
			
			break;
		}
		case local::H5AD:
		{
			_normalizeCheck->setVisible(false);
			_normalizeLabel->setVisible(false);
			break;
		}
	}
}


void HDF5Loader::nameFilterChanged(const QString &current)
{
	if (current == "TOME (*.tome)")
	{
		nameFilterChanged(local::H5TOME);
		return;
	}
	if (current == "10X (*.h5)")
	{
		nameFilterChanged(local::H510X);
		return;
	}
	if (current == "H5AD (*.h5ad)")
	{
		nameFilterChanged(local::H5AD);
		return;
	}
	
}

void HDF5Loader::loadData()
{
	QSettings settings(QString::fromLatin1("HDPS"), QString::fromLatin1("Plugins/HDF5Loader"));
	QStringList fileTypeOptions;
	fileTypeOptions.append("TOME (*.tome)");
	fileTypeOptions.append("10X (*.h5)");
	fileTypeOptions.append("H5AD (*.h5ad)");
	

	_openFileDialog = new QFileDialog();
	_openFileDialog->setOption(QFileDialog::DontUseNativeDialog);
	_openFileDialog->setFileMode(QFileDialog::ExistingFile);
	_openFileDialog->setNameFilters(fileTypeOptions);

	QGridLayout* openFileDialogLayout = dynamic_cast<QGridLayout*>(_openFileDialog->layout());
	int rowCount = openFileDialogLayout->rowCount();


	_normalizeCheck = new QCheckBox("yes");
	_normalizeLabel = new QLabel(QString("Normalize to CPM: "));
	openFileDialogLayout->addWidget(_normalizeLabel, rowCount, 0);
	openFileDialogLayout->addWidget(_normalizeCheck, rowCount++, 1);
	_normalizeCheck->setChecked([&settings]
	{
		const auto value = settings.value(Keys::normalizeKey);
		return value.isValid() && value.toBool();
	}());

	_conversionCombo = local::addConversionComboBox(openFileDialogLayout);

	IfValid(settings.value(Keys::conversionIndexKey), [this](const QVariant& value)
	{
		_conversionCombo->setCurrentIndex(value.toInt());
	});
	IfValid(settings.value(Keys::selectedNameFilterKey), [this](const QVariant& value)
	{
		_openFileDialog->selectNameFilter(value.toString());
	});
	IfValid(settings.value(Keys::fileNameKey), [this](const QVariant& value)
	{
		_openFileDialog->selectFile(value.toString());
	});

	connect(_openFileDialog, SIGNAL(filterSelected(const QString&)), this, SLOT(nameFilterChanged(const QString&)));
	nameFilterChanged(_openFileDialog->selectedNameFilter());


	if (_openFileDialog->exec())
	{
		QStringList fileNames = _openFileDialog->selectedFiles();

		if (fileNames.size())
			_fileName = fileNames[0];
		else 
			return;

		Points *result = nullptr;
		QString selectedNameFilter = _openFileDialog->selectedNameFilter();
		const auto conversionIndex = _conversionCombo->currentIndex();
		const auto normalize = _normalizeCheck->isChecked();

		settings.setValue(Keys::conversionIndexKey, conversionIndex);
		settings.setValue(Keys::fileNameKey, _fileName);
		settings.setValue(Keys::normalizeKey, normalize);
		settings.setValue(Keys::selectedNameFilterKey, selectedNameFilter);

		if (selectedNameFilter == "TOME (*.tome)")
		{
			HDF5_TOME_Loader loader(_core);
			result = loader.open(_fileName, conversionIndex, normalize);
		}
		else if (selectedNameFilter == "10X (*.h5)")
		{
			HDF5_10X_Loader loader(_core);
			result = loader.open(_fileName, 0, conversionIndex);
		}
		else if (selectedNameFilter == "H5AD (*.h5ad)")
		{
			HDF5_AD_Loader loader(_core);
			result = loader.open(_fileName);
		}
// 
// #ifdef _DEBUG
// 		if (result)
// 		{
// 			std::int64_t numDimensions = result->getNumDimensions();
// 			std::uint64_t numPoints = result->getNumPoints();
// 			std::vector<double> sum(result->getNumDimensions());
// 			#pragma  omp parallel for
// 			for (std::int64_t i = 0; i < numDimensions; ++i)
// 			{
// 				for (std::uint64_t j = 0; j < numPoints; ++j)
// 					sum[i] += (*result)[(j*numDimensions) + i];
// 
// 			}
// 			QMap<QString, double> x;
// 			for (std::int64_t i = 0; i < numDimensions; ++i)
// 			{
// 				x[result->getDimensionNames()[i]] = sum[i] / numPoints;
// 			}
// 
// 			for (auto it : x.toStdMap())
// 			{
// 				qInfo() << it.first << " " << it.second;
// 			}
// 		}
// #endif
	}

}

