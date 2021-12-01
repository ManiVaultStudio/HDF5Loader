#pragma  once
#include <QString>
#include "DataTransform.h"
namespace hdps
{
	class CoreInterface;
}

class Points;

class HDF5_TOME_Loader 
{
	hdps::CoreInterface *_core;

public:
	HDF5_TOME_Loader(hdps::CoreInterface *core);

	bool open(const QString &fileName, TRANSFORM::Type conversionIndex, bool normalize);

};