#pragma once

#include <DataHierarchyItem.h>

#include "H5Cpp.h"
#include <string>
#include <vector>
#include <map>
#include <QVariant>
#include <QColor>
#include "PointData.h"
#include "Dataset.h"
#include "DataHierarchyItem.h"
#include <QBitArray>
namespace hdps
{
	class CoreInterface;
}

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
	bool read_multi_dimensional_data(const H5::DataSet &dataset, MultiDimensionalData<T> &mdd, const H5::DataType &mem_type)
	{
		mdd.data.clear();
		mdd.size.clear();

		H5::DataSpace dataspace = dataset.getSpace();
		/*
		* Get the number of dimensions in the dataspace.
		*/
		const int dimensions = dataspace.getSimpleExtentNdims();
		std::size_t totalSize = 1;
		if (dimensions > 0)
		{

			mdd.size.resize(dimensions);
			int ndims = dataspace.getSimpleExtentDims(mdd.size.data(), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{
				totalSize *= mdd.size[d];
			}
		}
		else
		{
			return false;
		}
		mdd.data.resize(totalSize);
		dataset.read(mdd.data.data(), mem_type);
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

	
	bool read_vector_string(H5::Group& group, const std::string& name, std::vector<std::string>& result);
	bool read_vector_string(H5::Group& group, const std::string& name, std::vector<QString>& result);
	void read_strings(H5::DataSet &dataset, std::size_t totalsize, std::vector<std::string> &result);
	void read_strings(H5::DataSet& dataset, std::size_t totalsize, std::vector<QString>& result);

	bool read_vector_string(H5::DataSet &dataset, std::vector<std::string> &result);
	bool read_vector_string(H5::DataSet &dataset, std::vector<QString>& result);

	//bool read_vector_string(H5::Group& group, const std::string& name, std::vector<std::string>& result);

	inline const std::string QColor_to_stdString(const QColor& color);

	template<class RandomIterator>
	void transpose(RandomIterator first, RandomIterator last, int m, hdps::DataHierarchyItem &progressItem)
	{
		//https://stackoverflow.com/questions/9227747/in-place-transposition-of-a-matrix
		const std::ptrdiff_t mn1 = (last - first - 1);
		const std::ptrdiff_t n = (last - first) / m;
		RandomIterator cycle = first;
		std::vector<uint8_t> visited(last - first, 0);
		std::size_t updateCounter = 0;
		std::size_t updateFrequency = n;
		if (updateFrequency > 1000)
			updateFrequency = 100;
		while (++cycle != last) {
			if (visited[cycle - first])
				continue;
			std::ptrdiff_t a = cycle - first;
			do {
				a = a == mn1 ? mn1 : n * a % mn1;
				std::swap(*(first + a), *cycle);
				visited[a] = 1;
				++updateCounter;
				if((updateCounter % updateFrequency) == 0)
				{
					float progress = (1.0 * updateCounter) / (last - first);
					progressItem.setTaskProgress(progress);
					QGuiApplication::instance()->processEvents();
				}
				
				
			} while ((first + a) != cycle);
		}
		progressItem.setTaskProgress(100);
		
	}

	bool is_number(const std::string& s);
	bool is_number(const QString& s);
	


	bool read_vector_string(H5::Group& group, const std::string& name, std::vector<std::string>& result);
	


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

	
	hdps::Dataset<Points> createPointsDataset(::hdps::CoreInterface* core,bool ask=true, QString=QString());

	
	void addNumericalMetaData(hdps::CoreInterface* core, std::vector<float>& numericalData, std::vector<QString>& numericalDimensionNames, bool transpose, hdps::Dataset<Points> parent, QString name=QString());
	void addClusterMetaData(hdps::CoreInterface* core, std::map<QString, std::vector<unsigned int>>& indices, QString name, hdps::Dataset<Points> parent, std::map<QString, QColor> colors = std::map<QString, QColor>());
}

