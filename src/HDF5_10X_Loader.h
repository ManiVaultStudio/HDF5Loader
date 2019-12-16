#pragma  once
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

	Points* open(const QString &fileName, int conversionIndex, int speedIndex);

};