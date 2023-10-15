#pragma once

#include <LoaderPlugin.h>

using namespace mv::plugin;

// =============================================================================
// Factory
// =============================================================================

class HDF5LoaderFactory : public LoaderPluginFactory
{
	Q_INTERFACES(mv::plugin::LoaderPluginFactory mv::plugin::PluginFactory)
	Q_OBJECT
	Q_PLUGIN_METADATA(	IID   "nl.lumc.HDF5Loader"
						FILE  "HDF5Loader.json")

public:
	HDF5LoaderFactory(void) {}
	~HDF5LoaderFactory(void) override {}

	LoaderPlugin* produce() override;

	mv::DataTypes supportedDataTypes() const override;
};