#pragma once

#include "PointData/PointData.h"
#include "DataTransform.h"
#include "Dataset.h"

#include "VectorHolder.h"

typedef std::uint64_t DataPointID;
typedef std::uint64_t MarkerID;
typedef float DataValue;

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
	//void addRowDataPtr(RowID row, const float * const data, bool normalize, TRANSFORM::Type transformType);
	//void addDataValue(RowID row, ColumnID column, float value, TRANSFORM::Type transformType);
	//void increaseDataValue(RowID row, ColumnID column, float value, TRANSFORM::Type transformType);
	
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

	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int8_t>& x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int16_t>& x, TRANSFORM::Type transformType);
	//void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::int32_t>& x, TRANSFORM::Type transformType);

	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint8_t>& x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint16_t>& x, TRANSFORM::Type transformType);
	//void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<std::uint32_t>& x, TRANSFORM::Type transformType);

	void set_sparse_row_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);
	void set_sparse_row_data(std::vector<uint64_t>& i, std::vector<uint32_t>& p, std::vector<biovault::bfloat16_t>& x, TRANSFORM::Type transformType);
	void set_sparse_row_data(H5Utils::VectorHolder& i, H5Utils::VectorHolder& p, H5Utils::VectorHolder& x, TRANSFORM::Type transformType);
	
	
	void increase_sparse_row_data(std::vector<uint64_t> &i, std::vector<uint32_t> &p, std::vector<float> &x, TRANSFORM::Type transformType);

	
	void resize(RowID rows, ColumnID columns, std::size_t reserveSize = 0);
	

};
