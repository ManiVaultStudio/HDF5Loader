#pragma once

#include "HDF5_AD_Loader.h"

#include <PointData/StorageBackendConfiguratorAction.h>
#include <actions/DirectoryPickerAction.h>
#include <actions/HorizontalGroupAction.h>
#include <actions/OptionAction.h>
#include <actions/OptionsAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>
#include <actions/VerticalGroupAction.h>
#include <actions/WidgetAction.h>
#include <util/SeverityLevel.h>

#include <QDialog>
#include <QPointer>

/** Displays one severity-coded import observation and an optional corrective action. */
class H5ADImportCheckAction : public mv::gui::WidgetAction
{
    Q_OBJECT

public:
    H5ADImportCheckAction(QObject* parent, const QString& category);

    void setObservation(mv::util::SeverityLevel severity, const QString& description, const QString& actionText = {}, const QString& secondaryActionText = {});

signals:
    void actionTriggered(int actionIndex);

protected:
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;

private:
    void updateWidget(QWidget* widget) const;

    mv::util::SeverityLevel _severity = mv::util::SeverityLevel::Info;
    QString _description;
    QStringList _actionTexts;
    QList<QPointer<QWidget>> _widgets;
};

/** Collects H5AD import choices after a file has been selected and opened. */
class H5ADImportDialog : public QDialog
{
public:
    H5ADImportDialog(const QString& fileName, const QString& stagingDirectory, const QString& defaultStagingDirectory, const QStringList& optionalObjects, std::uint64_t rows, std::uint64_t columns, int storageTypeIndex, const QString& backendType, const mv::pointdata::storage::StorageBackendConfiguration& tiledConfiguration, QWidget* parent = nullptr);

    /** Returns the selected import options after the dialog is accepted. */
    H5ADImportOptions options();

    /** Returns the selected index for restoring the data-type control. */
    int storageTypeIndex() const;

    /** Returns the selected backend name for restoring the backend control. */
    QString backendType();

    /** Returns the staging directory selected for this import. */
    QString stagingDirectory() const;

private:
    void updateImportChecks();

    mv::gui::VerticalGroupAction _settingsAction;
    mv::gui::StringAction _fileAction;
    mv::gui::StringAction _matrixAction;
    mv::gui::StringAction _datasetNameAction;
    mv::gui::OptionAction _storageTypeAction;
    StorageBackendConfiguratorAction _backendSelector;
    mv::gui::HorizontalGroupAction _stagingActions;
    mv::gui::DirectoryPickerAction _stagingDirectoryAction;
    QString _defaultStagingDirectory;
    mv::gui::VerticalGroupAction _checksAction;
    H5ADImportCheckAction _memoryCheckAction;
    H5ADImportCheckAction _stagingCheckAction;
    mv::gui::OptionsAction _optionalObjectsAction;
    mv::gui::HorizontalGroupAction _buttonsAction;
    mv::gui::TriggerAction _cancelAction;
    mv::gui::TriggerAction _importAction;
    mv::pointdata::storage::StorageBackendConfiguration _tiledConfiguration;
    std::uint64_t _rows = 0;
    std::uint64_t _columns = 0;
};
