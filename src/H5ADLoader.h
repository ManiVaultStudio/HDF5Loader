#pragma once

#include <LoaderPlugin.h>

#include <QObject>
#include <QFileDialog>

using namespace mv::plugin;

// =============================================================================
// Loader
// =============================================================================

class H5ADLoader :  public LoaderPlugin
{
	QFileDialog _fileDialog;
	
protected:
	int _storageType;

public:
	
	H5ADLoader(PluginFactory* factory);
    ~H5ADLoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

};
