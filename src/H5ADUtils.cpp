#include "H5ADUtils.h"
#include "DataContainerInterface.h"

namespace H5AD
{


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





	void LoadData(const H5::DataSet& dataset, Dataset<Points> pointsDataset)
	{

		H5Utils::MultiDimensionalData<float> mdd;
		if (H5Utils::read_multi_dimensional_data(dataset, mdd, H5::PredType::NATIVE_FLOAT))
		{

			if (mdd.size.size() == 2)
			{
				pointsDataset->setData(std::move(mdd.data), mdd.size[1]);
			}
		}
	}

	void LoadData(H5::Group& group, Dataset<Points>& pointsDataset)
	{
		bool result = true;
		std::vector<float> data;
		std::vector<std::uint64_t> indices;
		std::vector<std::uint32_t> indptr;


		result &= H5Utils::read_vector(group, "data", &data, H5::PredType::NATIVE_FLOAT);
		if (result)
			result &= H5Utils::read_vector(group, "indices", &indices, H5::PredType::NATIVE_UINT64);
		if (result)
			result &= H5Utils::read_vector(group, "indptr", &indptr, H5::PredType::NATIVE_UINT32);

		if (result)
		{
			DataContainerInterface dci(pointsDataset);
			std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;
			std::uint64_t ysize = *std::max_element(indices.cbegin(), indices.cend()) + 1;
			dci.resize(xsize, ysize);
			dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());
		}
	}

	void LoadIndexStrings(H5::DataSet& dataset, std::vector<QString>& result)
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
						return;
					}
				}
			}
		}

	}

	void LoadIndexStrings(H5::Group& group, std::vector<QString>& result)
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
			std::string currentIndexObjectName = indexObjectNames[i];
			for (auto go = 0; go < nrOfObjects; ++go)
			{
				std::string objectName = group.getObjnameByIdx(go);
				H5G_obj_t objectType = group.getObjTypeByIdx(go);

				if ((objectType == H5G_DATASET) && (objectName == currentIndexObjectName))
				{
					H5::DataSet dataSet = group.openDataSet(objectName);

					H5Utils::read_vector_string(dataSet, result);
					return;
				}
			}
		}


	}



	bool LoadSparseMatrix(H5::Group& group, Dataset<Points>& pointsDataset, CoreInterface* _core)
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
				if (xsize == pointsDataset->getNumPoints())
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
					Dataset<Points> numericalDataset = _core->createDerivedDataset(numericalDatasetName, pointsDataset); // core->addDataset("Points", numericalDatasetName, parent);

					data.visit([&numericalDataset](auto& vec) {
						typedef typename std::decay_t<decltype(vec)> v;
						numericalDataset->setDataElementType<typename v::value_type>();
						});


					events().notifyDatasetAdded(numericalDataset);
					DataContainerInterface dci(numericalDataset);
					dci.resize(xsize, ysize);
					dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());


					numericalDataset->setDimensionNames(dimensionNames);
					events().notifyDatasetChanged(numericalDataset);
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
			if ((*it)->getGuiName() == name)
			{
				return *it;
			}
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

				if (H5Utils::read_vector(group, "codes", &values, H5::PredType::NATIVE_INT64))
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



	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, Dataset<Points> pointsDataset)
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
						H5AD::LoadData(dataset, pointsDataset);
						break;

					}
				}
				else if (objectType1 == H5G_GROUP)
				{
					if (objectName1 == "X")
					{
						H5::Group group = h5fILE->openGroup(objectName1);
						H5AD::LoadData(group, pointsDataset);
						break;
					}
				}
			}
			if (!pointsDataset.isValid())
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
		return true;
	}

}// namespace
