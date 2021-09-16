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
#include <QString>
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

}


void HDF5Loader::loadData()
{
	QSettings settings(QString::fromLatin1("HDPS"), QString::fromLatin1("Plugins/HDF5Loader"));
	QStringList fileTypeOptions;
	fileTypeOptions.append("TOME (*.tome)");
	fileTypeOptions.append("10X (*.h5)");
	fileTypeOptions.append("H5AD (*.h5ad)");
	

	QFileDialog openFileDialog;
	openFileDialog.setOption(QFileDialog::DontUseNativeDialog);
	openFileDialog.setFileMode(QFileDialog::ExistingFile);
	openFileDialog.setNameFilters(fileTypeOptions);

	QGridLayout* openFileDialogLayout = dynamic_cast<QGridLayout*>(openFileDialog.layout());
	int rowCount = openFileDialogLayout->rowCount();


	QCheckBox normalizeCheck("yes");
	QLabel normalizeLabel(QString("Normalize to CPM: "));
	openFileDialogLayout->addWidget(&normalizeLabel, rowCount, 0);
	openFileDialogLayout->addWidget(&normalizeCheck, rowCount++, 1);
	normalizeCheck.setChecked([&settings]
	{
		const auto value = settings.value(Keys::normalizeKey);
		return value.isValid() && value.toBool();
	}());

	QComboBox *const conversionCombo = local::addConversionComboBox(openFileDialogLayout);

	IfValid(settings.value(Keys::conversionIndexKey), [conversionCombo](const QVariant& value)
	{
		conversionCombo->setCurrentIndex(value.toInt());
	});
	IfValid(settings.value(Keys::selectedNameFilterKey), [&openFileDialog](const QVariant& value)
	{
		openFileDialog.selectNameFilter(value.toString());
	});
	IfValid(settings.value(Keys::fileNameKey), [&openFileDialog](const QVariant& value)
	{
		openFileDialog.selectFile(value.toString());
	});

	const auto onFilterSelected = [&normalizeCheck, &normalizeLabel](const QString& nameFilter)
	{
		const bool isTomeSelected{ nameFilter == "TOME (*.tome)" };

		normalizeCheck.setVisible(isTomeSelected);
		normalizeLabel.setVisible(isTomeSelected);
	};

	QObject::connect(&openFileDialog, &QFileDialog::filterSelected, onFilterSelected);
	onFilterSelected(openFileDialog.selectedNameFilter());

	if (openFileDialog.exec())
	{
		QStringList fileNames = openFileDialog.selectedFiles();

		if (fileNames.empty())
		{
			return;
		}
		const QString fileName = fileNames.constFirst();

		Points *result = nullptr;
		QString selectedNameFilter = openFileDialog.selectedNameFilter();
		const auto conversionIndex = conversionCombo->currentIndex();
		const auto normalize = normalizeCheck.isChecked();

		settings.setValue(Keys::conversionIndexKey, conversionIndex);
		settings.setValue(Keys::fileNameKey, fileName);
		settings.setValue(Keys::normalizeKey, normalize);
		settings.setValue(Keys::selectedNameFilterKey, selectedNameFilter);

		if (selectedNameFilter == "TOME (*.tome)")
		{
			HDF5_TOME_Loader loader(_core);
			result = loader.open(fileName, conversionIndex, normalize);
		}
		else if (selectedNameFilter == "10X (*.h5)")
		{
			HDF5_10X_Loader loader(_core);
			result = loader.open(fileName, 0, conversionIndex);
		}
		else if (selectedNameFilter == "H5AD (*.h5ad)")
		{
			HDF5_AD_Loader loader(_core);
			result = loader.open(fileName);
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

