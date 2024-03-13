#include "H5ADLoaderFactory.h"
#include "HDF5Loader.h"
#include "DataType.h"
// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* H5ADLoaderFactory::produce()
{
	return new HDF5Loader(this);
}

mv::DataTypes H5ADLoaderFactory::supportedDataTypes() const
{
	mv::DataTypes supportedTypes;
	return supportedTypes;
}
