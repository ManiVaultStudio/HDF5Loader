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

mv::DataTypes HDF5LoaderFactory::supportedDataTypes() const
{
	mv::DataTypes supportedTypes;
	return supportedTypes;
}
