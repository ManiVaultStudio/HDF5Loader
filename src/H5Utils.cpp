#include "H5Utils.h"

#include "VectorHolder.h"

#include "ClusterData/Cluster.h"
#include "ClusterData/ClusterData.h"
#include "PointData/PointData.h"
#include "DataHierarchyItem.h"

#include <iostream>
#include <cstdint>

#include <QInputDialog>
#include <QMainWindow>
#include <QtDebug>

#include "H5Cpp.h"

namespace H5Utils
{
	namespace local
	{
		template<typename T>
		void ExtractValues(const std::vector< std::vector<char> > &raw_buffer, H5::CompType compType, const std::string &name, std::vector<QVariant> &result)
		{
			long long nrOfItems = raw_buffer.size();
			if (result.size() < nrOfItems)
				result.resize(nrOfItems);

			for (long long i = 0; i < nrOfItems; ++i)
			{
				CompoundExtractor ce(raw_buffer[i], compType);
				result[i] = ce.extract<T>(name);
			}
		}

		bool ExtractQVariantValues(const std::vector< std::vector<char> > &raw_buffer, H5::CompType compType, const std::string &name, std::vector<QVariant> &result)
		{
			auto nrOfItems = raw_buffer.size();
			if (result.size() < nrOfItems)
				result.resize(nrOfItems);
			bool ok = true;
			for (std::size_t i = 0; i < nrOfItems; ++i)
			{
				CompoundExtractor ce(raw_buffer[i], compType);
				result[i] = ce.extractVariant(name);
				ok &= result[i].isValid();
			}
			return ok;
		}
		
		void ExtractStringValues(const std::vector< std::vector<char> > &raw_buffer, H5::CompType compType, const std::string &name, std::vector<QVariant> &result)
		{
			std::size_t nrOfItems = raw_buffer.size();
			if (result.size() < nrOfItems)
				result.resize(nrOfItems);
			
			for (std::size_t i = 0; i < nrOfItems; ++i)
			{
				CompoundExtractor ce(raw_buffer[i], compType);
				result[i] = ce.extractString(name).c_str();
			}
		}

		template<typename T>
		struct VarLenStruct
		{
			T* ptr;

		};
		
		bool read_var_length_compound_strings(const H5::DataSet &dataset, const std::string &name, std::vector<QVariant> &result)
		{
			typedef VarLenStruct<char> StringStruct;

			H5::CompType dataSetCompType = dataset.getCompType();

			// create datatypes
			H5::StrType strType(H5T_C_S1, H5T_VARIABLE);
			
			strType.setCset(dataSetCompType.getMemberStrType(dataSetCompType.getMemberIndex(name)).getCset());
			H5::CompType compType(sizeof(StringStruct));
			compType.insertMember(name, HOFFSET(StringStruct, ptr), strType);
			std::size_t size = get_vector_size(dataset);
			if(size)
			{
				std::vector<StringStruct> buffer(size);

				//read stuff
				dataset.read(buffer.data(), compType);
				// copy results
				result.resize(size);
				if (strType.getCset() == H5T_CSET_UTF8)
				{
					for (auto i = 0; i < size; ++i)
					{
						result[i] = QString::fromUtf8((const char*)buffer[i].ptr);
					}
				}
				else
				{
					for (auto i = 0; i < size; ++i)
					{
						result[i] = (const char*)buffer[i].ptr;
					}
				}
				/*
				* Release resources.  Note that H5Dvlen_reclaim works
				* for variable-length strings as well as variable-length arrays.
				*/


				H5::DataSet::vlenReclaim(buffer.data(), compType, dataset.getSpace());
				buffer.clear();
				return true;
			}
			return false;
		}

		

		void CreateColorVector(std::size_t nrOfColors, std::vector<QColor>& colors)
		{

			if (nrOfColors)
			{
				colors.resize(nrOfColors);
				std::size_t index = 0;
				for (std::size_t i = 0; i < nrOfColors; ++i)
				{
					const float h = std::min<float>((1.0 * i / (nrOfColors + 1)), 1.0f);
					if (h > 1 || h < 0)
					{
						int bp = 0;
						bp++;
					}
					colors[i] = QColor::fromHsvF(h, 0.5f, 1.0f);
				}
			}
			else
				colors.clear();
		}

	}
	void read_strings(H5::DataSet dataset, std::size_t totalsize, std::vector<std::string>& result)
	{
		try
		{
			H5::StrType strType = dataset.getStrType();
			bool utf8 = false;
			try
			{
				utf8 = (strType.getCset() == H5T_CSET_UTF8);
			}
			catch (const H5::DataTypeIException& e)
			{
				std::cout << strType.getObjName() << std::endl;
				qInfo() << e.getDetailMsg().c_str();
			}
			std::size_t stringSize = strType.getSize();
			result.resize(totalsize);
			std::vector<char> rData(totalsize * stringSize);
			dataset.read(rData.data(), strType);
			for (std::size_t d = 0; d < totalsize; ++d)
			{
				auto offset = rData.cbegin() + (d * stringSize);
				if (*offset == '\"')
					result[d] = std::string(offset + 1, offset + stringSize - 1).c_str();
				else
					result[d] = std::string(offset, offset + stringSize).c_str();
			}

		}
		catch (const H5::Exception &e)
		{
			qCritical() << e.getDetailMsg().c_str();
			result.clear();
		}
		
	}

	H5::PredType getPredTypeFromDataset(const H5::DataSet& dataset)
	{
		auto typeClass = dataset.getTypeClass();
		if (typeClass == H5T_FLOAT)
		{
			H5::FloatType floatType = dataset.getFloatType();
			if (floatType.getSize() == 4)
				return H5::PredType::NATIVE_FLOAT;
			else
				return	H5::PredType::NATIVE_DOUBLE;
		}
		else if (typeClass == H5T_INTEGER)
		{
			H5::IntType intType = dataset.getIntType();
			H5T_sign_t intSign = intType.getSign();
			auto intSize = intType.getSize();
			if (intSign == H5T_SGN_NONE)
			{
				switch (intType.getSize())
				{
				case 1: return H5::PredType::NATIVE_UINT8;
				case 2: return H5::PredType::NATIVE_UINT16;
				case 4: return H5::PredType::NATIVE_UINT32;
				case 8: return H5::PredType::NATIVE_UINT64;
				}
			}
			else if (intSign == H5T_SGN_2)
			{
				switch (intType.getSize())
				{
				case 1: return H5::PredType::NATIVE_INT8;
				case 2: return H5::PredType::NATIVE_INT16;
				case 4: return H5::PredType::NATIVE_INT32;
				case 8: return H5::PredType::NATIVE_INT64;
				}
			}
		}
		// the default
		return H5::PredType::NATIVE_DOUBLE;
	}

	bool read_vector(H5::Group& group, const std::string& name, VectorHolder& vectorHolder)
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
				totalSize *= dimensionSize[d];
		}

		if (dimensions != 1)
			return false;

		H5::PredType predType = getPredTypeFromDataset(dataset);
		vectorHolder.resize(totalSize);
		vectorHolder.setPredTypeSpecifier(predType);

		dataset.read(vectorHolder.data(), vectorHolder.H5DataType()); // since vector holder doesn't support all H5::PredType types we ask which one it is compatible with
		dataset.close();

		return true;
	}

	bool read_vector_string(H5::Group group, const std::string& name, std::vector<std::string>& result)
	{
		try
		{
			//	std::cout << name << " ";
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
				//		std::cout << "rank " << dimensions << ", dimensions ";
				std::vector<hsize_t> dimensionSize(dimensions);
				int ndims = dataspace.getSimpleExtentDims(&(dimensionSize[0]), NULL);
				for (std::size_t d = 0; d < dimensions; ++d)
				{
					//			std::cout << (unsigned long)dimensionSize[d] << " ";
					totalSize *= dimensionSize[d];
				}
				//		std::cout << std::endl;
			}
			if (dimensions != 1)
			{
				return false;
			}
			H5Utils::read_strings(dataset, totalSize, result);
			dataset.close();
			return true;
		}
		catch (const H5::Exception& e)
		{
			qCritical() << e.getDetailMsg().c_str();
		}
		result.clear();
		return false;
	}

	bool read_vector_string(H5::Group group, const std::string& name, std::vector<QString>& result)
	{
		try
		{
			//	std::cout << name << " ";
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
				//		std::cout << "rank " << dimensions << ", dimensions ";
				std::vector<hsize_t> dimensionSize(dimensions);
				int ndims = dataspace.getSimpleExtentDims(&(dimensionSize[0]), NULL);
				for (std::size_t d = 0; d < dimensions; ++d)
				{
					//			std::cout << (unsigned long)dimensionSize[d] << " ";
					totalSize *= dimensionSize[d];
				}
				//		std::cout << std::endl;
			}
			if (dimensions != 1)
			{
				return false;
			}
			H5Utils::read_strings(dataset, totalSize, result);
			dataset.close();
			return true;
		}
		catch(const H5::Exception &e)
		{
			qCritical() << e.getDetailMsg().c_str();
			result.clear();
		}
		
		return false;
	}


	void read_strings(H5::DataSet dataset, std::size_t totalsize, std::vector<QString>& result)
	{
		try
		{
			H5::StrType strType = dataset.getStrType();
			bool utf8 = false;
			try
			{
				utf8 = (strType.getCset() == H5T_CSET_UTF8);
			}
			catch (const H5::DataTypeIException& e)
			{
				qInfo() << e.getDetailMsg().c_str();
			}
			std::size_t stringSize = strType.getSize();
			result.resize(totalsize);
			std::vector<char> rData(totalsize * stringSize);
			dataset.read(rData.data(), strType);
			if (utf8)
			{
				for (std::size_t d = 0; d < totalsize; ++d)
				{
					auto offset = rData.cbegin() + (d * stringSize);
					if (*offset == '\"')
						result[d] = QString::fromUtf8(std::string(offset + 1, offset + stringSize - 1).c_str());
					else
						result[d] = QString::fromUtf8(std::string(offset, offset + stringSize).c_str());
				}
			}
			else
			{
				for (std::size_t d = 0; d < totalsize; ++d)
				{
					auto offset = rData.cbegin() + (d * stringSize);
					if (*offset == '\"')
						result[d] = std::string(offset + 1, offset + stringSize - 1).c_str();
					else
						result[d] = std::string(offset, offset + stringSize).c_str();
				}
			}
		}
		catch (const H5::Exception &e)
		{
			qCritical() << e.getDetailMsg().c_str();
			result.clear();
		}
		
	}

	bool read_vector_string(H5::DataSet dataset, std::vector<std::string> &result)
	{
		try
		{
			H5::DataSpace dataspace = dataset.getSpace();


			H5::DataType datatype = dataset.getDataType();
			if (datatype.isVariableStr())
			{
				typedef local::VarLenStruct<char> StringStruct;

				// create datatypes
				H5::StrType strType = dataset.getStrType();
				bool utf8 = false;
				try
				{
					utf8 = (strType.getCset() == H5T_CSET_UTF8);
				}
				catch (const H5::DataTypeIException& e)
				{
					qInfo() << e.getDetailMsg().c_str();
				}
				std::size_t size = get_vector_size(dataset);
				if (size)
				{
					std::vector<StringStruct> buffer(size);

					//read stuff
					dataset.read(buffer.data(), strType);
					// copy results
					result.resize(size);
					for (auto i = 0; i < size; ++i)
					{
						result[i] = (const char*)buffer[i].ptr;

					}
					/*
					* Release resources.  Note that H5Dvlen_reclaim works
					* for variable-length strings as well as variable-length arrays.
					*/
					
					H5::DataSet::vlenReclaim(buffer.data(), strType, dataset.getSpace());
					buffer.clear();
					return true;
				}
				return false;
			}
			else
			{
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
				read_strings(dataset, totalSize, result);
				dataset.close();
				return true;
			}
		}
		catch(const H5::Exception &e)
		{
			qCritical() << e.getDetailMsg().c_str();
		}
		result.clear();
		return false;
	}


	bool read_vector_string(H5::DataSet dataset, std::vector<QString>& result)
	{
		try
		{
			H5::DataType datatype = dataset.getDataType();
			if (datatype.isVariableStr())
			{
				typedef local::VarLenStruct<char> StringStruct;

				// create datatypes
				H5::StrType strType = dataset.getStrType();
				bool utf8 = false;
				try
				{
					utf8 = (strType.getCset() == H5T_CSET_UTF8);
				}
				catch (const H5::DataTypeIException& e)
				{
					qInfo() << e.getDetailMsg().c_str();
				}
				std::size_t size = get_vector_size(dataset);
				if (size)
				{
					std::vector<StringStruct> buffer(size);

					//read stuff
					dataset.read(buffer.data(), strType);
					if (utf8)
					{
						// copy results
						result.resize(size);
						for (auto i = 0; i < size; ++i)
						{
							result[i] = QString::fromUtf8((const char*)buffer[i].ptr);
						}
					}
					else
					{
						// copy results
						result.resize(size);
						for (auto i = 0; i < size; ++i)
						{
							result[i] = (const char*)buffer[i].ptr;
						}
					}

					/*
					* Release resources.  Note that H5Dvlen_reclaim works
					* for variable-length strings as well as variable-length arrays.
					*/
					H5::DataSet::vlenReclaim(buffer.data(), strType, dataset.getSpace());
					
					buffer.clear();
					return true;
				}
				return false;
			}
			else if (datatype.getClass() == H5T_STRING)
			{
				/*
				* Get the number of dimensions in the dataspace.
				*/
				H5::DataSpace dataspace = dataset.getSpace();
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
				
					read_strings(dataset, totalSize, result);
				dataset.close();
				return true;
			}
		}
		catch(const std::exception &e)
		{
			std::cout << "Exception caught: " << e.what() << std::endl;
			result.clear();
		}
		return false;

	}

	bool read_compound_buffer(const H5::DataSet &dataset, std::vector<std::vector<char>> &result)
	{
		H5::DataSpace dataspace = dataset.getSpace();
		/*
		* Get the number of dimensions in the dataspace.
		*/
		const int dimensions = dataspace.getSimpleExtentNdims();
		std::size_t nrOfItems = 1;
		if (dimensions > 0)
		{

			std::vector<hsize_t> dimensionSize(dimensions);
			int ndims = dataspace.getSimpleExtentDims(&(dimensionSize[0]), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{
				nrOfItems *= dimensionSize[d];
			}

		}
		if (dimensions != 1)
		{
			return false;
		}

		try
		{
			H5::DataType dtype = dataset.getDataType();
			H5::CompType compType = dataset.getCompType();
			auto compoundMembers = compType.getNmembers();
			std::size_t totalCompoundSize = 0;
			for (unsigned int m = 0; m < compoundMembers; ++m)
			{
				auto memberDtype = compType.getMemberDataType(m);
				totalCompoundSize += memberDtype.getSize();
			}
			
			std::size_t compoundSize = dtype.getSize();
			result.resize(nrOfItems);
			std::vector<char> rData(nrOfItems*totalCompoundSize);
			dataset.read(rData.data(), dtype);
			for (std::size_t d = 0; d < nrOfItems; ++d)
			{
				auto offset = rData.cbegin() + (d*compoundSize);
				result[d] = std::vector<char>(offset, offset + compoundSize);
			}
		}
		catch (const std::exception &e)
		{
			std::cout << e.what() << std::endl;
			result.clear();
			return false;
		}
		return true;
	}

	
	bool read_compound(const H5::DataSet &dataset, std::map<std::string, std::vector<QVariant> > &result)
	{
		std::vector<std::vector<char>> raw_buffer;
		if (read_compound_buffer(dataset, raw_buffer))
		{
			H5::CompType compType = dataset.getCompType();
			auto nrOfComponents = compType.getNmembers();
			for (int c = 0; c < nrOfComponents; ++c)
			{
				std::string componentName = compType.getMemberName(c);
				auto componentClass = compType.getMemberClass(c);
				if (componentClass == H5T_STRING)
				{
					if (compType.getMemberStrType(c).isVariableStr())
					{
						if (!local::read_var_length_compound_strings(dataset, componentName, result[componentName]))
						{
							result.erase(componentName);
						}
					}
					else
					{
						if (!local::ExtractQVariantValues(raw_buffer, compType, componentName, result[componentName]))
						{
							result.erase(componentName);
						}
					}
				}
				else
				{
					if (!local::ExtractQVariantValues(raw_buffer, compType, componentName, result[componentName]))
					{
						result.erase(componentName);
					}
					QList<QVariant> list = result[componentName][0].toList();
					const int listSize = list.size();
					if (listSize)
					{
						
						std::size_t nrOfSamples = result[componentName].size();
					//	std::vector<std::vector<double>> temp(listSize);
						for (int l = 0; l < listSize; ++l)
						{
							std::string newName = componentName + "_" + std::to_string(l + 1);
							result[newName].resize(nrOfSamples);
							result[newName][0] = list[l];
						//	temp[l].resize(nrOfSamples);
						//	temp[l][0] = list[l].toDouble();
						}
						for (std::size_t i = 1; i < nrOfSamples; ++i)
						{
							list = result[componentName][i].toList();
							for (int l = 0; l < list.size(); ++l)
							{
								std::string newName = componentName + "_" + std::to_string(l + 1);
								result[newName].resize(nrOfSamples);
								result[newName][i] = list[l];
						//		temp[l][i] = list[l].toDouble();
							}
						}
						result.erase(componentName);
					}
				}
			}
			return true;
		}
		
		return false;
	}

	

	std::size_t get_vector_size(const H5::DataSet &dataset)
	{
		H5::DataSpace dataspace = dataset.getSpace();
		/*
		* Get the number of dimensions in the dataspace.
		*/
		const int dimensions = dataspace.getSimpleExtentNdims();
		if (dimensions == 0)
			return 0;
		std::size_t nrOfItems = 1;
		if (dimensions == 1)
		{

			std::vector<hsize_t> dimensionSize(dimensions);
			int ndims = dataspace.getSimpleExtentDims(&(dimensionSize[0]), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{
				nrOfItems *= dimensionSize[d];
			}
			return nrOfItems;
		}
		else
		{
			return 0;
		}
	}

	CompoundExtractor::CompoundExtractor(const H5::DataSet& d)
	{
		const auto dtype = d.getDataType();
		if (dtype.getClass() != H5T_COMPOUND) {
			throw(std::runtime_error("CompoundExtractor: invalid data set"));
		}
		this->member_info = H5::CompType(d);
		this->data.resize(member_info.getSize());
		d.read(this->data.data(), member_info);
	}

	CompoundExtractor::CompoundExtractor(const std::vector<char>&_data, H5::CompType _ctype) :data(_data)
		, member_info(_ctype)
	{

	}



	
	
	std::string CompoundExtractor::extractString(unsigned index) const
	{
		const auto o = this->member_info.getMemberOffset(index);

		std::string result;
		if ((index + 1) < this->member_info.getNmembers())
		{
			const auto o2 = this->member_info.getMemberOffset(index + 1);
			result = std::string(this->data.data() + o, this->data.data() + o2);
		}
		else
		{
			result = std::string(this->data.data() + o, this->data.data() + this->data.size());
		}
		std::ptrdiff_t resultSize = result.size();
		if (resultSize)
		{
			//remove '\0'
			while (resultSize && (result[resultSize - 1] == '\0'))
			{
				--resultSize;
			}
			result.resize(resultSize);
			// remove '\"'
			if (result[0] == '\"' && result[resultSize-1]== '\"')
			{
				result.erase(resultSize-1);
				result.erase(0);
			}
		}
		return result;
	}

	std::string CompoundExtractor::extractString(const std::string &n) const
	{
		return extractString(this->member_info.getMemberIndex(n));
	}


	QVariant CompoundExtractor::extractVariant(const std::string &n) const
	{
		auto index = member_info.getMemberIndex(n);
		auto sourceClass = member_info.getMemberClass(index);
		
		switch (sourceClass)
		{
			case H5T_NO_CLASS: return QVariant(); // return invalid QVariant
			case H5T_INTEGER:
			{
				H5::IntType intType = member_info.getMemberIntType(index);
				switch (intType.getSign())
				{
					case H5T_SGN_NONE:
					{
						switch (intType.getSize())
						{
							case 1: return extract<std::uint8_t>(index);
							case 2: return extract<std::uint16_t>(index);
							case 4: return extract<std::uint32_t>(index);
							case 8: return (quint64) extract<std::uint64_t>(index);
							default: return QVariant(); // return invalid QVariant
						}
					}
					case H5T_SGN_2:
					{
						switch (intType.getSize())
						{
						case 1: return extract<std::int8_t>(index);
						case 2: return extract<std::int16_t>(index);
						case 4: return extract<std::int32_t>(index);
						case 8: return (qint64) extract<std::int64_t>(index);
						default: return QVariant(); // return invalid QVariant
						}
					}
					default: return QVariant(); // return invalid QVariant
					
				}
				
			}
			
			case H5T_FLOAT:
			{
				H5::FloatType floatType = member_info.getMemberFloatType(index);
				switch (floatType.getSize())
				{
					case 4: return extract<float>(index);
					case 8: return extract<double>(index);
					default: return QVariant(); // return invalid QVariant
				}
			}
			case H5T_TIME: default: return QVariant(); // return invalid QVariant
			case H5T_STRING: 
			{
				H5::StrType strType = member_info.getMemberStrType(index);
				if (strType.isVariableStr() == false)
				{
					std::string text = extractString(index);
					return QString::fromStdString(text);
				}
				
				return QVariant(); // return invalid QVariant // no support for variable length strings here
			}
			case H5T_BITFIELD:return QVariant(); // return invalid QVariant;
			case H5T_OPAQUE:return QVariant(); // return invalid QVariant;
			case H5T_COMPOUND: return QVariant(); // return invalid QVariant;
			case H5T_REFERENCE:return QVariant(); // return invalid QVariant;
			case H5T_ENUM:
			{
				H5::EnumType enumType= member_info.getMemberEnumType(index);
				
				switch (enumType.getSize())
				{
					case 1: {
								std::int8_t numericalValue = extract<std::int8_t>(index);
								std::string text  = enumType.nameOf(&numericalValue,100);
								return text.c_str();
							}
					case 2: {
								std::int16_t numericalValue = extract<std::int16_t>(index);
								std::string text = enumType.nameOf(&numericalValue, 100);
								return text.c_str();
							}
					case 4: {
								std::int32_t numericalValue = extract<std::int32_t>(index);
								std::string text = enumType.nameOf(&numericalValue, 100);
								return text.c_str();
							}
					case 8: {
								std::int64_t numericalValue = extract<std::int64_t>(index);
								std::string text = enumType.nameOf(&numericalValue, 100);
								return text.c_str();
							}
					default: return QVariant(); // return invalid QVariant
				}
			}
			case H5T_VLEN:return QVariant(); // return invalid QVariant;
			case H5T_ARRAY:
			{
				H5::ArrayType arrayType = member_info.getMemberArrayType(index);
				int numDims = arrayType.getArrayNDims();
				if (numDims == 1)
				{
					// for now just 1D arrays
					std::vector<hsize_t> dimensions(numDims);
					arrayType.getArrayDims(dimensions.data());
					auto offset = member_info.getMemberOffset(index);
					QList<QVariant> list;
					auto superType = arrayType.getSuper();
					switch (superType.getClass())
					{
						case H5T_INTEGER:
						{
							H5::IntType intType = *(static_cast<H5::IntType*>(&superType));
							switch (intType.getSign())
							{
							case H5T_SGN_NONE:
							{
								switch (intType.getSize())
								{
								case 1: 
								{
									const std::uint8_t *ptr = extractPtr<std::uint8_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								};
								case 2: 
								{
									const std::uint16_t *ptr = extractPtr<std::uint16_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								}
								case 4: 
								{
									const std::uint32_t *ptr = extractPtr<std::uint32_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								}
								case 8: 
								{
									const std::uint64_t *ptr = extractPtr<std::uint64_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back((quint64)ptr[d]);
									}
									return list;
								}
								default: return QVariant(); // return invalid QVariant
								}
							}
							case H5T_SGN_2:
							{
								switch (intType.getSize())
								{
								case 1:
								{
									const std::int8_t *ptr = extractPtr<std::int8_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								};
								case 2:
								{
									const std::int16_t *ptr = extractPtr<std::int16_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								}
								case 4:
								{
									const std::int32_t *ptr = extractPtr<std::int32_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back(ptr[d]);
									}
									return list;
								}
								case 8:
								{
									const std::int64_t *ptr = extractPtr<std::int64_t>(index);
									for (hsize_t d = 0; d < dimensions[0]; ++d)
									{
										list.push_back((qint64)ptr[d]);
									}
									return list;
								}
								default: return QVariant(); // return invalid QVariant
								}
							}
							default: return QVariant(); // return invalid QVariant

							}
						}
							
						case H5T_FLOAT:
						{
							switch (superType.getSize())
							{
							case 4:
							{
								const float *ptr = extractPtr<float>(index);
								for (hsize_t d = 0; d < dimensions[0]; ++d)
								{
									list.push_back(ptr[d]);
								}
								return list;
							}
							case 8:const double *ptr = extractPtr<double>(index);
							{
								for (hsize_t d = 0; d < dimensions[0]; ++d)
								{
									list.push_back(ptr[d]);
								}
								return list;
							}
							}
						}
					default:
						break;
					}
				}
				
				return QVariant(); // return invalid QVariant;
			}
				
		}
		
	}

	bool is_number(const std::string& s)
	{

		char* end = nullptr;
		strtod(s.c_str(), &end);
		return *end == '\0';
	}

	bool is_number(const QString& s)
	{
		return is_number(s.toStdString());
	}

	inline const std::string QColor_to_stdString(const QColor& color)
	{
		return color.name().toUpper().toStdString();
	}


	QMainWindow* getMainWindow()
	{
		foreach(QWidget * w, qApp->topLevelWidgets())
			if (QMainWindow* mainWin = qobject_cast<QMainWindow*>(w))
				return mainWin;
		return nullptr;
	}

	Dataset<Points> createPointsDataset(mv::CoreInterface* core, bool ask, QString suggestion)
	{
		QString dataSetName = suggestion;
		
		if (ask || suggestion.isEmpty())
		{
			if (suggestion.isEmpty())
				suggestion = "Dataset";
			bool ok = true;
			dataSetName = QInputDialog::getText(getMainWindow(), "Add New Dataset",
				"Dataset name:", QLineEdit::Normal, suggestion, &ok);

			if (!ok || dataSetName.isEmpty())
			{
				throw std::out_of_range("Dataset name out of range");
			}
		}

		
		mv::Dataset<Points> newDataset = mv::data().createDataset("Points", dataSetName);
		
		return newDataset;
	}


	bool compareNumeric(const QString& a, const QString& b)
	{
		auto intA = a.toDouble();
		int intB = b.toInt();
		return a.toDouble() < b.toDouble();
	}

	bool isNumericalVector(const std::vector<QString>& vec) {
		for (const QString& str : vec) {
			bool ok = false;
			str.toDouble(&ok);
			if (!ok) {
				return false;
			}
		}
		return true;
	}

	void addClusterMetaData(std::map<QString, std::vector<unsigned int>>& indices, QString name, mv::Dataset<Points> parent, std::map<QString, QColor> colors, QString prefix)
	{
		if (indices.size() <= 1)
			return; // no point in adding only a single cluster
		while (name[0] == '/')
			name.remove(0, 1);
		while (name[0] == '\\')
			name.remove(0, 1);
		if (colors.empty())
		{
			std::vector<QColor> qcolors;
			local::CreateColorVector(indices.size(), qcolors);

			// let's assume the indices contains numbers which can be sorted
			std::vector<QString> sorted_labels;
			sorted_labels.reserve(indices.size());
			for (const auto& indice : indices)
				sorted_labels.push_back(indice.first);
			if (isNumericalVector(sorted_labels))
				std::sort(sorted_labels.begin(), sorted_labels.end(), compareNumeric);
			else
				std::sort(sorted_labels.begin(), sorted_labels.end());
			std::size_t index = 0;
			for (const auto& label : sorted_labels)
			{
				colors[label] = qcolors[index];
				++index;
			}
		}

		QString datasetName = prefix.isEmpty() ? name : prefix + name;
		Dataset<Clusters> clusterDataset = mv::data().createDataset("Cluster", datasetName, parent);

		clusterDataset->getClusters().reserve(indices.size());

		for (const auto& indice : indices)
		{
			Cluster cluster;
			if (indice.first.isEmpty())
				cluster.setName(QString(" "));
			else
				cluster.setName(indice.first);
			cluster.setColor(colors[indice.first]);

			cluster.setIndices(indice.second);
			clusterDataset->addCluster(cluster);
		}

		// Notify others that the clusters have changed
		events().notifyDatasetDataChanged(clusterDataset);
	}
		

}