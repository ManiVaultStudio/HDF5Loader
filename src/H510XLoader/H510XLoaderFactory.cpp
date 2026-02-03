#include "H510XLoaderFactory.h"

#include "H510XLoader.h"
#include "DataType.h"

// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* H510XLoaderFactory::produce()
{
	return new H510XLoader(this);
}

mv::DataTypes H510XLoaderFactory::supportedDataTypes() const
{
	mv::DataTypes supportedTypes;
	return supportedTypes;
}
