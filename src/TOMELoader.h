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
public:
    TOMELoader(PluginFactory* factory);
    ~TOMELoader() Q_DECL_OVERRIDE;

    void init() Q_DECL_OVERRIDE;

    void loadData() Q_DECL_OVERRIDE;

private:
	QFileDialog _fileDialog = QFileDialog{};
};
