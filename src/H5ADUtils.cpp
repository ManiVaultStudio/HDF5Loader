#include "H5ADUtils.h"
#include "DataContainerInterface.h"
#include <filesystem>
#include <QDialogButtonBox>

#include <PointData/DimensionsPickerAction.h>
#include <QMainWindow>

namespace H5AD
{
	using namespace  mv;

	

	struct compareStringsAsNumbers {
		bool operator()(const std::string& a, const std::string& b) const
		{
			try
			{
				if (H5Utils::is_number(a) && H5Utils::is_number(b))
				{
					double d_a = strtod(a.c_str(), nullptr);
					double d_b = strtod(b.c_str(), nullptr);
					return d_a < d_b;
				}
				return a < b;
			}
			catch (...)
			{

			}
		}
	};


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

	
	template<typename T>
	void LoadDataAs(const H5::DataSet& dataset, LoaderInfo &loaderInfo, bool optimize_storage_size = false, bool allow_lossy_storage = false)
	{
		static_assert(sizeof(T) <= 4);
		H5Utils::MultiDimensionalData<T> mdd;
		
		if(!std::numeric_limits<T>::is_specialized)
		{
			// bfloat16
			H5Utils::MultiDimensionalData<float> mdd_float;
			
			H5Utils::read_multi_dimensional_data(dataset, mdd_float);
			{
				if (mdd_float.size.size() == 2)
				{
					mdd.size = mdd_float.size;
					mdd.data.resize(mdd.data.size());
					#pragma omp parallel for
					for (std::ptrdiff_t i = 0; i < mdd.data.size(); ++i)
						mdd.data[i] = mdd_float.data[i];
				}
			}
		}
		else
		{
			H5Utils::read_multi_dimensional_data(dataset, mdd);
		}

		if (mdd.size.size() == 2)
			

		if(optimize_storage_size)
		{
			auto sizeOfT = sizeof(T);
			auto minmax_pair = std::minmax_element(mdd.data.cbegin(), mdd.data.cend());
			T minVal = *(minmax_pair.first);
			T maxVal = *(minmax_pair.second);
			if (*(minmax_pair.first) < 0)
			{
				//signed
				if (H5Utils::in_range<std::int8_t>(minVal, maxVal))
				{
					if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(mdd.data))
					{
						mdd.data.clear();
						mdd.size.clear();
						//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
						LoadDataAs<std::int8_t>(dataset, loaderInfo);
						return;
					}
					else
					{
						if (allow_lossy_storage)
						{
							mdd.data.clear();
							mdd.size.clear();
							//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
							LoadDataAs<biovault::bfloat16_t>(dataset, loaderInfo);
							return;
						}
							
					}
				}
				else if ((sizeOfT > 2) && H5Utils::in_range<std::int16_t>(minVal, maxVal))
				{
					if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(mdd.data))
					{
						mdd.data.clear();
						mdd.size.clear();
						//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
						LoadDataAs<std::int16_t>(dataset, loaderInfo);
						return;
					}
					else
					{
						if (allow_lossy_storage)
						{
							mdd.data.clear();
							mdd.size.clear();
							//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
							LoadDataAs<biovault::bfloat16_t>(dataset, loaderInfo);
							return;
						}
					}
				}
			}
			else
			{
				//unsigned
				if (H5Utils::in_range<std::uint8_t>(minVal, maxVal))
				{
					if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(mdd.data))
					{
						mdd.data.clear();
						mdd.size.clear();
						//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
						LoadDataAs<std::uint8_t>(dataset, loaderInfo);
						return;
					}
					else
					{
						if (allow_lossy_storage)
						{
							mdd.data.clear();
							mdd.size.clear();
							//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
							LoadDataAs<biovault::bfloat16_t>(dataset, loaderInfo);
							return;
						}
					}
				}
				else if ((sizeOfT > 2) && (H5Utils::in_range<std::uint16_t>(minVal, maxVal)))
				{
					if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(mdd.data))
					{
						mdd.data.clear();
						mdd.size.clear();
						//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
						LoadDataAs<std::uint16_t>(dataset, loaderInfo);
						return;
					}
					else
					{
						if (allow_lossy_storage)
						{
							mdd.data.clear();
							mdd.size.clear();
							//reload the data since it will be moved later and even if it was copied later it would consume less memory (but more time)
							LoadDataAs<biovault::bfloat16_t>(dataset, loaderInfo);
							return;
						}
					}
				}
			}
		}
		
		if (mdd.size.size() == 2)
		{
			
			loaderInfo._pointsDataset->setDataElementType<T>(); // not sure this is needed since we move data below but it shouldn't hurt either
			loaderInfo._pointsDataset->setData(std::move(mdd.data), mdd.size[1]);
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
		}
	}
	void LoadData(const H5::DataSet& dataset, LoaderInfo &loaderInfo, int storageType)
	{
		if(storageType < 0) // use native format
		{
			H5::DataType datatype = dataset.getDataType();
			H5T_class_t class_type = datatype.getClass();
			 if (class_type == H5T_FLOAT)
			{
				LoadDataAs<float>(dataset, loaderInfo);
			}
			else if ((class_type == H5T_INTEGER) || (class_type == H5T_ENUM))
			{
				if(H5Tget_sign(class_type))
				{
					// signed
					switch(datatype.getSize())
					{
						case 1: LoadDataAs<std::int8_t>(dataset, loaderInfo); break;
						case 2: LoadDataAs<std::int16_t>(dataset, loaderInfo); break;
						//case 4: LoadDataAs<std::int32_t>(dataset, pointsDataset); break;
						default: LoadDataAs<float>(dataset, loaderInfo); break;
					}
				}
				else
				{
					// unsigned
					switch (datatype.getSize())
					{
						case 1: LoadDataAs<std::uint8_t>(dataset, loaderInfo); break;
						case 2: LoadDataAs<std::uint16_t>(dataset, loaderInfo); break;
						//case 4: LoadDataAs<std::uint32_t>(dataset, pointsDataset); break;
						default: LoadDataAs<float>(dataset, loaderInfo); break;
					}
				}
			}
		}
		else
		{
			PointData::ElementTypeSpecifier newTargetType = (PointData::ElementTypeSpecifier)storageType;
			switch (newTargetType)
			{
			case PointData::ElementTypeSpecifier::float32: LoadDataAs<float>(dataset, loaderInfo); break;
			case PointData::ElementTypeSpecifier::bfloat16: LoadDataAs<biovault::bfloat16_t>(dataset, loaderInfo); break;
			case PointData::ElementTypeSpecifier::int16: LoadDataAs<std::int16_t>(dataset, loaderInfo); break;
			case PointData::ElementTypeSpecifier::uint16: LoadDataAs<std::uint16_t>(dataset, loaderInfo); break;
			case PointData::ElementTypeSpecifier::int8: LoadDataAs<std::int8_t>(dataset, loaderInfo); break;
			case PointData::ElementTypeSpecifier::uint8: LoadDataAs<std::uint8_t>(dataset, loaderInfo); break;
			}
		}
	}

	
	

	template<typename T>
	void LoadDataAs(H5::Group& group, LoaderInfo &datasetInfo, bool optimize_storage_size = false, bool allow_lossy_storage = false)
	{
		static_assert(sizeof(T) <= 4);
		bool result = true;
		
		std::vector<T> data;
		std::vector<std::uint64_t> indices;
		std::vector<std::uint32_t> indptr;
		std::vector<biovault::bfloat16_t> bf16data;
		

		if(!std::numeric_limits<T>::is_specialized)
		{
			// bfloat16
			std::vector<float> float_data;
			result &= H5Utils::read_vector(group, "data", &float_data);
			data.resize(float_data.size());
			#pragma omp parallel for
			for (std::ptrdiff_t i = 0; i < data.size(); ++i)
				data[i] = float_data[i];
		}
		else 
		{
			H5Utils::read_vector(group, "data", &data);
		}
		
		int sizeOfT = sizeof(T);
		if(result && optimize_storage_size && (sizeOfT > 1))
		{
			auto minmax_pair = std::minmax_element(data.cbegin(), data.cend());
			T minVal = *(minmax_pair.first);
			T maxVal = *(minmax_pair.second);
			if( *(minmax_pair.first) < 0)
			{
				//signed
				if(H5Utils::in_range<std::int8_t>(minVal,maxVal))
				{
					if(H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(data))
					{
						data.clear();
						return LoadDataAs<std::int8_t>(group, datasetInfo);
						
					}
					
					
				}
				else if(sizeOfT > 2)
				{
					if (H5Utils::in_range<std::int16_t>(minVal,maxVal))
					{
						if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(data))
						{
							data.clear();
							return LoadDataAs<std::int16_t>(group, datasetInfo);
						}
							
					}

					
				}
			}
			else
			{
				//unsigned
				if (H5Utils::in_range<std::uint8_t>(minVal,maxVal))
				{
					if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(data))
					{
						data.clear();
						return LoadDataAs<std::uint8_t>(group, datasetInfo);
					}
					
				}
				else if (sizeOfT > 2)
				{
					if (H5Utils::in_range<std::uint16_t>(minVal,maxVal))
					{
						if (H5Utils::is_integer(minVal) && H5Utils::is_integer(maxVal) && H5Utils::contains_only_integers(data))
						{
							data.clear();
							return LoadDataAs<std::uint16_t>(group, datasetInfo);
						}
						
					}
				}
			}
			if (allow_lossy_storage)
			{
				bf16data.resize(data.size());
				#pragma omp parallel for
				for (std::ptrdiff_t i = 0; i < data.size(); ++i)
					bf16data[i] = data[i];
				data.clear();
			}
		}

		if (result)
			result &= H5Utils::read_vector(group, "indices", &indices);
		if (result)
			result &= H5Utils::read_vector(group, "indptr", &indptr);


		Dataset<Points> pointsDataset = datasetInfo._pointsDataset;
		auto selectedDimensionNames = datasetInfo._originalDimensionNames;
		std::vector<std::ptrdiff_t>& dimensionIndices = datasetInfo._selectedDimensionsLUT;
		std::vector<bool>& enabledDimensions = datasetInfo._enabledDimensions;
		{
			
			auto originalDimensionNames = datasetInfo._originalDimensionNames;
			const std::size_t nrOfOriginalDimensions = originalDimensionNames.size();
			std::size_t nrOfSelectedDimensions = 0;
			
			{
				Dataset<Points> tempDataset = mv::data().createDataset("Points", "temp");
				tempDataset->getDataHierarchyItem().setVisible(false);
				tempDataset->setData(std::vector<int8_t>(originalDimensionNames.size()), originalDimensionNames.size());
				tempDataset->setDimensionNames(originalDimensionNames);

				QDialog dialog(Application::getMainWindow());
				QGridLayout* layout = new QGridLayout;

				DimensionsPickerAction &dimensionPickerAction = tempDataset->getDimensionsPickerAction();;
				layout->addWidget(new QLabel("Select Dimensions:"));
				layout->addWidget(dimensionPickerAction.createWidget(Application::getMainWindow()));
				auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
				buttonBox->connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
				layout->addWidget(buttonBox, 3, 0, 1, 2);
				dialog.setLayout(layout);
				auto result = dialog.exec();
				if (result == 0)
				{
					datasetInfo = {};
					mv::data().removeDataset(tempDataset);
					mv::data().removeDataset(pointsDataset);
					return;
				}
				nrOfSelectedDimensions = dimensionPickerAction.getSelectedDimensions().size();
				enabledDimensions = dimensionPickerAction.getEnabledDimensions();
				mv::data().removeDataset(tempDataset);
			}
			

			if (nrOfSelectedDimensions == 0)
			{
				// nothing is selected
				datasetInfo = {};
				mv::data().removeDataset(pointsDataset);
				return;;
			} 
			
			
			if(nrOfSelectedDimensions < nrOfOriginalDimensions)
			{
				dimensionIndices.assign(nrOfOriginalDimensions, -1);
				std::vector<std::uint64_t> newDimensionIndex(nrOfOriginalDimensions,  std::numeric_limits<std::uint64_t>::max());
				selectedDimensionNames.resize(nrOfSelectedDimensions);
				std::ptrdiff_t newIndex = 0;
				for (std::ptrdiff_t i = 0; i < nrOfOriginalDimensions; ++i)
				{
					if (enabledDimensions[i])
					{
						selectedDimensionNames[newIndex] = originalDimensionNames[i];
						newDimensionIndex[i] = newIndex;
						dimensionIndices[i] = newIndex;
						++newIndex;
					}
				}

				// update data & indices so data can be ignored later on
				assert(data.size() == indices.size());
				#pragma omp parallel for
				for (std::ptrdiff_t i = 0; i < indices.size(); ++i)
				{
					auto oldColumnIndex = indices[i];
					auto newColumnIndex = dimensionIndices[oldColumnIndex];
					if(newColumnIndex < 0)
					{
						indices[i] = std::numeric_limits<std::uint64_t>::max();
						data[i] = 0;
					}
					else
					{
						indices[i] = static_cast<std::uint64_t>(newColumnIndex);
					}
					
				}
			} 	
		}




		if (result)
		{
			if(data.empty() && bf16data.size())
				pointsDataset->setDataElementType<biovault::bfloat16_t>();
			else
				pointsDataset->setDataElementType<T>();
			DataContainerInterface dci(pointsDataset);
			std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;
			std::uint64_t ysize = selectedDimensionNames.size();
			dci.resize(xsize, ysize);
			if (data.empty() && bf16data.size())
				dci.set_sparse_row_data(indices, indptr, bf16data, TRANSFORM::None());
			else
				dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());
			pointsDataset->setDimensionNames(selectedDimensionNames);
		}

		
	}

	void LoadData(H5::Group& group, LoaderInfo &datasetInfo, int storageType)
	{
		
		if (storageType < 0) // use native or optimized storage type		
		{

			H5::DataSet dataset = group.openDataSet("data");
			H5::DataType datatype = dataset.getDataType();
			H5T_class_t class_type = datatype.getClass();
			dataset.close();

			if (class_type == H5T_FLOAT)
			{
				LoadDataAs<float>(group, datasetInfo, storageType <= -2, storageType == -2);
			}
			else if ((class_type == H5T_INTEGER) || (class_type == H5T_ENUM))
			{
				if (H5Tget_sign(class_type))
				{
					// signed
					switch (datatype.getSize())
					{
					case 1: return LoadDataAs<std::int8_t>(group, datasetInfo, storageType <= -2, storageType == -2);
					case 2:return LoadDataAs<std::int16_t>(group, datasetInfo,  storageType <= -2, storageType <= -2);
				//	case 4: return LoadDataAs<std::int32_t>(group, datasetInfo, storageType <= -2, storageType <= -2);
					default: return LoadDataAs<float>(group, datasetInfo, storageType <= -2, storageType <= -2);
					}
				}
				else
				{
					// unsigned
					switch (datatype.getSize())
					{
					case 1: return LoadDataAs<std::uint8_t>(group, datasetInfo, storageType <= -2, storageType == -2);
					case 2: return LoadDataAs<std::uint16_t>(group, datasetInfo, storageType <= -2, storageType == -2);
				//	case 4: return LoadDataAs<std::uint32_t>(group, datasetInfo, storageType <= -2, storageType == -2);
					default: return LoadDataAs<float>(group, datasetInfo, storageType <= -2, storageType == -2);
					}
				}
			}
		}
		else
		{
			PointData::ElementTypeSpecifier newTargetType = (PointData::ElementTypeSpecifier)storageType;
			switch (newTargetType)
			{
			case PointData::ElementTypeSpecifier::float32: return LoadDataAs<float>(group, datasetInfo);
			case PointData::ElementTypeSpecifier::bfloat16: return LoadDataAs<biovault::bfloat16_t>(group, datasetInfo);
			case PointData::ElementTypeSpecifier::int16: return LoadDataAs<std::int16_t>(group, datasetInfo);
			case PointData::ElementTypeSpecifier::uint16: return LoadDataAs<std::uint16_t>(group, datasetInfo);
			case PointData::ElementTypeSpecifier::int8: return LoadDataAs<std::int8_t>(group, datasetInfo);
			case PointData::ElementTypeSpecifier::uint8: return LoadDataAs<std::uint8_t>(group, datasetInfo);
			}
		}
		
	}

	std::string LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result)
	{
		result.clear();
		std::map<std::string, std::vector<QVariant> > compoundMap;
		if (H5Utils::read_compound(dataset, compoundMap))
		{
			std::vector<std::string> indexNames(2);
			indexNames[0] = "index";
			indexNames[1] = "_index";
			for (std::size_t i = 0; i < 2; ++i)
			{
				std::string currentIndexName = indexNames[i];
				for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
				{
					if (component->first == currentIndexName)
					{
						std::size_t nrOfItems = component->second.size();
						result.resize(nrOfItems);
						for (std::size_t i = 0; i < nrOfItems; ++i)
							result[i] = component->second[i].toString();
						return currentIndexName;
					}
				}
			}
		}
		return std::string();
	}

	std::string LoadIndexStrings(H5::Group& group, std::vector<QString>& result)
	{
		/* order to look for is
		   -  value of _index attribute
		   -  _index dataset
		   -  index dataset
		*/
		std::vector<std::string> indexObjectNames;
		indexObjectNames.reserve(3);
		if (group.attrExists("_index"))
		{
			auto attribute = group.openAttribute("_index");
			std::string objectName;
			attribute.read(attribute.getDataType(), objectName);
			indexObjectNames.push_back(objectName);
		}
		indexObjectNames.push_back("_index");
		indexObjectNames.push_back("index");

		auto nrOfObjects = group.getNumObjs();
		for (std::size_t i = 0; i < indexObjectNames.size(); ++i)
		{
			std::string currentIndexName = indexObjectNames[i];
			for (auto go = 0; go < nrOfObjects; ++go)
			{
				std::string objectName = group.getObjnameByIdx(go);
				H5G_obj_t objectType = group.getObjTypeByIdx(go);

				if ((objectType == H5G_DATASET) && (objectName == currentIndexName))
				{
					H5::DataSet dataSet = group.openDataSet(objectName);

					H5Utils::read_vector_string(dataSet, result);
					return currentIndexName;
				}
			}
		}

		return std::string();
	}



	bool LoadSparseMatrix(H5::Group& group, LoaderInfo &loaderInfo)
	{
		auto nrOfObjects = group.getNumObjs();
		auto h5groupName = group.getObjName();

		bool containsSparseMatrix = false;
		if (nrOfObjects > 2)
		{
			H5Utils::VectorHolder data;
			H5Utils::VectorHolder indices;
			H5Utils::VectorHolder indptr;

			containsSparseMatrix = H5Utils::read_vector(group, "data", data);
			if (containsSparseMatrix)
				containsSparseMatrix &= H5Utils::read_vector(group, "indices", indices);
			if (containsSparseMatrix)
				containsSparseMatrix &= H5Utils::read_vector(group, "indptr", indptr);

			if (containsSparseMatrix)
			{
				std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;
				if (xsize == loaderInfo._pointsDataset->getNumPoints())
				{
					std::uint64_t ysize = indices.visit<std::uint64_t>([](auto& vec) { return *std::max_element(vec.cbegin(), vec.cend()); }) + 1;

					std::vector<QString> dimensionNames(ysize);
					for (std::size_t l = 0; l < ysize; ++l)
						dimensionNames[l] = QString::number(l);
					QString numericalDatasetName = QString(h5groupName.c_str()) /* + " (numerical)" */;
					while (numericalDatasetName[0] == '/')
						numericalDatasetName.remove(0, 1);
					while (numericalDatasetName[0] == '\\')
						numericalDatasetName.remove(0, 1);
					Dataset<Points> numericalDataset = mv::data().createDerivedDataset(numericalDatasetName, loaderInfo._pointsDataset); // core->addDataset("Points", numericalDatasetName, parent);
					numericalDataset->setProperty("Sample Names", loaderInfo._sampleNames);
					data.visit([&numericalDataset](auto& vec) {
						typedef typename std::decay_t<decltype(vec)> v;
						numericalDataset->setDataElementType<typename v::value_type>();
						});


					events().notifyDatasetAdded(numericalDataset);
					DataContainerInterface dci(numericalDataset);
					dci.resize(xsize, ysize);
					dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());


					numericalDataset->setDimensionNames(dimensionNames);

#ifdef MANIVAULT_API_Old
					events().notifyDatasetChanged(numericalDataset);
#endif
#ifdef MANIVAULT_API_New
					events().notifyDatasetDataChanged(numericalDataset);
#endif
					
				}
			}
		}

		return containsSparseMatrix;
	}

	bool LoadCategories(H5::Group& group, std::map<std::string, std::vector<QString>>& categories)
	{
		if (!group.exists("__categories"))
			return false;

		H5::Group categoriesGroup = group.openGroup("__categories");

		auto nrOfObjects = categoriesGroup.getNumObjs();
		for (auto go = 0; go < nrOfObjects; ++go)
		{
			std::string objectName1 = categoriesGroup.getObjnameByIdx(go);
			if (objectName1[0] == '\\')
				objectName1.erase(objectName1.begin());
			H5G_obj_t objectType1 = categoriesGroup.getObjTypeByIdx(go);

			if (objectType1 == H5G_DATASET)
			{
				H5::DataSet dataSet = categoriesGroup.openDataSet(objectName1);
				std::vector<QString> items;
				if (H5Utils::read_vector_string(dataSet, items))
					categories[objectName1] = items;
				else
				{
					H5Utils::VectorHolder numericalVector;
					if (H5Utils::read_vector(categoriesGroup, objectName1, numericalVector))
					{
						std::vector<QString> strings(numericalVector.size());
						numericalVector.visit([&strings](const auto& vec)
							{
								for (std::size_t i = 0; i < vec.size(); ++i)
									strings[i] = QString::number(vec[i]);
							});
						categories[objectName1] = strings;
					}

				}
			}
		}

		return (!categories.empty());
	}

	DataHierarchyItem* GetDerivedDataset(const QString& name, Dataset<Points>& pointsDataset)
	{
		const auto& children = pointsDataset->getDataHierarchyItem().getChildren();
		for (auto it = children.begin(); it != children.end(); ++it)
		{
#if defined(MANIVAULT_API_Old)
			if ((*it)->getGuiName() == name)
			{
				return *it;
			}

#elif defined(MANIVAULT_API_New)
			if ((*it)->getId() == name)
			{
				return *it;
			}

			if((*it)->getDataset()->getGuiName() == name)
		    {
				return *it;
		    }
#endif
		}
		return nullptr;
	}



	

	bool LoadCodedCategories(H5::Group& group, std::map<QString, std::vector<unsigned>>& result)
	{
		auto nrOfObjects = group.getNumObjs();
		if ((nrOfObjects == 2) && (group.getObjnameByIdx(0) == "categories") && (group.getObjnameByIdx(1) == "codes"))
		{
			H5G_obj_t objectTypeCategories = group.getObjTypeByIdx(0);
			H5G_obj_t objectTypeCodes = group.getObjTypeByIdx(1);
			H5::DataSet catDataset = group.openDataSet("categories");
			std::vector<QString> catValues;
			H5Utils::read_vector_string(catDataset, catValues);

			H5::DataSet codesDataset = group.openDataSet("codes");
			auto datasetClass = codesDataset.getDataType().getClass();

			std::vector<QString> categories;
			if (datasetClass == H5T_INTEGER)
			{
				std::vector<std::int64_t> values;

				if (H5Utils::read_vector(group, "codes", &values))
				{

					for (unsigned i = 0; i < values.size(); ++i)
					{
						std::int64_t value = values[i];
						if (value < catValues.size())
							result[catValues[value]].push_back(i);
						else
						{
							result.clear();
							return  false;
						}
					}
					return  true;
				}
			}
		}
		result.clear();
		return false;
	}



	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, LoaderInfo &loaderInfo, int storageType)
	{
		try
		{
			auto nrOfObjects = h5fILE->getNumObjs();

			std::size_t rows = 0;
			std::size_t columns = 0;
			// first read the main data
			for (auto fo = 0; fo < nrOfObjects; ++fo)
			{
				std::string objectName1 = h5fILE->getObjnameByIdx(fo);
				H5G_obj_t objectType1 = h5fILE->getObjTypeByIdx(fo);
				if (objectType1 == H5G_DATASET)
				{
					if (objectName1 == "X")
					{
						H5::DataSet dataset = h5fILE->openDataSet(objectName1);
						H5AD::LoadData(dataset, loaderInfo, storageType);
						break;

					}
				}
				else if (objectType1 == H5G_GROUP)
				{
					if (objectName1 == "X")
					{
						H5::Group group = h5fILE->openGroup(objectName1);
						H5AD::LoadData(group, loaderInfo, storageType);
						break;
					}
				}
			}
			if (!loaderInfo._pointsDataset.isValid())
			{
				return false;
			}
		}
		catch (const std::exception& e)
		{
			std::string mesg = e.what();
			int bp = 0;
			++bp;
			return false;
		}

		// set sample names
		if(loaderInfo._pointsDataset->getNumPoints() == loaderInfo._sampleNames.size())
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
		// set dimension names if not already set
		if(loaderInfo._pointsDataset->getDimensionNames().empty() && loaderInfo._pointsDataset->getNumDimensions() == loaderInfo._originalDimensionNames.size())
		{
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
		}
		return true;
	}


	template<typename numericMetaDataType>
	void LoadSampleNamesAndMetaData(H5::DataSet& dataset, LoaderInfo &loaderInfo, int storage_type)
	{
		static_assert(std::is_same<numericMetaDataType, float>::value, "");
		std::string h5datasetName = dataset.getObjName();
		if (h5datasetName[0] == '/')
			h5datasetName.erase(h5datasetName.begin());
		std::map<std::string, std::vector<QVariant> > compoundMap;

		if (H5Utils::read_compound(dataset, compoundMap))
		{
			std::size_t nrOfComponents = compoundMap.size();
			std::vector<numericMetaDataType> numericalMetaData;
			std::vector<QString> numericalMetaDataDimensionNames;

			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				const std::size_t nrOfSamples = component->second.size();
				if (nrOfSamples == loaderInfo._pointsDataset->getNumPoints())
				{
					if (component->first != "index")
					{
						std::map<QString, std::vector<unsigned>> indices;


						bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value
						std::vector<numericMetaDataType> values;
						values.reserve(loaderInfo._pointsDataset->getNumPoints());
						for (std::size_t s = 0; s < nrOfSamples; ++s)
						{
							QString item = component->second[s].toString();
							if (currentMetaDataIsNumerical)
							{
								if (H5Utils::is_number(item))
								{
									values.push_back(component->second[s].toFloat());
								}
								else
								{
									values.clear();
									currentMetaDataIsNumerical = false;
									// add former labels as caterogical as well.
									for (std::size_t i = 0; i <= s; ++i)
									{
										QString value = component->second[i].toString();
										indices[value].push_back(i);
									}
								}
							}
							else
							{
								indices[item].push_back(s);
							}
						}
						if (currentMetaDataIsNumerical)
						{
							numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
							numericalMetaDataDimensionNames.push_back(component->first.c_str());
						}
						else
						{
							QString prefix = h5datasetName.c_str() + QString("\\");
							H5Utils::addClusterMetaData(indices, component->first.c_str(), loaderInfo._pointsDataset, std::map<QString, QColor>(), prefix);
						}

					}
				}

			}

			Dataset<Points> numericalMetaDataset = H5Utils::addNumericalMetaData(numericalMetaData, numericalMetaDataDimensionNames, true, loaderInfo._pointsDataset, h5datasetName.c_str());
			numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
		}
	}


	
	template<typename numericalMetaDataType>
	void LoadSampleNamesAndMetaData(H5::Group& group, LoaderInfo& loaderInfo, int storage_type)
	{

		static_assert(std::is_same<numericalMetaDataType, float>::value, "");
		auto nrOfObjects = group.getNumObjs();

		std::filesystem::path path(group.getObjName());
		std::string h5GroupName = path.filename().string();
		if (LoadSparseMatrix(group, loaderInfo))
		{
			return;
		}
			


		std::vector<numericalMetaDataType> numericalMetaData;
		std::size_t nrOfNumericalMetaData = 0;
		std::vector<QString> numericalMetaDataDimensionNames;
		std::size_t nrOfRows = loaderInfo._pointsDataset->getNumPoints();
		std::map<std::string, std::vector<QString>> categories;

		bool categoriesLoaded = LoadCategories(group, categories);

		std::map<QString, std::vector<unsigned>> codedCategories;
		if (LoadCodedCategories(group, codedCategories))
		{
			std::size_t count = 0;
			for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
				count += it->second.size();
			if (count != nrOfRows)
				std::cout << "WARNING: " << "not all datapoints are accounted for" << std::endl;

			std::size_t posFound = h5GroupName.find("_color");
			if (posFound == std::string::npos)
			{
				if (count == nrOfRows)
				{
					H5Utils::addClusterMetaData(codedCategories, h5GroupName.c_str(), loaderInfo._pointsDataset);
				}
			}
			else
			{
				bool itemsAreColors = true;
				for (auto codedCat = codedCategories.cbegin(); codedCat != codedCategories.cend(); ++codedCat)
				{
					if (codedCat->first != "NA")
					{
						if (!QColor::isValidColor(codedCat->first))
						{

							itemsAreColors = false;
							break;
						}
					}
				}
				if (itemsAreColors)
				{

					
					int options = 2;
					for(int option =0; option < options; ++option)
					{
						QString datasetNameToFind = h5GroupName.c_str();
						datasetNameToFind.resize(posFound);
						if(option ==0)
							datasetNameToFind += "_label";
						if (datasetNameToFind[0] == '/')
							datasetNameToFind.remove(0, 1);
						DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, loaderInfo._pointsDataset);
						if (foundDataset)
						{
							foundDataset->setLocked(true);
							if (foundDataset->getDataType() == DataType("Clusters"))
							{
								//std::cout << " --- " << datasetNameToFind.toStdString() << " --- " << std::endl;
								auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
								int unchangedClusterColors = 0;
								//#pragma  omp parallel for schedule(dynamic,1)
								for (long long i = 0; i < clusters.size(); ++i)
								{
									const auto& clusterIndices = clusters[i].getIndices();
									std::set<uint32_t> clusterIndicesSet(clusterIndices.cbegin(), clusterIndices.cend());
									bool clusterColorChanged = false;
									for (auto codedCat = codedCategories.cbegin(); codedCat != codedCategories.cend(); ++codedCat)
									{
										const auto& indices = codedCat->second;
										if (indices.size() == clusterIndices.size())
										{

											std::set<uint32_t> indicesSet(indices.cbegin(), indices.cend());
											if (indicesSet == clusterIndicesSet)
											{
												bool subset = (indices.size() > clusterIndices.size());
												QString newColor = QColor::isValidColor(codedCat->first) ? codedCat->first : "#000000";

												clusters[i].setColor(newColor);
												clusterColorChanged = true;
												break;
											}
										}
									}
									if (!clusterColorChanged)
									{
										for (auto codedCat = codedCategories.cbegin(); codedCat != codedCategories.cend(); ++codedCat)
										{
											const auto& indices = codedCat->second;
											if (indices.size() > clusterIndices.size())
											{
												std::set<uint32_t> indicesSet(indices.cbegin(), indices.cend());
												if (std::includes(indicesSet.cbegin(), indicesSet.cend(), clusterIndicesSet.cbegin(), clusterIndicesSet.cend()))
												{
													bool subset = (indices.size() > clusterIndices.size());
													QString newColor = QColor::isValidColor(codedCat->first) ? codedCat->first : "#000000";
													clusters[i].setColor(newColor);
													clusterColorChanged = true;
													break;
												}
											}
										}
									}
									if (!clusterColorChanged)
									{
										++unchangedClusterColors;
										clusters[i].setColor("#000000");
										//	std::cout << "cluster " << clusters[i].getName().toStdString() << " color " << clusters[i].getColor().name().toStdString() << " was not changed" << std::endl;
									}
								}
								if (unchangedClusterColors < clusters.size())
								{
#if defined(MANIVAULT_API_Old)
									events().notifyDatasetChanged(foundDataset->getDataset());
#elif defined(MANIVAULT_API_New)
									events().notifyDatasetDataChanged(foundDataset->getDataset());
#endif
								}
									

								if (unchangedClusterColors)
								{
									// if not all cluster colors where changed, we will add the color as well so at least it's visible.
									std::map<QString, QColor> colors;
									for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
										colors[it->first] = QColor(it->first);


									H5Utils::addClusterMetaData(codedCategories, h5GroupName.c_str(), loaderInfo._pointsDataset, colors);
									option = options;
								}
							}
							foundDataset->setLocked(false);
						}
					}

					
				}
				else
				{
					H5Utils::addClusterMetaData(codedCategories, h5GroupName.c_str(), loaderInfo._pointsDataset);
				}
			}
		}
		else
		{
			// first do the basics
			for (int load_colors = 0; load_colors < 2; ++load_colors)
			{

				for (auto go = 0; go < nrOfObjects; ++go)
				{
					std::string objectName1 = group.getObjnameByIdx(go);
					std::size_t posFound = objectName1.find("_color");
					if (load_colors == 0)
					{
						if (posFound != std::string::npos)
							continue;
					}
					else
					{
						if (posFound == std::string::npos)
							continue;
					}
					if (objectName1[0] == '\\')
						objectName1.erase(objectName1.begin());
					H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

					if (objectType1 == H5G_DATASET)
					{
						H5::DataSet dataSet = group.openDataSet(objectName1);
						if (!((objectName1 == "index") || (objectName1 == "_index")))
						{

							if (categories.find(objectName1) != categories.end())
							{
								if (dataSet.attrExists("categories"))
								{
									std::vector<uint64_t> index;
									const std::vector<QString>& labels = categories[objectName1];
									if (H5Utils::read_vector(group, objectName1, &index))
									{
										std::map<QString, std::vector<unsigned>> indices;
										if (index.size() == loaderInfo._pointsDataset->getNumPoints())
										{
											for (unsigned i = 0; i < index.size(); ++i)
											{
												indices[labels[index[i]]].push_back((i));
											}
											for (auto indices_iterator = indices.begin(); indices_iterator != indices.end(); ++indices_iterator)
											{
												std::sort(indices_iterator->second.begin(), indices_iterator->second.end());
												auto ignore = std::unique(indices_iterator->second.begin(), indices_iterator->second.end());
												assert(indices_iterator->second.size() > 0);
											}
											if (load_colors == 0)
												H5Utils::addClusterMetaData(indices, dataSet.getObjName().c_str(), loaderInfo._pointsDataset);
											else
											{
												const std::vector<QString>& items = categories[objectName1];
												bool itemsAreColors = true;
												for (std::size_t i = 0; i < items.size(); ++i)
												{
													if (items[i] != "NA")
													{
														if (!QColor::isValidColor(items[i]))
														{

															itemsAreColors = false;
															break;
														}
													}
												}
												if (itemsAreColors)
												{
													QString datasetNameToFind = h5GroupName.c_str();
													datasetNameToFind += "/";
													

													const int options = 2;
													for(int option=0; option < options; ++option)
													{
														QString temp = objectName1.c_str();

														temp.resize(posFound);
														if(option==0)
															temp += "_label";
														datasetNameToFind += temp;
														if (datasetNameToFind[0] == '/')
															datasetNameToFind.remove(0, 1);
														std::cout << "matching colors for " << datasetNameToFind.toStdString() << "  " << std::endl;
														DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, loaderInfo._pointsDataset);
														if (foundDataset)
														{
															foundDataset->setLocked(true);
															if (foundDataset->getDataType() == DataType("Clusters"))
															{

																auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
																int unchangedClusterColors = 0;
																//#pragma omp parallel for schedule(dynamic,1)
																for (long long i = 0; i < clusters.size(); ++i)
																{
																	const auto& clusterIndices = clusters[i].getIndices();
																	assert(std::is_sorted(clusterIndices.cbegin(), clusterIndices.cend()));
																	bool clusterColorChanged = false;
																	for (auto indices_iterator = indices.cbegin(); indices_iterator != indices.cend(); ++indices_iterator)
																	{
																		const auto& colorIndices = indices_iterator->second;
																		assert(std::is_sorted(colorIndices.cbegin(), colorIndices.cend()));
																		if (clusterIndices == colorIndices)
																		{
																			QString newColor = QColor::isValidColor(indices_iterator->first) ? indices_iterator->first : "#000000";
																			clusters[i].setColor(newColor);

																			clusterColorChanged = true;
																			break;
																		}
																	}
																	if (!clusterColorChanged)
																	{
																		std::cout << "no exact match found for " << clusters[i].getName().toStdString() << std::endl;
																		for (auto indices_iterator = indices.cbegin(); indices_iterator != indices.cend(); ++indices_iterator)
																		{
																			const auto& colorIndices = indices_iterator->second;
																			assert(std::is_sorted(colorIndices.cbegin(), colorIndices.cend()));
																			if (colorIndices.size() > clusterIndices.size())
																			{

																				if (std::includes(colorIndices.cbegin(), colorIndices.cend(), clusterIndices.cbegin(), clusterIndices.cend()))
																				{
																					QString newColor = QColor::isValidColor(indices_iterator->first) ? indices_iterator->first : "#000000";
																					std::cout << "match found for " << clusters[i].getName().toStdString() << " color = " << newColor.toStdString() << " (" << colorIndices.size() << " vs " << clusterIndices.size() << ")" << std::endl;
																					clusters[i].setColor(newColor);

																					clusterColorChanged = true;
																					break;
																				}
																			}
																		}
																	}

																	if (!clusterColorChanged)
																	{
																		++unchangedClusterColors;
																		std::cout << "no  match found for " << clusters[i].getName().toStdString() << std::endl;
																		clusters[i].setColor("#000000");
																	}

																}
																if (unchangedClusterColors < clusters.size())
																{
#if defined(MANIVAULT_API_Old)
																	events().notifyDatasetChanged(foundDataset->getDataset());
#elif defined(MANIVAULT_API_New)
																	events().notifyDatasetDataChanged(foundDataset->getDataset());
#endif
																}
																	

																if (unchangedClusterColors)
																{
																	// if not all cluster colors where changed, we will add the color as well so at least it's visible.
																	std::map<QString, QColor> colors;
																	for (auto it = indices.cbegin(); it != indices.cend(); ++it)
																		colors[it->first] = QColor(it->first);

																	H5Utils::addClusterMetaData(indices, dataSet.getObjName().c_str(), loaderInfo._pointsDataset, colors);

																}
															}
															foundDataset->setLocked(false);
														}
													}
													

												}
											}
										}
									}
								}

							}
							else
							{
								auto datasetClass = dataSet.getDataType().getClass();

								if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_FLOAT) || (datasetClass == H5T_ENUM))
								{
									std::vector<float> values;
									if (H5Utils::read_vector(group, objectName1, &values))
									{
										// 1 dimensional
										if (values.size() == loaderInfo._pointsDataset->getNumPoints())
										{
											numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
											numericalMetaDataDimensionNames.push_back(dataSet.getObjName().c_str());
										}
									}
									else
									{
										// multi-dimensiona,  only 2 supported for now
										H5Utils::MultiDimensionalData<float> mdd;

										if (H5Utils::read_multi_dimensional_data(dataSet, mdd))
										{
											if (mdd.size.size() == 2)
											{
												if (mdd.size[0] == loaderInfo._pointsDataset->getNumPoints())
												{

													QString baseString = dataSet.getObjName().c_str();
													std::vector<QString> dimensionNames(mdd.size[1]);
													for (std::size_t l = 0; l < mdd.size[1]; ++l)
													{
														dimensionNames[l] = QString::number(l + 1);
													}
													mv::Dataset<Points> numericalMetaDataset = H5Utils::addNumericalMetaData(mdd.data, dimensionNames, false, loaderInfo._pointsDataset, baseString);
													numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
												}
											}
										}
									}

								}
								else if (datasetClass == H5T_COMPOUND)
								{
									std::map<std::string, std::vector<QVariant> >result;
									if (H5Utils::read_compound(dataSet, result))
									{
										int i = 0;
										++i;
									}

								}
								else // try to read as strings for now
								{

									std::map<QString, std::vector<unsigned>> indices;
									std::vector<QString> items;
									H5Utils::read_vector_string(dataSet, items);
									if (items.size() == loaderInfo._pointsDataset->getNumPoints())
									{
										for (unsigned i = 0; i < items.size(); ++i)
										{
											indices[items[i]].push_back(i);
										}
										H5Utils::addClusterMetaData(indices, dataSet.getObjName().c_str(), loaderInfo._pointsDataset);
									}
									else
									{
										bool itemsAreColors = true;
										for (std::size_t i = 0; i < items.size(); ++i)
										{
											if (items[i] != "NA")
											{
												if (!QColor::isValidColor(items[i]))
												{

													itemsAreColors = false;
													break;
												}
											}
										}
										if (itemsAreColors)
										{
											std::size_t posFound = posFound = objectName1.find("_color");
											if (posFound != std::string::npos)
											{
												QString datasetNameToFind;
												QString temp = objectName1.c_str();
												temp.resize(posFound);
												datasetNameToFind = QString("obs/") + temp;

												DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, loaderInfo._pointsDataset);
												if (foundDataset)
												{
													if (foundDataset->getDataType() == DataType("Clusters"))
													{
														auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
														if (clusters.size() == items.size())
														{
															for (std::size_t i = 0; i < clusters.size(); ++i)
																clusters[i].setColor(items[i]);

															#if defined(MANIVAULT_API_Old)
																events().notifyDatasetChanged(foundDataset->getDataset());
#elif defined(MANIVAULT_API_New)
															events().notifyDatasetDataChanged(foundDataset->getDataset());
#endif
														}
													}
												}
											}

										}

									}
								}
							}

						}
					}
					else if (objectType1 == H5G_GROUP)
					{
						H5::Group group2 = group.openGroup(objectName1);
						LoadSampleNamesAndMetaData<numericalMetaDataType>(group2, loaderInfo, storage_type);
					}
				}


			}
		}



		if (numericalMetaDataDimensionNames.size())
		{
			QString numericalMetaDataString;
			if (numericalMetaDataDimensionNames.size() == 1)
				numericalMetaDataString = numericalMetaDataDimensionNames[0];
			else 
				numericalMetaDataString = QString("Numerical Data (") + QString(h5GroupName.c_str()) + QString(")");
			Dataset<Points> numericalMetaDataset = H5Utils::addNumericalMetaData(numericalMetaData, numericalMetaDataDimensionNames, true, loaderInfo._pointsDataset, numericalMetaDataString);
			numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
		}
	}

	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, LoaderInfo &loaderInfo, int storage_type)
	{
		 H5AD::LoadSampleNamesAndMetaData<float>(dataset, loaderInfo, storage_type);
	}
	


	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, LoaderInfo& loaderInfo, int storage_type)
	{
		 H5AD::LoadSampleNamesAndMetaData<float>(group, loaderInfo, storage_type);
	}
	

}// namespace
