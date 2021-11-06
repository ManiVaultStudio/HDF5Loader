#pragma once

#include <LoaderPlugin.h>

using namespace hdps::plugin;

// =============================================================================
// Factory
// =============================================================================

class HDF5LoaderFactory : public LoaderPluginFactory
{
	Q_INTERFACES(hdps::plugin::LoaderPluginFactory hdps::plugin::PluginFactory)
	Q_OBJECT
	Q_PLUGIN_METADATA(	IID   "nl.lumc.HDF5Loader"
						FILE  "HDF5Loader.json")

public:
	HDF5LoaderFactory(void) {}
	~HDF5LoaderFactory(void) override {}

	LoaderPlugin* produce() override;

	hdps::DataTypes supportedDataTypes() const override;
};