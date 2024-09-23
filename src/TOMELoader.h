#pragma once

#include <LoaderPlugin.h>

#include <QObject>

#include <QFileDialog>
using namespace mv::plugin;

// =============================================================================
// Loader
// =============================================================================

class TOMELoader :  public LoaderPlugin
{
	QFileDialog _fileDialog;

protected:
	int _storageType;

public:
    TOMELoader(PluginFactory* factory);
    ~TOMELoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

};
