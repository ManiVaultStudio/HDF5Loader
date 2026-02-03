#pragma once

#include <LoaderPlugin.h>

using namespace mv::plugin;

// =============================================================================
// Factory
// =============================================================================

class TOMELoaderFactory : public LoaderPluginFactory
{
	Q_INTERFACES(mv::plugin::LoaderPluginFactory mv::plugin::PluginFactory)
	Q_OBJECT
	Q_PLUGIN_METADATA(	IID   "nl.lumc.TOMELoader"
						FILE  "PluginInfo.json")

public:
	TOMELoaderFactory(void) {}
	~TOMELoaderFactory(void) override {}

	LoaderPlugin* produce() override;

	mv::DataTypes supportedDataTypes() const override;
};
