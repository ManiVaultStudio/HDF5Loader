#include "HDF5LoaderFactory.h"
#include "HDF5Loader.h"
// =============================================================================
// Factory
// =============================================================================

LoaderPlugin* HDF5LoaderFactory::produce()
{
	return new HDF5Loader();
}
