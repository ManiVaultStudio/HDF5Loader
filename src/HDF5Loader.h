#pragma once

#include <LoaderPlugin.h>

#include <QObject>

using namespace hdps::plugin;

// =============================================================================
// View
// =============================================================================


class HDF5Loader : public QObject, public LoaderPlugin
{
public:
    HDF5Loader();
    ~HDF5Loader(void) override;
    
    void init() override;

    void loadData() Q_DECL_OVERRIDE;

};
