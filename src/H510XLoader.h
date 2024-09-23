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
	QFileDialog _fileDialog;

protected:
	
	int _storageType;

public:
	
	H510XLoader(PluginFactory* factory);
    ~H510XLoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

};
