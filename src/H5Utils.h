#pragma once

#include <DataHierarchyItem.h>
#include <CoreInterface.h>

#include "H5Cpp.h"
#include <string>
#include <vector>
#include <map>
#include <QVariant>
#include <QColor>
#include "PointData/PointData.h"
#include "Dataset.h"
#include <iostream>
#include <cstdint>
#include <QInputDialog>



namespace mv
{
	class CoreInterface;
}

namespace cpp20std
{
	template<class T, class U>
	constexpr bool cmp_less(T t, U u) noexcept
	{
		//if(std::is_floating_point<T>() || std::is_floating_point<U>())
		{
			double td = t;
			double ud = u;
			return t < u;
		}
		/*
		if constexpr (std::is_signed_v<T> == std::is_signed_v<U>)
			return t < u;
		else if constexpr (std::is_signed_v<T>)
			return t < 0 || std::make_unsigned_t<T>(t) < u;
		else
			return u >= 0 && t < std::make_unsigned_t<U>(u);
			*/
	}

	template<class T, class U>
	constexpr bool cmp_less_equal(T t, U u) noexcept
	{
		return !cmp_less(u, t);
	}

	template<class T, class U>
	constexpr bool cmp_greater_equal(T t, U u) noexcept
	{
		return !cmp_less(t, u);
	}

	template<class R, class T>
	constexpr bool in_range(T minVal, T maxVal) noexcept
	{
		static_assert(!std::is_floating_point<R>());
		return cmp_greater_equal(minVal, std::numeric_limits<R>::min()) &&
			cmp_less_equal(maxVal, std::numeric_limits<R>::max());
	}

	template<typename T>
	bool is_integer(T value)
	{
		return ((value - static_cast<std::ptrdiff_t>(value)) < 1e-03);
	}
	template<typename T>
	bool contains_only_integers(const std::vector<T>& data)
	{
		for (auto i = 0; i < data.size(); ++i)
		{
			if (!is_integer(data[i]))
				return false;
		}

	}
}
namespace H5Utils
{

	class VectorHolder // copied form PointData for now, and added double
	{
	private:
		using TupleOfVectors = std::tuple <
			std::vector<float>,
			std::vector<biovault::bfloat16_t>,
			std::vector<std::int64_t>,
			std::vector<std::uint64_t>,
			std::vector<std::int32_t>,
			std::vector<std::uint32_t>,
			std::vector<std::int16_t>,
			std::vector<std::uint16_t>,
			std::vector<std::int8_t>,
			std::vector<std::uint8_t>,
			std::vector<double> >;

		

	public:
		enum class ElementTypeSpecifier
		{
			float32,
			bfloat16,
			int64,
			uint64,
			int32,
			uint32,
			int16,
			uint16,
			int8,
			uint8,
		};

		static constexpr std::array<const char*, std::tuple_size<TupleOfVectors>::value> getElementTypeNames()
		{
			return
			{ {
				"float32",
				"bfloat16",
				"int64",
				"uint64",
				"int32",
				"uint32",
				"int16",
				"uint16",
				"int8",
				"uint8",
				"float64"
			} };
		}

	private:
		// Tuple of vectors. Only the vector whose value_type corresponds to _elementTypeSpecifier
	   // is selected. (The other vector is ignored, and could be cleared.) The vector stores the
	   // point data in dimension-major order
	   // Note: Instead of std::tuple, std::variant (from C++17) might be more appropriate, but at
	   // the moment of writing, C++17 may not yet be enabled system wide.
		TupleOfVectors _tupleOfVectors;

		// Specifies which vector is selected, based on its value_type.
		ElementTypeSpecifier _elementTypeSpecifier{};

		// Tries to find the element type specifier that corresponds to ElementType.
		template <typename ElementType, typename Head, typename... Tail>
		constexpr static ElementTypeSpecifier recursiveFindElementTypeSpecifier(
			const std::tuple<Head, Tail...>*,
			const int temp = 0)
		{
			using HeadValueType = typename Head::value_type;

			if (std::is_same<ElementType, HeadValueType>::value)
			{
				return static_cast<ElementTypeSpecifier>(temp);
			}
			else
			{
				constexpr const std::tuple<Tail...>* tailNullptr{ nullptr };
				return recursiveFindElementTypeSpecifier<ElementType>(tailNullptr, temp + 1);
			}
		}


		template <typename ElementType>
		constexpr static ElementTypeSpecifier recursiveFindElementTypeSpecifier(const std::tuple<>*, const int)
		{
			return ElementTypeSpecifier{}; // Should not occur!
		}

		template <typename ReturnType, typename VectorHolderType, typename FunctionObject, typename Head, typename... Tail>
		static ReturnType recursiveVisit(VectorHolderType& vectorHolder, FunctionObject functionObject, const std::tuple<Head, Tail...>*)
		{
			using HeadValueType = typename Head::value_type;

			if (vectorHolder.template isSameElementType<HeadValueType>())
			{
				return functionObject(vectorHolder.template getVector<HeadValueType>());
			}
			else
			{
				constexpr const std::tuple<Tail...>* tailNullptr{ nullptr };
				return recursiveVisit<ReturnType>(vectorHolder, functionObject, tailNullptr);
			}
		}

		template <typename ReturnType, typename VectorHolderType, typename FunctionObject>
		static ReturnType recursiveVisit(VectorHolderType&, FunctionObject&, const std::tuple<>*)
		{
			struct VisitException : std::exception
			{
				const char* what() const noexcept override
				{
					return "visit error!";
				}
			};
			throw VisitException{};
		}

	public:

		/// Yields the n-th supported element type.
		template <std::size_t N>
		using ElementTypeAt = typename std::tuple_element_t<N, TupleOfVectors>::value_type;

		/// Defaulted default-constructor. Ensures that the element type
		/// specifier is default-initialized (to "float32").
		VectorHolder() = default;

		/// Explicit constructor that copies the specified vector into the
		/// internal data structure. Ensures that the element type specifier
		/// matches the value type of the vector.
		template <typename T>
		explicit VectorHolder(const std::vector<T>& vec)
			:
			_elementTypeSpecifier{ getElementTypeSpecifier<T>() }
		{
			std::get<std::vector<T>>(_tupleOfVectors) = vec;
		}


		/// Explicit constructor that efficiently "moves" the specified vector
		/// into the internal data structure. Ensures that the element type
		/// specifier matches the value type of the vector.
		template <typename T>
		explicit VectorHolder(std::vector<T>&& vec)
			:
			_elementTypeSpecifier{ getElementTypeSpecifier<T>() }
		{
			std::get<std::vector<T>>(_tupleOfVectors) = std::move(vec);
		}


		// Similar to C++17 std::visit.
		template <typename ReturnType = void, typename FunctionObject>
		ReturnType constVisit(FunctionObject functionObject) const
		{
			constexpr const TupleOfVectors* const tupleNullptr{ nullptr };
			return recursiveVisit<ReturnType>(*this, functionObject, tupleNullptr);
		}


		// Similar to C++17 std::visit.
		template <typename ReturnType = void, typename FunctionObject>
		ReturnType visit(FunctionObject functionObject)
		{
			constexpr const TupleOfVectors* const tupleNullptr{ nullptr };
			return recursiveVisit<ReturnType>(*this, functionObject, tupleNullptr);
		}

		template <typename T>
		static constexpr ElementTypeSpecifier getElementTypeSpecifier()
		{
			constexpr const TupleOfVectors* const tupleNullptr{ nullptr };
			return recursiveFindElementTypeSpecifier<T>(tupleNullptr);
		}

		template <typename T>
		bool isSameElementType() const
		{
			constexpr auto elementTypeSpecifier = getElementTypeSpecifier<T>();
			return elementTypeSpecifier == _elementTypeSpecifier;
		}

		template <typename T>
		const std::vector<T>& getConstVector() const
		{
			// This function should only be used to access the currently selected vector.
			assert(isSameElementType<T>());
			return std::get<std::vector<T>>(_tupleOfVectors);
		}

		template <typename T>
		const std::vector<T>& getVector() const
		{
			return getConstVector<T>();
		}

		template <typename T>
		std::vector<T>& getVector()
		{
			return const_cast<std::vector<T>&>(getConstVector<T>());
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		std::size_t size() const
		{
			return constVisit<std::size_t>([](const auto& vec) { return vec.size(); });
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void resize(const std::size_t newSize)
		{
			visit([newSize](auto& vec) { vec.resize(newSize); });
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void clear()
		{
			visit([](auto& vec) { return vec.clear(); });
		}

		/// Just forwarding to the corresponding member function of the currently selected std::vector.
		void shrink_to_fit()
		{
			visit([](auto& vec) { return vec.shrink_to_fit(); });
		}

		void setElementTypeSpecifier(const ElementTypeSpecifier elementTypeSpecifier)
		{
			_elementTypeSpecifier = elementTypeSpecifier;
		}

		ElementTypeSpecifier getElementTypeSpecifier() const
		{
			return _elementTypeSpecifier;
		}


		/// Resizes the currently active internal data vector to the specified
		/// number of elements, and converts the elements of the specified data
		/// to the internal data element type, by static_cast. 
		template <typename T>
		void convertData(const T* const data, const std::size_t numberOfElements)
		{
			resize(numberOfElements);

			visit([data](auto& vec)
				{
					std::size_t i{};
					for (auto& elem : vec)
					{
						elem = static_cast<std::remove_reference_t<decltype(elem)>>(data[i]);
						++i;
					}
				});
		}
	};

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

	template<typename T>
	bool read_vector(H5::Group& group, const std::string& name, VectorHolder& vectorHolder, H5::PredType predType)
	{
		vectorHolder.setElementTypeSpecifier(VectorHolder::getElementTypeSpecifier<T>());
		return read_vector(group, name, &vectorHolder.getVector<T>(), predType);
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
						if (cpp20std::in_range<std::int8_t>(minVal, maxVal))
						{
							if (cpp20std::is_integer(minVal) && cpp20std::is_integer(maxVal) && cpp20std::contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::int8_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
						else if ((sizeOfT > 2) && cpp20std::in_range<std::int16_t>(minVal, maxVal))
						{
							if (cpp20std::is_integer(minVal) && cpp20std::is_integer(maxVal) && cpp20std::contains_only_integers(numericalData))
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
						if (cpp20std::in_range<std::uint8_t>(minVal, maxVal))
						{
							if (cpp20std::is_integer(minVal) && cpp20std::is_integer(maxVal) && cpp20std::contains_only_integers(numericalData))
							{
								numericalMetadataDataset->setDataElementType<std::uint8_t>();
							}
							else
							{
								if (allow_lossy_storage)
									numericalMetadataDataset->setDataElementType<biovault::bfloat16_t>();
							}
						}
						else if ((sizeOfT > 2) && (cpp20std::in_range<std::uint16_t>(minVal, maxVal)))
						{
							if (cpp20std::is_integer(minVal) && cpp20std::is_integer(maxVal) && cpp20std::contains_only_integers(numericalData))
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

#if defined(MANIVAULT_API_Old)
			mv::events().notifyDatasetChanged(numericalMetadataDataset);
#elif defined(MANIVAULT_API_New)
			mv::events().notifyDatasetDataChanged(numericalMetadataDataset);
#endif

			return numericalMetadataDataset;
		}

		
	}

	
	void addClusterMetaData(std::map<QString, std::vector<unsigned int>>& indices, QString name, mv::Dataset<Points> parent, std::map<QString, QColor> colors = std::map<QString, QColor>(), QString prefix = QString());


	
}

