#pragma  once

#include "Dataset.h"

#include <QString>

namespace hdps
{
	class CoreInterface;
}

class Points;

class HDF5_10X_Loader 
{
	hdps::CoreInterface *_core;

public:
	HDF5_10X_Loader(hdps::CoreInterface *core);

	hdps::Dataset<Points> open(const QString &fileName, int conversionIndex, int speedIndex);

};