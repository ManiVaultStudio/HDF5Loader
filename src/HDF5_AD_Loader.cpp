#include "HDF5_AD_Loader.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>

#include "H5Utils.h"
#include "DataContainerInterface.h"
#include <iostream>

using namespace hdps;

namespace H5AD
{


	inline const std::string QColor_to_stdString(const QColor &color)
	{
		return color.name().toUpper().toStdString();
	}


	bool is_number(const std::string& s)
	{
		char *end = nullptr;
		strtod(s.c_str(), &end);
		return *end == '\0';
	}

	struct compareWithNumbers {
		bool operator()(const std::string& a, const std::string& b) const
		{
			try
			{
				if (is_number(a) && is_number(b))
				{
					double d_a = strtod(a.c_str(), nullptr);
					double d_b = strtod(b.c_str(), nullptr);
					return d_a < d_b;
				}
				return a < b;
			}
			catch (...)
			{

			}
		}
	};


	void LoadData(const H5::DataSet &dataset, PointData *pointsPlugin, TRANSFORM::Type transformType, bool normalize_and_cpm)
	{
		H5Utils::MultiDimensionalData<float> data;

		if (H5Utils::read_multi_dimensional_data(dataset, data, H5::PredType::NATIVE_FLOAT))
		{
			if (data.size.size() == 2)
			{
				pointsPlugin->setData(data.data.data(), data.size[0], data.size[1]);
			}
		}
	}


	void LoadGeneNames(H5::DataSet &dataset, PointData *pointsPlugin)
	{
		std::map<std::string, std::vector<QVariant> > compoundMap;
		if (H5Utils::read_compound(dataset, compoundMap))
		{

			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				if (component->first == "index")
				{
					std::size_t nrOfItems = component->second.size();
					std::vector<QString> dimensionNames(nrOfItems);
					for (std::size_t i = 0; i < nrOfItems; ++i)
						dimensionNames[i] = component->second[i].toString();
					pointsPlugin->setDimensionNames(dimensionNames);
				}
			}
		}
	}

	void LoadSampleNamesAndMetaData(H5::DataSet dataset, PointData *pointsPlugin)
	{
#ifndef HIDE_CONSOLE
		std::cout << "Loading MetaData" << std::endl; // and sample names
#endif


		const int numFiles = 1; // assuming only 1 file for now
		const int fileID = 0;

	
		std::map<std::string, std::vector<QVariant> > compoundMap;
		if (H5Utils::read_compound(dataset, compoundMap))
		{
			std::size_t nrOfComponents = compoundMap.size();
			

			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				const std::size_t nrOfSamples = component->second.size();
				if (component->first == "index" && (pointsPlugin != nullptr))
				{
					QList<QVariant> list;
					for (auto i = 0; i < nrOfSamples; ++i)
					{
						list.append(component->second[i]);
					}
					pointsPlugin->setProperty("Sample Names", list);
				}
				else // add as metadata
				{

					std::map<std::string, std::string, H5AD::compareWithNumbers> valueColorMap;
					bool allNumericalValues = true;
					double minValue = std::numeric_limits<double>::max();
					double maxValue = std::numeric_limits<double>::lowest();
					for (std::size_t i = 0; i < nrOfSamples; ++i)
					{
						bool ok = false;
						const double numericalValue = component->second[i].toDouble(&ok);
						allNumericalValues &= ok;
						if (numericalValue < minValue)
							minValue = numericalValue;
						if (numericalValue > maxValue)
							maxValue = numericalValue;
						valueColorMap[component->second[i].toString().toStdString()] = "";
					}
					std::size_t nrOfColors = valueColorMap.size();
					double range = 0;
					if (allNumericalValues)
						range = maxValue - minValue;
					bool assumeIndex = false;
					if (minValue == 0 && maxValue == (nrOfColors - 1))
					{
						assumeIndex = true;
						range += 1;
					}
					if (minValue == 1 && maxValue == nrOfColors)
					{
						assumeIndex = true;
						range += 1;
					}
					if (!assumeIndex)
						range *= 1.25;// to prevent white

					if (nrOfColors)
					{
						std::size_t index = 0;
						for (auto it = valueColorMap.begin(); it != valueColorMap.end(); ++it, ++index)
						{
							if (allNumericalValues && (minValue != maxValue))
							{
								const double value = atof(it->first.c_str());
								const float relativeValue = std::min<double>(1.0f, std::max<double>(0.0f, (value - minValue) / (range)));
								if (assumeIndex)
								{
									QColor hsvColor = QColor::fromHsvF(relativeValue, 0.5f, 1.0f);
									it->second = H5AD::QColor_to_stdString(hsvColor);
								}
								else
								{
									QColor hsvColor = QColor::fromHsvF(0.0f, 0.0f, relativeValue); /*GetViridisColor(relativeValue);*/
									it->second = H5AD::QColor_to_stdString(hsvColor);
								}
							}
							else
							{
								float h = std::min<float>((1.0*index / (nrOfColors + 1)), 1.0f);
								if (h > 1 || h < 0)
								{
									int bp = 0;
									bp++;
								}
								QColor hsvColor = QColor::fromHsvF(h, 0.5f, 1.0f);
								it->second = H5AD::QColor_to_stdString(hsvColor);
							}
						}

						QList<QVariant> valueList;
						QList<QVariant> colorList;
						QString colorLabel= QString::fromStdString(component->first + "_color");
						for (std::size_t i = 0; i < nrOfSamples; ++i)
						{
							std::string valueString = component->second[i].toString().toStdString();
							valueList.append(component->second[i]);
							colorList.append(valueColorMap[valueString].c_str());
						}
						pointsPlugin->setProperty(component->first.c_str(), valueList);
						pointsPlugin->setProperty(colorLabel, colorList);
					}
				}
			}
		}
	}




}// namespace

HDF5_AD_Loader::HDF5_AD_Loader(hdps::CoreInterface *core)
{
	_core = core;
}

PointData* HDF5_AD_Loader::open(const QString &fileName)
{
	QGuiApplication::setOverrideCursor(Qt::WaitCursor);

	try
	{
		PointData *pointData = nullptr;
		bool ok;
		QString dataSetName = QInputDialog::getText(nullptr, "Add New Dataset",
			"Dataset name:", QLineEdit::Normal, "DataSet", &ok);

		if (ok && !dataSetName.isEmpty()) {
			QString name = _core->addData("Points", dataSetName);
			const IndexSet& set = _core->requestSet<IndexSet>(name);
			pointData = &(set.getData<PointData>());
		}
		if (pointData == nullptr)
			return nullptr;
		

		H5::H5File file(fileName.toLatin1().constData(), H5F_ACC_RDONLY);

		auto nrOfObjects = file.getNumObjs();

		// first do the basics
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = file.getObjnameByIdx(fo);

			H5G_obj_t objectType1 = file.getObjTypeByIdx(fo);
			if (objectType1 == H5G_DATASET)
			{
				if (objectName1 == "X")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					H5AD::LoadData(dataset, pointData, 0, false);
				}
				else if (objectName1 == "var")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					H5AD::LoadGeneNames(dataset, pointData);
				}
				else if (objectName1 == "obs")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					H5AD::LoadSampleNamesAndMetaData(dataset, pointData);
				}
			}
		}
		// now we look for nice to have data
		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = file.getObjnameByIdx(fo);

			H5G_obj_t objectType1 = file.getObjTypeByIdx(fo);
			if (objectType1 == H5G_DATASET)
			{
				if (objectName1 == "obsm")
				{
					H5::DataSet dataset = file.openDataSet(objectName1);
					H5AD::LoadSampleNamesAndMetaData(dataset, pointData);
				}
			}
		}

		return pointData;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return nullptr;
	}

	QGuiApplication::restoreOverrideCursor();
}
	

