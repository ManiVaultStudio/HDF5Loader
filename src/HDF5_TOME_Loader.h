#pragma  once
#include <QString>
namespace hdps
{
	class CoreInterface;
}

class PointsPlugin;

class HDF5_TOME_Loader 
{
	hdps::CoreInterface *_core;

public:
	HDF5_TOME_Loader(hdps::CoreInterface *core);

	PointsPlugin *open(const QString &fileName, int conversionIndex, bool normalize);

};