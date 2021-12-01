
#include "DataTransform.h"
#include <QGridLayout>
#include <QMessageBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
namespace TRANSFORM
{

	void Control::currentOptionChanged(int option)
	{
		m_value->setText(m_option->currentData().toString());
		m_value->setHidden(option != TRANSFORM::ARCSIN5);
		m_postLabel->setHidden(option != TRANSFORM::ARCSIN5);
	}

	void Control::valueChanged()
	{
		if (m_value->hasAcceptableInput())
		{
			bool ok = false;
			const double value = m_value->text().toDouble(&ok);
			if (ok)
			{
				if (m_option->currentIndex() == TRANSFORM::ARCSIN5)
				{
					if (value >= 1 && value <= 100)
					{
						m_option->setItemData(m_option->currentIndex(), value);
						return;
					}
				}
				else
					return;
			}
		}
		QMessageBox::warning(nullptr, "Invalid Value", m_value->toolTip());
		m_value->setText(m_option->currentData().toString());
	}

	Control::Control(QGridLayout* layout)
	{
		assert(layout);
		const int row = layout->rowCount();

		layout->setAlignment(Qt::AlignLeft);
		layout->addWidget(new QLabel("Conversion:"), row, 0);

		QGridLayout* optionLayout = new QGridLayout();
		m_option = new QComboBox();
		m_option->addItem("None",0.0f);
		m_option->addItem("2Log(Value+1)",0.0f);
		m_option->addItem("Sqrt(Value)",0.0f);
		m_option->addItem("Arcsinh(Value/", 5.0f);
		m_option->setItemData(TRANSFORM::ARCSIN5, QVariant(), Qt::SizeHintRole);
		
		m_option->setCurrentIndex(0);
		
		connect(m_option, SIGNAL(currentIndexChanged(int)), this, SLOT(currentOptionChanged(int)));
		optionLayout->addWidget(m_option, 0, 0);

		m_value = new QLineEdit();
		QDoubleValidator* doubleValidator = new QDoubleValidator(m_value);
		QString tooltip("value should be between 1 and 100");

		m_value->setToolTip(tooltip);
		m_value->setText("0.0");
		m_value->setValidator(doubleValidator);
		connect(m_value, &QLineEdit::editingFinished, this, &Control::valueChanged);

		optionLayout->addWidget(m_value, 0, 1);

		m_postLabel = new QLabel(")");
		optionLayout->addWidget(m_postLabel, 0, 2);

		m_value->hide();
		m_postLabel->hide();


		layout->addLayout(optionLayout, row, 1);
	}

	
	
	void Control::set(Type& type_pair)
	{
		m_option->setCurrentIndex(type_pair.first);
		m_option->setItemData(type_pair.first,type_pair.second);
		m_value->setText(QString::number(type_pair.second));
		currentOptionChanged(type_pair.first);
		valueChanged();
	}

	Type Control::get() const
	{
		switch (m_option->currentIndex())
		{
		case TRANSFORM::NONE: return std::make_pair(TRANSFORM::NONE, m_option->currentData().toDouble());
		case TRANSFORM::LOG: return std::make_pair(TRANSFORM::LOG, m_option->currentData().toDouble());
		case TRANSFORM::SQRT: return std::make_pair(TRANSFORM::SQRT, m_option->currentData().toDouble());
		case TRANSFORM::ARCSIN5: return std::make_pair(TRANSFORM::ARCSIN5, m_option->currentData().toDouble());

		default: return std::make_pair(TRANSFORM::NONE, 0.0f);
		}
	}
}
