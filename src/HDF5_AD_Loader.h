#pragma  once
#include <QString>
#include <memory>
#include <vector>
#include "H5Utils.h"
namespace hdps
{
	class CoreInterface;
}


class Points;


class HDF5_AD_Loader 
{
	hdps::CoreInterface *_core;
	std::unique_ptr<H5::H5File> _file;
	std::vector<QString> _dimensionNames;
	std::vector<QString> _sampleNames;
	QString _fileName;
public:
	HDF5_AD_Loader(hdps::CoreInterface *core);
	
	bool open(const QString &);
	const std::vector<QString> &getDimensionNames() const;
	bool load();
	
};