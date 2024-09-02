#pragma  once

#include <QString>
#include "DataTransform.h"

namespace mv
{
	class CoreInterface;
}

class Points;

class HDF5_TOME_Loader 
{
	mv::CoreInterface *_core;

public:
	HDF5_TOME_Loader(mv::CoreInterface *core);

	bool open(const QString &fileName, TRANSFORM::Type conversionIndex, bool normalize);

};