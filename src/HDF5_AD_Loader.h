#pragma  once

#include <QString>

namespace hdps
{
	class CoreInterface;
}
class Points;


class HDF5_AD_Loader 
{

	

	hdps::CoreInterface *_core;

public:
	HDF5_AD_Loader(hdps::CoreInterface *core);

	Points* open(const QString &filename);

};