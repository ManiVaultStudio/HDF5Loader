#pragma once

#include <LoaderPlugin.h>

using namespace mv::plugin;

// =============================================================================
// Factory
// =============================================================================

class H510XLoaderFactory : public LoaderPluginFactory
{
	Q_INTERFACES(mv::plugin::LoaderPluginFactory mv::plugin::PluginFactory)
	Q_OBJECT
	Q_PLUGIN_METADATA(	IID   "nl.lumc.H510XLoaderFactory"
						FILE  "H510XLoader.json")

public:
	H510XLoaderFactory(void) {}
	~H510XLoaderFactory(void) override {}

	LoaderPlugin* produce() override;

	mv::DataTypes supportedDataTypes() const override;
};