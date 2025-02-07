#pragma once

#include <LoaderPlugin.h>

#include <QObject>
#include <QFileDialog>

using namespace mv::plugin;

// =============================================================================
// Loader
// =============================================================================

class H510XLoader :  public LoaderPlugin
{
public:
	
	H510XLoader(PluginFactory* factory);
    ~H510XLoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

private:
	QFileDialog _fileDialog;
};
