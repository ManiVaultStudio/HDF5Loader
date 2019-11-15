#pragma  once
#include <QString>
namespace hdps
{
	class CoreInterface;
}

class PointData;

class HDF5_TOME_Loader 
{
	hdps::CoreInterface *_core;

public:
	HDF5_TOME_Loader(hdps::CoreInterface *core);

	PointData *open(const QString &fileName, int conversionIndex, bool normalize);

};