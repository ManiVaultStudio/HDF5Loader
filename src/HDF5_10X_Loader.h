#pragma  once

#include "H5Utils.h"
#include "DataTransform.h"

#include "Dataset.h"

#include <QString>

#include <memory>

 namespace mv
{
	class CoreInterface;
}

class Points;

class HDF5_10X_Loader 
{
	mv::CoreInterface *_core;
	std::unique_ptr<H5::H5File> _file;
	std::vector<QString> _dimensionNames;
	std::vector<QString> _sampleNames;
	QString _fileName;

public:
	HDF5_10X_Loader(mv::CoreInterface *core);

	bool open(const QString& fileName);
	const std::vector<QString>& getDimensionNames() const;
	bool load(TRANSFORM::Type conversionIndex, int speedIndex);

};
