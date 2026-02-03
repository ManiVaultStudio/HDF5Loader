#pragma once

#include <LoaderPlugin.h>

using namespace mv::plugin;

// =============================================================================
// Factory
// =============================================================================

class H5ADLoaderFactory : public LoaderPluginFactory
{
	Q_INTERFACES(mv::plugin::LoaderPluginFactory mv::plugin::PluginFactory)
	Q_OBJECT
	Q_PLUGIN_METADATA(	IID   "nl.lumc.H5ADLoaderFactory"
						FILE  "PluginInfo.json")

public:
	H5ADLoaderFactory(void) {}
	~H5ADLoaderFactory(void) override {}

	LoaderPlugin* produce() override;

	mv::DataTypes supportedDataTypes() const override;
};