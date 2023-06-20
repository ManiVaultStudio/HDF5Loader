#include "H5ADUtils.h"
#include "DataContainerInterface.h"

namespace H5AD
{
	using namespace  hdps;

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


	template<typename numericMetaDataType>
	void LoadSampleNamesAndMetaData(H5::DataSet& dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core)
	{

		std::string h5datasetName = dataset.getObjName();
		if (h5datasetName[0] == '/')
			h5datasetName.erase(h5datasetName.cbegin());
		std::map<std::string, std::vector<QVariant> > compoundMap;

		if (H5Utils::read_compound(dataset, compoundMap))
		{
			std::size_t nrOfComponents = compoundMap.size();
			std::vector<numericMetaDataType> numericalMetaData;
			std::vector<QString> numericalMetaDataDimensionNames;

			for (auto component = compoundMap.cbegin(); component != compoundMap.cend(); ++component)
			{
				const std::size_t nrOfSamples = component->second.size();
				if (nrOfSamples == pointsDataset->getNumPoints())
				{
					if (component->first != "index")
					{
						std::map<QString, std::vector<unsigned>> indices;


						bool currentMetaDataIsNumerical = true; // we start assuming it's a numerical value
						std::vector<numericMetaDataType> values;
						values.reserve(pointsDataset->getNumPoints());
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
							H5Utils::addClusterMetaData(_core, indices, component->first.c_str(), pointsDataset, std::map<QString, QColor>(), prefix);
						}

					}
				}

			}

			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset, h5datasetName.c_str());
		}
	}


	template<typename numericalMetaDataType>
	void LoadSampleNamesAndMetaData(H5::Group& group, Dataset<Points>  pointsDataset, hdps::CoreInterface* _core)
	{

		auto nrOfObjects = group.getNumObjs();
		auto h5groupName = group.getObjName();

		if (LoadSparseMatrix(group, pointsDataset, _core))
			return;


		std::vector<numericalMetaDataType> numericalMetaData;
		std::size_t nrOfNumericalMetaData = 0;
		std::vector<QString> numericalMetaDataDimensionNames;
		std::size_t nrOfRows = pointsDataset->getNumPoints();
		std::map<std::string, std::vector<QString>> categories;

		bool categoriesLoaded = LoadCategories(group, categories);

		std::map<QString, std::vector<unsigned>> codedCategories;
		if (LoadCodedCategories(group, codedCategories))
		{
			std::size_t count = 0;
			for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
				count += it->second.size();
			if (count != pointsDataset->getNumPoints())
				std::cout << "WARNING: " << "not all datapoints are accounted for" << std::endl;

			std::size_t posFound = h5groupName.find("_color");
			if (posFound == std::string::npos)
			{
				if (count == pointsDataset->getNumPoints())
				{
					H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset);
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

					QString datasetNameToFind = h5groupName.c_str();
					datasetNameToFind.resize(posFound);
					datasetNameToFind += "_label";
					if (datasetNameToFind[0] == '/')
						datasetNameToFind.remove(0, 1);
					DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, pointsDataset);
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
								events().notifyDatasetChanged(foundDataset->getDataset());

							if (unchangedClusterColors)
							{
								// if not all cluster colors where changed, we will add the color as well so at least it's visible.
								std::map<QString, QColor> colors;
								for (auto it = codedCategories.cbegin(); it != codedCategories.cend(); ++it)
									colors[it->first] = QColor(it->first);


								H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset, colors);
							}
						}
						foundDataset->setLocked(false);
					}
				}
				else
				{
					H5Utils::addClusterMetaData(_core, codedCategories, h5groupName.c_str(), pointsDataset);
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
									if (H5Utils::read_vector(group, objectName1, &index, H5::PredType::NATIVE_UINT64))
									{
										std::map<QString, std::vector<unsigned>> indices;
										if (index.size() == pointsDataset->getNumPoints())
										{
											for (unsigned i = 0; i < index.size(); ++i)
											{
												indices[labels[index[i]]].push_back((i));
											}
											for (auto indices_iterator = indices.begin(); indices_iterator != indices.end(); ++indices_iterator)
											{
												std::sort(indices_iterator->second.begin(), indices_iterator->second.end());
											std:unique(indices_iterator->second.begin(), indices_iterator->second.end());
												assert(indices_iterator->second.size() > 0);
											}
											if (load_colors == 0)
												H5Utils::addClusterMetaData(_core, indices, dataSet.getObjName().c_str(), pointsDataset);
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
													QString datasetNameToFind = h5groupName.c_str();
													datasetNameToFind += "/";
													QString temp = objectName1.c_str();

													temp.resize(posFound);
													temp += "_label";
													datasetNameToFind += temp;
													if (datasetNameToFind[0] == '/')
														datasetNameToFind.remove(0, 1);
													std::cout << "matching colors for " << datasetNameToFind.toStdString() << "  " << std::endl;
													DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, pointsDataset);
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
																events().notifyDatasetChanged(foundDataset->getDataset());

															if (unchangedClusterColors)
															{
																// if not all cluster colors where changed, we will add the color as well so at least it's visible.
																std::map<QString, QColor> colors;
																for (auto it = indices.cbegin(); it != indices.cend(); ++it)
																	colors[it->first] = QColor(it->first);

																H5Utils::addClusterMetaData(_core, indices, dataSet.getObjName().c_str(), pointsDataset, colors);

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
							else
							{
								auto datasetClass = dataSet.getDataType().getClass();

								if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_FLOAT) || (datasetClass == H5T_ENUM))
								{
									std::vector<float> values;
									if (H5Utils::read_vector(group, objectName1, &values, H5::PredType::NATIVE_FLOAT))
									{
										// 1 dimensional
										if (values.size() == pointsDataset->getNumPoints())
										{
											numericalMetaData.insert(numericalMetaData.end(), values.cbegin(), values.cend());
											numericalMetaDataDimensionNames.push_back(dataSet.getObjName().c_str());
										}
									}
									else
									{
										// multi-dimensiona,  only 2 supported for now
										H5Utils::MultiDimensionalData<float> mdd;

										if (H5Utils::read_multi_dimensional_data(dataSet, mdd, H5::PredType::NATIVE_FLOAT))
										{
											if (mdd.size.size() == 2)
											{
												if (mdd.size[0] == pointsDataset->getNumPoints())
												{

													QString baseString = dataSet.getObjName().c_str();
													std::vector<QString> dimensionNames(mdd.size[1]);
													for (std::size_t l = 0; l < mdd.size[1]; ++l)
													{
														dimensionNames[l] = QString::number(l + 1);
													}
													H5Utils::addNumericalMetaData(_core, mdd.data, dimensionNames, false, pointsDataset, baseString);
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
									if (items.size() == pointsDataset->getNumPoints())
									{
										for (unsigned i = 0; i < items.size(); ++i)
										{
											indices[items[i]].push_back(i);
										}
										H5Utils::addClusterMetaData(_core, indices, dataSet.getObjName().c_str(), pointsDataset);
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
											std::size_t posFound = posFound = objectName1.find("_colors");
											if (posFound != std::string::npos)
											{
												QString datasetNameToFind;
												QString temp = objectName1.c_str();
												temp.resize(posFound);
												datasetNameToFind = QString("obs/") + temp;

												DataHierarchyItem* foundDataset = GetDerivedDataset(datasetNameToFind, pointsDataset);
												if (foundDataset)
												{
													if (foundDataset->getDataType() == DataType("Clusters"))
													{
														auto& clusters = foundDataset->getDataset<Clusters>()->getClusters();
														if (clusters.size() == items.size())
														{
															for (std::size_t i = 0; i < clusters.size(); ++i)
																clusters[i].setColor(items[i]);

															events().notifyDatasetChanged(foundDataset->getDataset());
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
						LoadSampleNamesAndMetaData<numericalMetaDataType>(group2, pointsDataset, _core);
					}
				}


			}
		}



		if (numericalMetaDataDimensionNames.size())
		{
			H5Utils::addNumericalMetaData(_core, numericalMetaData, numericalMetaDataDimensionNames, true, pointsDataset, h5groupName.c_str());
		}
	}

	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core)
	{
		H5AD::LoadSampleNamesAndMetaData<float>(dataset, pointsDataset, _core);
	}
	void LoadSampleNamesAndMetaDataBFloat16(H5::DataSet& dataset, Dataset<Points> pointsDataset, hdps::CoreInterface* _core)
	{
		LoadSampleNamesAndMetaData<biovault::bfloat16_t>(dataset, pointsDataset, _core);
	}


	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, Dataset<Points>  pointsDataset, hdps::CoreInterface* _core)
	{
		LoadSampleNamesAndMetaData<float>(group, pointsDataset, _core);
	}
	void LoadSampleNamesAndMetaDataBFloat16(H5::Group& group, Dataset<Points>  pointsDataset, hdps::CoreInterface* _core)
	{
		LoadSampleNamesAndMetaData<biovault::bfloat16_t>(group, pointsDataset, _core);
	}

}// namespace
