#pragma  once

#include <QString>

#include <memory>
#include <string>
#include <vector>

#include "H5Utils.h"

namespace mv
{
	class CoreInterface;
}

class HDF5_AD_Loader 
{
public:
	HDF5_AD_Loader(mv::CoreInterface *core);
	
	bool open(const QString &);
	const std::vector<QString> &getDimensionNames() const;
	bool load(int storageType);
	
private:
	mv::CoreInterface* _core = nullptr;
	std::unique_ptr<H5::H5File> _file = nullptr;
	std::vector<QString> _dimensionNames = {};
	std::vector<QString> _sampleNames = {};

	QString _fileName = {};

	std::string _var_indexName = {};
	std::string _obs_indexName = {};
};