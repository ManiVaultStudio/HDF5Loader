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
public:
	
	H5ADLoader(PluginFactory* factory);
    ~H5ADLoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

private:
	QFileDialog _fileDialog = QFileDialog{};

};
