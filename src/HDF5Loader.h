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

    /** Returns the icon of this plugin */
    QIcon getIcon() const override {
        return hdps::Application::getIconFont("FontAwesome").getIcon("sitemap");
    }

    void init() override;

    void loadData() Q_DECL_OVERRIDE;

};
