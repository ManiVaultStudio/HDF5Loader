#include "HDF5LoaderFactory.h"
#include "HDF5Loader.h"
#include "DataType.h"
// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* HDF5LoaderFactory::produce()
{
	return new HDF5Loader(this);
}

hdps::DataTypes HDF5LoaderFactory::supportedDataTypes() const
{
	hdps::DataTypes supportedTypes;
	return supportedTypes;
}
