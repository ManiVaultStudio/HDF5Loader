#pragma once

#include "ClusterSetTableViewAction.h"

#include <actions/HorizontalGroupAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>
#include <actions/VerticalGroupAction.h>

/** @brief Groups cluster-set selection controls and the cluster table vertically. */
class ClusterSetSelectionAction : public mv::gui::VerticalGroupAction
{
    Q_OBJECT

public:
    explicit ClusterSetSelectionAction(QObject* parent);

    /** Rebuilds the table from discovered cluster sets and restores the current selection. */
    void setClusterSets(const QMap<QString, H5ADClusterSetInfo>& clusterSets, const QStringList& selectedClusterSets);

    /** Returns names of cluster sets enabled for import. */
    QStringList selectedClusterSets() const;

signals:
    /** Emitted when a cluster set is enabled or disabled. */
    void selectionChanged();

private:
    void selectAll();
    void selectNone();
    void invertSelection();

    mv::gui::HorizontalGroupAction _toolbarAction; /**< Search and bulk-selection controls. */
    mv::gui::StringAction _searchAction; /**< Search text for filtering cluster-set names. */
    mv::gui::TriggerAction _selectAllAction; /**< Selects every cluster set. */
    mv::gui::TriggerAction _selectNoneAction; /**< Clears every cluster-set selection. */
    mv::gui::TriggerAction _invertSelectionAction; /**< Inverts every cluster-set selection. */
    ClusterSetTableViewAction _tableViewAction; /**< Model-backed selectable cluster-set table. */
};
