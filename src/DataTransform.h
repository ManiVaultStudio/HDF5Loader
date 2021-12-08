#pragma once
#include "QObject"

class QComboBox;
class QLineEdit;
class QLabel;
class QGridLayout;

namespace TRANSFORM
{
	typedef enum { NONE, LOG, SQRT, ARCSIN5} Index;
	typedef std::pair<Index, double> Type;
	inline Type None() { return std::make_pair(NONE, 0.0f); }

	class Control : public QObject
	{
		Q_OBJECT

		QComboBox* m_option;
		QLineEdit* m_value;
		QLabel* m_postLabel;

	private slots:
		void currentOptionChanged(int option);
		void valueChanged();

	public:
		Control(QGridLayout* layout);
		Type get() const;
		void set(Type&);
	};
}


