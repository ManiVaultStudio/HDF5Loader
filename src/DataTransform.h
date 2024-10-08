#pragma once

#include <QObject>

#include <utility>

class QComboBox;
class QLineEdit;
class QLabel;
class QGridLayout;

//#define USE_HDF5_TRANSFORM

namespace TRANSFORM
{
	typedef enum { NONE, LOG, SQRT, ARCSIN5} Index;
	typedef std::pair<Index, double> Type;
	inline Type None() { return std::make_pair(NONE, 0.0f); }

	class Control : public QObject
	{
		Q_OBJECT

		QComboBox* m_option = nullptr;
		QLineEdit* m_value = nullptr;
		QLabel* m_postLabel = nullptr;

	private slots:
		void currentOptionChanged(int option);
		void valueChanged();

	public:
		Control(QGridLayout* layout);
		Type get() const;
		void set(Type&);
		void setTransform(int);
		void setVisible(bool);
	};
}


