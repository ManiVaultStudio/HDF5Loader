#pragma once

#include <LoaderPlugin.h>

#include <QObject>
#include <QFileDialog>
using namespace hdps::plugin;

// =============================================================================
// View
// =============================================================================



class HDF5Loader : public LoaderPlugin
{
	QFileDialog _fileDialog;
	
public:
    HDF5Loader(PluginFactory* factory);
    ~HDF5Loader() override;

    void init() override;

    void loadData() Q_DECL_OVERRIDE;

};
