#pragma once

#include <LoaderPlugin.h>

#include <QString>

using namespace hdps::plugin;

// =============================================================================
// View
// =============================================================================


class QFileDialog;
class QComboBox;
class QCheckBox;

class QLabel;

class HDF5Loader : public QObject, public LoaderPlugin
{
	Q_OBJECT
public:
    HDF5Loader();
    ~HDF5Loader(void) override;
    
    void init() override;

    void loadData() Q_DECL_OVERRIDE;



private slots:
	void nameFilterChanged(int);
	void nameFilterChanged(const QString &);
private:
    QString _fileName;
	QFileDialog *_openFileDialog;
	QComboBox *_conversionCombo;
	QCheckBox *_normalizeCheck;
	QLabel *_normalizeLabel;
	
};
