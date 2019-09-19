#pragma  once
#include <QString>

namespace hdps
{
	class CoreInterface;
}

class PointsPlugin;

class HDF5_10X_Loader 
{

	

	hdps::CoreInterface *_core;

public:
	HDF5_10X_Loader(hdps::CoreInterface *core);

	PointsPlugin* open(const QString &fileName, int conversionIndex, int speedIndex);

};