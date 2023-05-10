#pragma once

#include <LoaderPlugin.h>

#include <QObject>
#include <QFileDialog>
using namespace hdps::plugin;

// =============================================================================
// View
// =============================================================================



class HDF5Loader :  public LoaderPlugin
{
	QFileDialog _fileDialog;

	
protected:
	
	int _storageType;
public:
	
	
    HDF5Loader(PluginFactory* factory);
    ~HDF5Loader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

	

};
