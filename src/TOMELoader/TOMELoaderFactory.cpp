#include "TOMELoaderFactory.h"

#include "TOMELoader.h"
#include "DataType.h"

// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* TOMELoaderFactory::produce()
{
	return new TOMELoader(this);
}

mv::DataTypes TOMELoaderFactory::supportedDataTypes() const
{
	mv::DataTypes supportedTypes;
	return supportedTypes;
}
