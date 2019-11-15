#pragma  once

#include <QString>

namespace hdps
{
	class CoreInterface;
}
class PointData;


class HDF5_AD_Loader 
{

	

	hdps::CoreInterface *_core;

public:
	HDF5_AD_Loader(hdps::CoreInterface *core);

	PointData* open(const QString &filename);

};