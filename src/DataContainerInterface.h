#pragma once

#include "PointData/PointData.h"
#include "DataTransform.h"
#include "Dataset.h"
#include "H5Utils.h"

typedef std::uint64_t DataPointID;
typedef std::uint64_t MarkerID;
typedef float DataValue;

namespace local
{
	class Progress
	{
		mv::DataHierarchyItem& dataHierarcyItem;

	public:
		Progress(mv::DataHierarchyItem& item, const QString& taskName, std::size_t nrOfSteps);
		void setStep(std::size_t step);
		~Progress();
		
	};
}

class DataContainerInterface // acting as a translation between Cytosplore Transcriptomics and PointData. Should be removed in the future
{

public:
	typedef DataPointID RowID;
	typedef MarkerID ColumnID;
	typedef DataValue ValueType;

private:
	
	mv::Dataset<Points>		m_data;
	
public:
	void applyTransform(TRANSFORM::Type transformType, bool normalized_and_cpm);
	void addRowDataPtr(RowID row, const float * const data, bool normalize, TRANSFORM::Type transformType);
	void addDataValue(RowID row, ColumnID column, float value, TRANSFORM::Type transformType);
	void increaseDataValue(RowID row, ColumnID column, float value, TRANSFORM::Type transformType);
	
	explicit DataContainerInterface(mv::Dataset<Points> points);
	~DataContainerInterface() = default;

	mv::Dataset<Points> points();

// 	const DataValue get(RowID row, ColumnID column) const;
 	void set(RowID row, ColumnID column, const ValueType & value);
// 	void setRow(RowID row, const RowVector &rowVector);
	

 	void add(std::vector<uint32_t> *rows, std::vector<uint32_t> *columns, std::vector<float> *data, TRANSFORM::Type transformType);
// 	void increase(std::vector<uint32_t> *rows, std::vector<uint32_t> *columns, std::vector<float> *data, TRANSFORM::Type transformType);
// 
// 	void addDataPtr(const float * const data, bool normalized_cpm, TRANSFORM::Type transformType);
 	void addRow(RowID row, const std::vector<uint32_t> &columns, const std::vector<float> &data, TRANSFORM::Type transformType);

	void set_sparse_column_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);
	void increase_sparse_column_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);

	template<typename T1, typename T2, typename T3>
	static void set_sparse_row_data_impl (mv::Dataset<Points> m_data, std::vector<T1>& column_index, std::vector<T2>& row_offset, std::vector<T3>& data, TRANSFORM::Type transformType)
	{
		static_assert(!std::is_same<T3, std::int64_t>::value, "");
		static_assert(!std::is_same<T3, std::uint64_t>::value, "");

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

	template<typename T1, typename T2, typename T3>
	void set_sparse_row_data(std::vector<T1>& column_index, std::vector<T2>& row_offset, std::vector<T3>& data, TRANSFORM::Type transformType)
	{
		set_sparse_row_data_impl(m_data, column_index, row_offset, data, transformType);
	}

	/*
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int8_t>& x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int16_t>& x, TRANSFORM::Type transformType);
	//void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int32_t>& x, TRANSFORM::Type transformType);

	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint8_t>& x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint16_t>& x, TRANSFORM::Type transformType);
	//void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint32_t>& x, TRANSFORM::Type transformType);

	void set_sparse_row_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<biovault::bfloat16_t>& x, TRANSFORM::Type transformType);
	*/
	void set_sparse_row_data(H5Utils::VectorHolder& i, H5Utils::VectorHolder& p, H5Utils::VectorHolder& x, TRANSFORM::Type transformType);
	
	

	void increase_sparse_row_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);

	
	void resize(RowID rows, ColumnID columns, std::size_t reserveSize = 0);
	

};
