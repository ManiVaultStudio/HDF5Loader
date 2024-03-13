#include "H5ADLoaderFactory.h"
#include "H5ADLoader.h"
#include "DataType.h"
// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* H5ADLoaderFactory::produce()
{
	return new H5ADLoader(this);
}

mv::DataTypes H5ADLoaderFactory::supportedDataTypes() const
{
	mv::DataTypes supportedTypes;
	return supportedTypes;
}
