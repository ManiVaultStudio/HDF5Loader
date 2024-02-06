#include "DataContainerInterface.h"
#include <vector>
#include <cmath>
#include <cassert>
#include <numeric>
#include <algorithm>
#include <DataHierarchyItem.h>
#include <QInputDialog>
#include <QDebug>
#include "Plugin.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QString>
#include <stdexcept> // For std::out_of_range.
#include <iostream>
#include <QBitArray>
#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace mv;

namespace local
{
	class Progress
	{
		mv::DataHierarchyItem& dataHierarcyItem;

	public:
		Progress(mv::DataHierarchyItem& item, const QString& taskName, std::size_t nrOfSteps)
			:dataHierarcyItem(item)
		{
			auto& task = dataHierarcyItem.getDataset()->getTask();
			
			task.setName(taskName);
			task.setProgressDescription(taskName);
			task.setRunning();
			task.setSubtasks(nrOfSteps);
		}

		void setStep(std::size_t step)
		{
#if defined(_OPENMP)
			if(omp_get_thread_num() == 0)
				dataHierarcyItem.getDataset()->getTask().setSubtaskFinished(step);
#endif
		}

		~Progress()
		{
			dataHierarcyItem.getDataset()->getTask().setFinished();
		}
	};

	template<typename T1, typename T2, typename T3>
	void set_sparse_row_data_impl(Dataset<Points> m_data, std::vector<T1>& column_index, std::vector<T2>& row_offset, std::vector<T3>& data, TRANSFORM::Type transformType)
	{
		long long lrows = ((long long)m_data->getNumPoints());
		auto columns = m_data->getNumDimensions();

		local::Progress progress(m_data->getDataHierarchyItem(), "Loading Data", lrows);
		m_data->visitFromBeginToEnd([&column_index, &row_offset, &data, transformType, lrows, columns, &progress](const auto beginOfData, const auto endOfData)
			{
				#pragma omp parallel for schedule(dynamic,1)
				for (long long row = 0; row < lrows; ++row)
				{
					uint64_t start = row_offset[row];
					uint64_t end = row_offset[row + 1];
					uint64_t points_offset = row * columns;

					for (uint64_t i = start; i < end; ++i)
					{
						
						uint64_t column = column_index[i];
						if (column < columns)
						{
							double value = data[i];
							if (value != 0)
							{
								switch (transformType.first)
								{
								case TRANSFORM::NONE: beginOfData[points_offset + column] = value; break;
								case TRANSFORM::LOG:  beginOfData[points_offset + column] = log2(1 + value); break;
								case TRANSFORM::ARCSIN5: beginOfData[points_offset + column] = asinh(value / 5.0); break;
								case TRANSFORM::SQRT: beginOfData[points_offset + column] = sqrt(value); break;
								}
							}
						}
					}
					progress.setStep(row);

				}
			});
	}

	template<typename T1, typename T2>
	void set_sparse_row_data_T2(mv::Dataset<Points> dataset, std::vector<T1>& column_index, std::vector<T2>& row_offset, H5Utils::VectorHolder& data, TRANSFORM::Type transformType)
	{
		
		data.visit([&dataset, &column_index, &row_offset, transformType](auto& vec)
		{
			return set_sparse_row_data_impl(dataset, column_index, row_offset, vec, transformType);
		});
	}

	template<typename T1>
	void set_sparse_row_data_T1(mv::Dataset<Points> dataset, std::vector<T1>& column_index, H5Utils::VectorHolder& row_offset, H5Utils::VectorHolder& data, TRANSFORM::Type transformType)
	{
		row_offset.visit([dataset, &column_index, &data, transformType](auto& vec)
		{
			return set_sparse_row_data_T2(dataset, column_index, vec, data, transformType);
		});
	}
}



DataContainerInterface::DataContainerInterface(Dataset<Points> points) :
	m_data(points)
{
	
}

void DataContainerInterface::resize(RowID rows, ColumnID columns, std::size_t reserveSize /*= 0*/)
{
	if (m_data->getNumPoints() ==0)
	{
		try
		{
			m_data->setData(nullptr, rows, columns);
		}
		catch(const std::bad_alloc &)
		{
			qDebug() << "Bad Allocation in setData: " << rows << " x " << columns;
		}
			
			

			qDebug() << "Number of dimensions: " << m_data->getNumDimensions();
			qDebug() << "Number of data points: " << m_data->getNumPoints();		
	}
}


void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::int8_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::int8_t>(this->m_data, column_index, row_offset, data, transformType);
}
void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::int16_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::int16_t>(this->m_data, column_index, row_offset, data, transformType);
}

void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::int32_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::int32_t>(this->m_data, column_index, row_offset, data, transformType);
}
void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::uint8_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::uint8_t>(this->m_data, column_index, row_offset, data, transformType);
}
void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::uint16_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::uint16_t>(this->m_data, column_index, row_offset, data, transformType);
}
void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<std::uint32_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, std::uint32_t>(this->m_data, column_index, row_offset, data, transformType);
}

void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t> &column_index, std::vector<uint32_t> &row_offset, std::vector<float> &data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, float>(this->m_data, column_index, row_offset, data, transformType);
}
void DataContainerInterface::set_sparse_row_data(std::vector<uint64_t>& column_index, std::vector<uint32_t>& row_offset, std::vector<biovault::bfloat16_t>& data, TRANSFORM::Type transformType)
{
	local::set_sparse_row_data_impl<uint64_t, uint32_t, biovault::bfloat16_t>(this->m_data, column_index, row_offset, data, transformType);
}


void DataContainerInterface::set_sparse_row_data(H5Utils::VectorHolder& column_index, H5Utils::VectorHolder& row_offset, H5Utils::VectorHolder& data, TRANSFORM::Type transformType)
{
	mv::Dataset<Points> dataset = m_data;
	column_index.visit([dataset, &row_offset, &data, transformType](auto& vec)
	{
		return local::set_sparse_row_data_T1(dataset, vec, row_offset, data, transformType);
	});
}


void DataContainerInterface::increase_sparse_row_data(std::vector<uint64_t> &column_index, std::vector<uint32_t> &row_offset, std::vector<float> &data, TRANSFORM::Type transformType)
{
	long long lrows = ((long long)m_data->getNumPoints());
	auto columns = m_data->getNumDimensions();
	local::Progress progress(m_data->getDataHierarchyItem(), "Loading Data", lrows);
	m_data->visitFromBeginToEnd([&column_index, &row_offset, &data, transformType, lrows, columns, &progress](const auto beginOfData, const auto endOfData)
		{
#pragma omp parallel for
			for (long long row = 0; row < lrows; ++row)
			{
				uint32_t start = row_offset[row];
				uint32_t end = row_offset[row + 1];
				uint64_t points_offset = row * columns;

				for (uint64_t i = start; i < end; ++i)
				{
					float value = data[i];
					uint64_t column = column_index[i];
					if (value != 0)
					{
						switch (transformType.first)
						{
						case TRANSFORM::NONE: beginOfData[points_offset + column] += value; break;
						case TRANSFORM::LOG:  beginOfData[points_offset + column] += log2(1 + value); break;
						case TRANSFORM::ARCSIN5: beginOfData[points_offset + column] += asinh(value / 5.0); break;
						case TRANSFORM::SQRT: beginOfData[points_offset + column] += sqrt(value); break;
						}
					}
				}
				progress.setStep(row);
			}
			
		});
}

void DataContainerInterface::set_sparse_column_data(std::vector<uint64_t> &row_index, std::vector<uint32_t> &column_offset, std::vector<float> &data, TRANSFORM::Type transformType /*= TRANSFORM::NONE*/)
{
	auto columns = m_data->getNumDimensions();
	local::Progress progress(m_data->getDataHierarchyItem(), "Loading Data", columns);
	m_data->visitFromBeginToEnd([&column_offset, &row_index, &data, transformType, columns, &progress](const auto beginOfData, const auto endOfData)
		{
			#pragma  omp parallel for
			for (long long column = 0; column < columns; ++column)
			{
				uint32_t start = column_offset[column];
				uint32_t end = column_offset[column + 1];
				for (uint32_t i = start; i < end; ++i)
				{
					uint32_t r = row_index[i];
					float value = data[i];
					if (value != 0)
					{
						switch (transformType.first)
						{
						case TRANSFORM::NONE: beginOfData[(r*columns) + column] = value; break;
						case TRANSFORM::LOG:  beginOfData[(r*columns) + column] = log2(1 + value); break;
						case TRANSFORM::ARCSIN5:beginOfData[(r*columns) + column] = asinh(value / 5.0); break;
						case TRANSFORM::SQRT: beginOfData[(r*columns) + column] = sqrt(value); break;
						}
					}
				}
				progress.setStep(column);
			}
			
		});
}

void DataContainerInterface::increase_sparse_column_data(std::vector<uint64_t> &row_index, std::vector<uint32_t> &column_offset, std::vector<float> &data, TRANSFORM::Type transformType /*= TRANSFORM::NONE*/)

{
	auto columns = m_data->getNumDimensions();
	local::Progress progress(m_data->getDataHierarchyItem(), "Loading Data", columns);
	m_data->visitFromBeginToEnd([&row_index, &column_offset, &data, transformType, columns, &progress](const auto beginOfData, const auto endOfData)
		{
		    #pragma  omp parallel for
			for (long long column = 0; column < columns; ++column)
			{
				uint32_t start = column_offset[column];
				uint32_t end = column_offset[column + 1];
				for (uint32_t i = start; i < end; ++i)
				{
					uint32_t r = row_index[i];
					float value = data[i];
					if (value != 0)
					{
						switch (transformType.first)
						{
						case TRANSFORM::NONE: beginOfData[(r*columns) + column] += value; break;
						case TRANSFORM::LOG:  beginOfData[(r*columns) + column] += log2(1 + value); break;
						case TRANSFORM::ARCSIN5:beginOfData[(r*columns) + column] += asinh(value / 5.0); break;
						case TRANSFORM::SQRT: beginOfData[(r*columns) + column] += sqrt(value); break;

						}
					}
				}
				progress.setStep(column);
			}
		});
}

void DataContainerInterface::applyTransform(TRANSFORM::Type transformType, bool normalized_and_cpm)
{
	const auto rows = m_data->getNumPoints();
	const auto columns = m_data->getNumDimensions();
	if ((transformType.first == TRANSFORM::NONE) && !normalized_and_cpm)
		return;
	local::Progress progress(m_data->getDataHierarchyItem(), "Processing Data", rows);
	m_data->visitFromBeginToEnd([transformType, rows, columns, &progress](const auto beginOfData, const auto endOfData)
		{
#pragma omp parallel for
			for (std::int64_t row = 0; row < rows; ++row)
			{
				uint64_t offset = row * columns;
				auto *rowdata = &(beginOfData[offset]);
				double dummy = 0.0;
				double sum = std::accumulate(rowdata, rowdata + columns, dummy);
				for (auto *i = rowdata; i != rowdata + columns; ++i)
				{
					const DataValue value = *i;
					if (value != 0)
					{
						const DataValue normalized_cpm_value = (sum == 0) ? 0 : 1e6 * (value / sum);
						switch (transformType.first)
						{
						case TRANSFORM::NONE: *i = normalized_cpm_value; break;
						case TRANSFORM::LOG: *i = log2(1 + normalized_cpm_value); break;
						case TRANSFORM::ARCSIN5: *i = asinh(normalized_cpm_value / 5.0); break;
						case TRANSFORM::SQRT: *i = sqrt(normalized_cpm_value); break;
						}
					}
				}

				progress.setStep(row);
			}
		});
}

mv::Dataset<Points> DataContainerInterface::points()
{
	return m_data;
}

void DataContainerInterface::set(RowID row, ColumnID column, const ValueType & value)
{
	m_data->setValueAt((row*m_data->getNumDimensions()) + column, value);
}

void DataContainerInterface::addRow(RowID row, const std::vector<uint32_t> &columns, const std::vector<float> &data, TRANSFORM::Type transformType)
{
	const std::size_t dataSize = data.size();
	if (dataSize != columns.size())
		throw std::out_of_range("DataContainerInterface::addRow vectors have different sizes!");
	
	auto points_offset = row * m_data->getNumDimensions();
	m_data->visitFromBeginToEnd([&columns, &data, transformType, points_offset](const auto beginOfData, const auto endOfData)
		{
			auto d = data.cbegin();

			for (auto i = columns.cbegin(); i != columns.cend(); ++i, ++d)
			{
				float value = *d;
				auto column = *i;

				switch (transformType.first)
				{
				case TRANSFORM::NONE: beginOfData[points_offset + column] = value; break;
				case TRANSFORM::LOG:  beginOfData[points_offset + column] = log2(1 + value); break;
				case TRANSFORM::ARCSIN5: beginOfData[points_offset + column] = asinh(value / 5.0); break;
				case TRANSFORM::SQRT: beginOfData[points_offset + column] = sqrt(value); break;

				}
			}
		});
}

void DataContainerInterface::add(std::vector<uint32_t> *rows, std::vector<uint32_t> *columns, std::vector<float> *data, TRANSFORM::Type transformType)
{
	auto m_rows = m_data->getNumPoints();
	if ((m_rows + 1) != rows->size())
	{
		m_rows = rows->size() - 1;
		assert(false);
		
	}
	long long lrows = ((long long)m_rows);
	#pragma omp parallel for
	for (long long row = 0; row < lrows; ++row)
	{
		uint32_t start = (*rows)[row];
		uint32_t end = (*rows)[row + 1];
		std::uint64_t points_offset = row * m_data->getNumDimensions();

		m_data->visitFromBeginToEnd([columns, data, transformType, start, end, points_offset](const auto beginOfData, const auto endOfData)
			{
				const float* data_ptr = data->data();
				const float* d = data_ptr + start;
				const uint32_t* column_ptr = columns->data();
				for (const uint32_t* i = column_ptr + start; i != column_ptr + end; ++i, ++d)
				{
					float value = *d;
					auto column = *i;
					if (value != 0)
					{
						switch (transformType.first)
						{
						case TRANSFORM::NONE: beginOfData[points_offset + column] = value; break;
						case TRANSFORM::LOG:  beginOfData[points_offset + column] = log2(1 + value); break;
						case TRANSFORM::ARCSIN5: beginOfData[points_offset + column] = asinh(value / 5.0); break;
						case TRANSFORM::SQRT: beginOfData[points_offset + column] = sqrt(value); break;
						}
					}
				}
			});
	}
}

