#pragma once

#include <PointData/PointData.h>
#include <Dataset.h>
#include <DataHierarchyItem.h>
#include <CoreInterface.h>

#include "H5Cpp.h"

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include <QVariant>
#include <QColor>
#include <QInputDialog>


namespace mv
{
	class CoreInterface;
}

namespace H5Utils
{
	class VectorHolder;

	template<typename R, typename U>
	class IntegerCompareSpecialization
	{
	private:
		template<typename T>
		static constexpr bool UnsupportedTypes()
		{
			return !std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, char> || std::is_same_v<T, char16_t> ||
				std::is_same_v<T, char32_t> || std::is_same_v<T, wchar_t> || std::is_same_v<T, bool> ||
				std::is_same_v<T, std::byte>;
		}
	public:
		static constexpr bool value =  !(UnsupportedTypes<R>() || UnsupportedTypes<U>());
	};

	template<bool IntegerSpecialization, typename T, typename U>
	class compare
	{
	};

	template<typename T, typename U>
	class compare<false, T, U>
	{
	public:
		static constexpr bool in_range(U u_min, U u_max) noexcept
		{
			return ((u_min >= std::numeric_limits<T>::min()) &&(u_max <= std::numeric_limits<T>::max()));
		}
	};
	template<typename T, typename U>
	class compare<true, T, U>
	{
	private:
		static constexpr  bool cmp_less(T t, U u) noexcept
		{
			if constexpr (std::is_signed_v<T> == std::is_signed_v<U>)
				return t < u;
			else if constexpr (std::is_signed_v<T>)
				return t < 0 || std::make_unsigned_t<T>(t) < u;
			else
				return u >= 0 && t < std::make_unsigned_t<U>(u);
		}

		static constexpr  bool cmp_greater(T t, U u) noexcept
		{
			return cmp_less(u, t);
		}

		static constexpr  bool cmp_less_equal(T t, U u) noexcept
		{
			return !cmp_less(u, t);
		}

		static constexpr bool cmp_greater_equal(T t, U u) noexcept
		{
			return !cmp_less(t, u);
		}

	public:		
		static constexpr bool in_range(U u) noexcept
		{
			return cmp_greater_equal(u, std::numeric_limits<T>::min()) &&
				cmp_less_equal(u, std::numeric_limits<T>::max());
		}

		static constexpr bool in_range(U u_min, U u_max) noexcept
		{
			return cmp_greater_equal(u_min, std::numeric_limits<T>::min()) &&
				cmp_less_equal(u_max, std::numeric_limits<T>::max());
		}
	};

	template<typename R, typename T>
	constexpr bool in_range(T u_min, T u_max) noexcept
	{
		return compare<IntegerCompareSpecialization<R,T>::value, R, T>::in_range(u_min, u_max);
	}

	template<typename T>
	bool is_integer(T value)
	{
		return std::is_integral_v<T> || (value == std::round(value));
	}

	template<typename T>
	bool contains_only_integers(const std::vector<T>& data)
	{
		if (std::is_integral_v<T>)
			return true;

		for (auto i = 0; i < data.size(); ++i)
		{
			if (!is_integer(data[i]))
				return false;
		}
		return true;
	}

	template<typename T>
	H5::DataType getH5DataType()
	{
		if(std::numeric_limits<T>::is_specialized)
		{
			if(std::numeric_limits<T>::is_integer)
			{
				if(std::numeric_limits<T>::is_signed)
				{
					switch(sizeof(T))
					{
						case 1: return H5::PredType::NATIVE_INT8;
						case 2: return H5::PredType::NATIVE_INT16;
						case 4: return H5::PredType::NATIVE_INT32;
						case 8: return H5::PredType::NATIVE_INT64;
					}
				}
				else
				{
					switch (sizeof(T))
					{
						case 1: return H5::PredType::NATIVE_UINT8;
						case 2: return H5::PredType::NATIVE_UINT16;
						case 4: return H5::PredType::NATIVE_UINT32;
						case 8: return H5::PredType::NATIVE_UINT64;
					}
				}
			}
			else 
			{
				assert(std::is_floating_point<T>());
				switch (sizeof(T))
				{
					case 4: return H5::PredType::NATIVE_FLOAT;
					case 8: return H5::PredType::NATIVE_DOUBLE;
				}
			}
		}
		else
		{
			if(std::is_same_v<biovault::bfloat16_t,T>)
				return H5::PredType::NATIVE_UINT16; // use in 10x Loader
		}
		return H5::DataType();
	}
	
	template<typename T>
	class MultiDimensionalData
	{
		public:
			MultiDimensionalData() = default;
			std::vector<hsize_t> size;
			std::vector<T> data;
	};

	template<typename T>
	bool read_multi_dimensional_data(const H5::DataSet &dataset, MultiDimensionalData<T> &mdd)
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
		dataset.read(mdd.data.data(), getH5DataType<T>());
		return true;
	}

	template<typename T>
	bool read_vector(H5::Group &group, const std::string &name, std::vector<T>*vector_ptr)
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
		dataset.read(vector_ptr->data(), getH5DataType<T>());
		dataset.close();
		return true;
	}

		inline bool contains_name(H5::Group& group, const std::string& name)
	{
		return group.exists(name);
	}

	bool read_vector(H5::Group& group, const std::string& name, VectorHolder& vectorHolder);
	
	bool read_vector_string(H5::Group group, const std::string& name, std::vector<std::string>& result);
	bool read_vector_string(H5::Group group, const std::string& name, std::vector<QString>& result);
	void read_strings(H5::DataSet dataset, std::size_t totalsize, std::vector<std::string> &result);
	void read_strings(H5::DataSet dataset, std::size_t totalsize, std::vector<QString>& result);

	bool read_vector_string(H5::DataSet dataset, std::vector<std::string> &result);
	bool read_vector_string(H5::DataSet dataset, std::vector<QString>& result);

	//bool read_vector_string(H5::Group& group, const std::string& name, std::vector<std::string>& result);

	inline const std::string QColor_to_stdString(const QColor& color);

	template<class RandomIterator>
	void transpose(RandomIterator first, RandomIterator last, int m, mv::DataHierarchyItem &progressItem)
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
					progressItem.getDataset()->getTask().setProgress(progress);
					QGuiApplication::instance()->processEvents();
				}
				
				
			} while ((first + a) != cycle);
		}
		progressItem.getDataset()->getTask().setProgress(100);
		
	}

	bool is_number(const std::string& s);
	bool is_number(const QString& s);

	bool read_vector_string(H5::Group group, const std::string& name, std::vector<std::string>& result);

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

	
	mv::Dataset<Points> createPointsDataset(::mv::CoreInterface* core,bool ask=true, QString=QString());

	template<typename numericalMetaDataType>
	mv::Dataset<Points> addNumericalMetaData(std::vector<numericalMetaDataType>& numericalData, std::vector<QString>& numericalDimensionNames, bool transpose, mv::Dataset<Points> parent, QString name = QString(), int storageType = (int)PointData::ElementTypeSpecifier::float32)
	{
	
		const std::size_t numberOfDimensions = numericalDimensionNames.size();
		if (numberOfDimensions)
		{

			std::size_t nrOfSamples = numericalData.size() / numberOfDimensions;

			//std::cout << "DEBUG: " << "addNumericalMetaData: " << nrOfSamples << " x " << numberOfDimensions << std::endl;
			QString numericalDatasetName = "Numerical MetaData";
			if (!name.isEmpty())
			{
				// if numericalMetaDataDimensionNames start with name, remove it
				for (auto& dimName : numericalDimensionNames)
				{
					if (dimName.indexOf(name) == 0)
					{
						dimName.remove(0, name.length());
					}
					// remove forward slash from dimName if it has one
				
					while ((dimName.size()) && (dimName[0] == '/'))
						dimName.remove(0, 1);
					while ((dimName.size()) && (dimName[0] == '\\'))
						dimName.remove(0, 1);
				}
				// remove forward slash from name if it has one
				while ((name.size()) && (name[0] == '/'))
					name.remove(0, 1);
				while (name.size() && (name[0] == '\\'))
					name.remove(0, 1);
				numericalDatasetName = name /* + " (numerical)"*/;
			}


			mv::Dataset<Points> numericalMetadataDataset = mv::data().createDerivedDataset(numericalDatasetName, parent); // core->addDataset("Points", numericalDatasetName, parent);
			if(storageType >=0)
			{
				PointData::ElementTypeSpecifier newTargetType = (PointData::ElementTypeSpecifier)storageType;
				switch (newTargetType)
				{
				case PointData::ElementTypeSpecifier::float32: numericalMetadataDataset->setDataElementType<float>(); break;
				case PointData::ElementTypeSpecifier::bfloat16: numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>(); break;
				case PointData::ElementTypeSpecifier::int16: numericalMetadataDataset->setDataElementType<std::int16_t>(); break;
				case PointData::ElementTypeSpecifier::uint16: numericalMetadataDataset->setDataElementType<std::uint16_t>(); break;
				case PointData::ElementTypeSpecifier::int8: numericalMetadataDataset->setDataElementType<std::int8_t>(); break;
				case PointData::ElementTypeSpecifier::uint8: numericalMetadataDataset->setDataElementType<std::uint8_t>(); break;
				}
			}
			else
			{
				numericalMetadataDataset->setDataElementType<numericalMetaDataType>();
				if(storageType <= -2)
				{
					const bool allow_lossy_storage = (storageType == -2);
					auto sizeOfT = sizeof(numericalMetaDataType);
					auto minmax_pair = std::minmax_element(numericalData.cbegin(), numericalData.cend());
					numericalMetaDataType minVal = *(minmax_pair.first);
					numericalMetaDataType maxVal = *(minmax_pair.second);
					if (*(minmax_pair.first) < 0)
					{
						//signed
						if (in_range<std::int8_t>(minVal, maxVal))
						{
							if (is_integer(minVal) && is_integer(maxVal) && contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::int8_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
						else if ((sizeOfT > 2) && in_range<std::int16_t>(minVal, maxVal))
						{
							if (is_integer(minVal) && is_integer(maxVal) && contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::int16_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
					}
					else
					{
						//unsigned
						if (in_range<std::uint8_t>(minVal, maxVal))
						{
							if (is_integer(minVal) && is_integer(maxVal) && contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::uint8_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
						else if ((sizeOfT > 2) && (in_range<std::uint16_t>(minVal, maxVal)))
						{
							if (is_integer(minVal) && is_integer(maxVal) && contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::uint16_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
					}
				}
			}
			

			mv::events().notifyDatasetAdded(numericalMetadataDataset);

			auto& task = numericalMetadataDataset->getDataHierarchyItem().getDataset()->getTask();

			task.setName("Loading points");
			task.setProgressDescription("Transposing");
			task.setRunning();

			if (transpose)
			{
				H5Utils::transpose(numericalData.begin(), numericalData.end(), nrOfSamples, numericalMetadataDataset->getDataHierarchyItem());
			}

			numericalMetadataDataset->setDataElementType<numericalMetaDataType>();
			numericalMetadataDataset->setData(std::move(numericalData), numberOfDimensions);
			numericalMetadataDataset->setDimensionNames(numericalDimensionNames);
			
			task.setFinished();

			mv::events().notifyDatasetDataChanged(numericalMetadataDataset);
			return numericalMetadataDataset;
		}

		return mv::Dataset<Points>();
	}

	void addClusterMetaData(const std::map<QString, std::vector<unsigned int>>& indices, QString name, mv::Dataset<Points> parent, std::map<QString, QColor> colors = std::map<QString, QColor>(), QString prefix = QString(), bool filterUniqueProperties = false);

}

