#ifndef H5UTILS_H
#define H5UTILS_H

#include "H5Cpp.h"
#include <string>
#include <vector>
#include <map>
#include "QVariant"

namespace H5Utils
{
	

	template<typename T>
	class MultiDimensionalData
	{
		public:
			MultiDimensionalData() = default;
			std::vector<hsize_t> size;
			std::vector<T> data;
	};

	template<typename T>
	bool read_multi_dimensional_data(const H5::DataSet &dataset, MultiDimensionalData<T> &data, const H5::DataType &mem_type)
	{
		data.data.clear();
		data.size.clear();

		H5::DataSpace dataspace = dataset.getSpace();
		/*
		* Get the number of dimensions in the dataspace.
		*/
		const int dimensions = dataspace.getSimpleExtentNdims();
		std::size_t totalSize = 1;
		if (dimensions > 0)
		{

			data.size.resize(dimensions);
			int ndims = dataspace.getSimpleExtentDims(data.size.data(), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{
				totalSize *= data.size[d];
			}
		}
		else
		{
			return false;
		}
		data.data.resize(totalSize);
		dataset.read(data.data.data(), mem_type);
		return true;
	}
	template<typename T>
	bool read_vector(H5::Group &group, const std::string &name, std::vector<T>*vector_ptr, const H5::DataType &mem_type)
	{

		if (!group.exists(name))
			return false;
		H5::DataSet dataset = group.openDataSet(name);

		H5::DataSpace dataspace = dataset.getSpace();
		/*
		* Get the number of dimensions in the dataspace.
		*/
		const int dimensions = dataspace.getSimpleExtentNdims();
		std::size_t totalSize = 1;
		if (dimensions > 0)
		{

			std::vector<hsize_t> dimensionSize(dimensions);
			int ndims = dataspace.getSimpleExtentDims(&(dimensionSize[0]), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{

				totalSize *= dimensionSize[d];
			}

		}
		if (dimensions != 1)
		{
			return false;
		}
		vector_ptr->resize(totalSize);
		dataset.read(vector_ptr->data(), mem_type);
		dataset.close();
		return true;
	}

	void read_strings(H5::DataSet &dataset, std::size_t totalsize, std::vector<std::string> &result);

	bool read_vector_string(H5::DataSet dataset, std::vector<std::string> &result);

	class CompoundExtractor
	{
		//based on: https://stackoverflow.com/questions/41782527/c-hdf5-extract-one-member-of-a-compound-data-type

		std::vector<char> data;
		H5::CompType member_info;
	public:;
		   CompoundExtractor(const H5::DataSet& d);
		   CompoundExtractor(const std::vector<char>&_data, H5::CompType _ctype);

		 
		   template<typename T>
		   T extract(const std::string &n) const {
			   const auto index = this->member_info.getMemberIndex(n);
			   return extract<T>(index);
		   } // end of CompoundExtractor::extract
		   template<typename T>
		   T extract(unsigned index) const {
			   const auto o = this->member_info.getMemberOffset(index);
			   return *(reinterpret_cast<const T *>(this->data.data() + o));
		   } // end of CompoundExtractor::extract
		   template<typename T>
		   const T* extractPtr(unsigned index) const {
			   const auto o = this->member_info.getMemberOffset(index);
			   return (reinterpret_cast<const T *>(this->data.data() + o));
		   } // end of CompoundExtractor::extract

		   std::string extractString(unsigned index) const; // end
		   std::string extractString(const std::string &n) const; // end
		   
		   QVariant extractVariant(const std::string &n) const;

	};


	bool read_compound_buffer(const H5::DataSet &dataset, std::vector<std::vector<char>> &result);
	bool read_compound(const H5::DataSet &dataset, std::map<std::string, std::vector<QVariant> > &result);

	//bool read_buffer_vector(H5::DataSet &dataset, std::vector<std::vector<char>> &result);

	std::size_t get_vector_size(const H5::DataSet &dataset);
	
}
#endif