#include "TreeModel.h"
#include "H5Utils.h"

#include <set>
#include <QString>
#include <util/StyledIcon.h>
#include <tuple>
#include <QString> // Include for QString
#include <H5Cpp.h> // HDF5 C++ API header


namespace H5Utils
{

	struct TooltipInfo
	{
		QString topText;
		QString dataDescription;
		std::vector<std::size_t> dimensions;
		QString bottomText;

		TooltipInfo() = default;

		TooltipInfo(const std::vector<std::size_t>& dim, const QString& description, const QString top = "", const QString bottom = "")
			: topText(top) 
			, dataDescription(description)
			, dimensions(dim)
			, bottomText(bottom)
		{
			
		}
	};

	// forward declaration
	bool addHdf5ObjectTo(const H5::H5Object& obj, hsize_t idx, QStandardItem* parentItem, TreeModel* model = nullptr);
	
	

	void checkParent(QStandardItem* parent, Qt::CheckState checkState)
	{
		if (parent == nullptr)
			return;
		if (checkState == Qt::PartiallyChecked)
			parent->setCheckState(Qt::PartiallyChecked);
		else
		{
			Qt::CheckState stateToCheck = checkState;

			auto rowCount = parent->rowCount();
			bool allChildrenHaveTheSameCheckState = true;
			for (std::size_t i = 0; i < rowCount; ++i)
			{
				if (parent->child(i, 0)->checkState() != stateToCheck)
				{
					parent->setCheckState(Qt::PartiallyChecked);
					allChildrenHaveTheSameCheckState = false;
					break;
				}
			}
			if (allChildrenHaveTheSameCheckState)
				parent->setCheckState(stateToCheck);
		}
	}

	// Recursive function to check all children
	void checkAllChildren(QStandardItem* parentItem, Qt::CheckState checkState) {
		if (!parentItem) {
			return;
		}

		// Set the check state for the current item (optional, depending on your logic)
		// If you only want to set children, remove this line or add a condition.
		// parentItem->setCheckState(checkState);

		for (int i = 0; i < parentItem->rowCount(); ++i) {
			QStandardItem* childItem = parentItem->child(i);
			if (childItem) {
				childItem->setCheckState(checkState); // Set the check state for the child
				if (childItem->hasChildren()) {
					checkAllChildren(childItem, checkState); // Recurse for grand-children
				}
			}
		}
	}



	enum class TreeItemType { Unknown, Group, GeneralDataset, PointDataset, ClusterDataset, CompoundDatset };
	

	QString formatBytes1024_QString(hsize_t bytes) {
		const QStringList units = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
		double value = static_cast<double>(bytes);
		int unitIndex = 0;

		while (value >= 1024 && unitIndex < units.size() - 1) {
			value /= 1024;
			unitIndex++;
		}

		if (unitIndex == 0) { // For bytes, no decimal places
			return QString("%1 %2").arg(static_cast<long long>(value)).arg(units[unitIndex]);
		}
		else {
			return QString("%1 %2").arg(value, 0, 'f', 2).arg(units[unitIndex]);
		}
	}

	// Function to get dataspace dimensions as std::vector<hsize_t>
	std::vector<hsize_t> get_dataspace_dimensions(const H5::DataSpace& dataSpace) {

		
		H5S_class_t dataSpaceType = dataSpace.getSimpleExtentType();
		// Get the rank (number of dimensions)
		int rank = dataSpace.getSimpleExtentNdims();

		// If it's a scalar or null dataspace, rank might be 0 or -1.
		// For scalar, conceptually it has 1 element, no dimensions in the array sense.
		// For null, it has no elements.
		if (rank <= 0) {
			// You might return an empty vector, or a vector containing {1} for scalar
			// depending on how you want to represent it.
			// For simplicity, let's return an empty vector for non-simple or zero-dimension.
			if (dataSpaceType == H5S_SCALAR) {
				return { 1 }; // A scalar dataspace has a single element
			}
			else if (dataSpaceType == H5S_NULL) {
				return {}; // A null dataspace has no elements or dimensions
			}
			return {}; // For any other non-positive rank, return empty
		}

		// Create a vector to hold the dimensions
		std::vector<hsize_t> dims(rank);

		// Retrieve the dimensions
		// The second argument (maxdims) can be NULL if you don't need max dimensions.
		dataSpace.getSimpleExtentDims(dims.data(), NULL);

		return dims;
	}

	std::vector<hsize_t> getDimensions(const H5::DataSpace &dataSpace)
	{

		auto  newDim = get_dataspace_dimensions(dataSpace);
		const int numDimensions = dataSpace.getSimpleExtentNdims();
		if (numDimensions == 0)
			return std::vector<hsize_t>();

		std::vector<hsize_t> dimensions(numDimensions);
		int ndims = dataSpace.getSimpleExtentDims(dimensions.data(), NULL);
		if (ndims == 0)
			return std::vector<hsize_t>();

		
		return dimensions;
	}

	template<typename H5ObjectT>
	QString getAttributeInfo(const H5ObjectT& object)
	{
		auto fullPath = object.getObjName();
		//auto name = std::filesystem::path(fullPath).filename().string();

		QString attributeInfo;
		hsize_t numAttrs = object.getNumAttrs();
		for (hsize_t idx = 0; idx < numAttrs; ++idx)
		{
			try
			{
				H5::Attribute attribute = object.openAttribute(idx);
				auto attributeName = attribute.getName();
				//std::cerr << fullPath << " -> " << attributeName;
				attributeInfo += "\n- " + QString::fromStdString(attributeName);
				// Get the data type of the attribute
				auto attr_data_type = attribute.getDataType();
				auto attr_data_space = attribute.getSpace();
				// Check if it's a string type
				if (attr_data_type.getClass() == H5T_STRING) {


					
					H5::StrType str_read_type = attribute.getStrType();
					if (str_read_type.isVariableStr())
					{
						char** rdata = nullptr;
						try
						{

							// Get the number of elements in the dataspace
							hsize_t num_elements = attr_data_space.getSelectNpoints(); // Number of elements selected
							// Allocate a C-style array of char pointers to hold the read strings
							// HDF5 will allocate memory for each string, so we need to free it later.
							rdata = new char* [num_elements];

							// Read the attribute data
							attribute.read(str_read_type, rdata);

							std::vector<std::string> items;
							// Convert the C-style char** array to std::vector<std::string>
							for (hsize_t i = 0; i < num_elements; ++i) {
								if (rdata[i] != nullptr) {
									items.push_back(std::string(rdata[i]));
									// IMPORTANT: Free the memory allocated by HDF5 for each string
									H5free_memory(rdata[i]);
								}
								else {
									items.push_back(""); // Or handle null pointers as needed
								}
							}
							// IMPORTANT: Free the memory allocated for the array of pointers itself
							delete[] rdata;

							attributeInfo += ": {";
							bool first = true;
							for (const auto& item : items) {
								if (!first)
									attributeInfo += ", ";
								attributeInfo += QString::fromStdString(item);
							}
							attributeInfo += "}";
						}
						catch (...)
						{
						}

					}


					else
					{
						// Read the string directly into an H5std_string
						H5std_string h5_retrieved_string;
						attribute.read(attr_data_type, h5_retrieved_string);

						// Convert H5std_string to std::string
						attributeInfo += ": [" + QString::fromStdString(h5_retrieved_string) + "]";

					}
				}
				else
				{ }
				std::cerr << std::endl;
				attribute.close();
			}
			catch (H5::AttributeIException& err) 
			{
				//std::cerr << "Error reading attribute: " << err.getMsg() << std::endl;
				//return 1;
			}
			catch (H5::DataTypeIException& err)
			{
				//std::cerr << "Error getting data type of attribute: " << err.getMsg() << std::endl;
				//return 1;
			}
			catch (...)
			{

			}
			
		}
		return attributeInfo;
	}

	std::size_t getNumberOfElements(const std::vector<hsize_t> &dimensions)
	{
		if (dimensions.empty())
			return 0;
		std::size_t nrOfElements = 1;
		for (auto dim : dimensions)
		{
			nrOfElements *= dim;
		}

		return nrOfElements;
	}

	


	class TreeItem
	{
		

	public:
		enum { Value = Qt::UserRole+1, Visible = Qt::UserRole + 2, Sorting = Qt::UserRole + 3, ItemType = Qt::UserRole + 4 };
		enum { Description = 0, Size = 1,  Columns };
		static const QStringList HeaderLabels() 
		{
			return { "Name", "Estimated Size"};
		}

	private:
		QStandardItem* _item = nullptr;
		QStandardItem* _sizeItem = nullptr;


		TreeItem() = delete;
	protected:
		explicit TreeItem(QStandardItem* item, QStandardItem *sizeItem)
		{
			_item = item;
			_sizeItem = sizeItem;
		}

	
		//TreeItem(QString description, hsize_t idx, bool visible = true, QVariant sortingValue = QVariant());
		
		
		
	public:
		TreeItem(const TreeItem&) = default;
		// static function for creation to make clear memory is not allocated inside the class instantation
		static TreeItem Create(const QString& text, hsize_t idx);
		static TreeItem Create(const H5::H5Object &obj, hsize_t idx);
		
		static void Destroy(TreeItem item) { item.clear(); }

		~TreeItem() {}; // no deletion done here. Should be done by the model

		operator QList<QStandardItem*>() const
		{
			return { _item, _sizeItem };
		}

		QString getText() { return _item->text(); }
		void setIdx(hsize_t idx);
		hsize_t getIdx() const;
		void setSortValue(const QVariant& value);
		void setVisible(bool visible);
		void setIcon(const QIcon& icon);
		void setIcon(const QString& description);
		void setText(const QString& text);
		void setData(const QVariant& value, int role = Qt::UserRole + 1);
		void setTreeItemType(TreeItemType itemType)
		{
			_item->setData(static_cast<int>(itemType), ItemType);
		}
		TreeItemType getTreeItemType() const
		{
			return static_cast<TreeItemType>(_item->data(ItemType).toInt());
		}

		QVariant data(int role = Qt::UserRole + 1) const;

		void setSize(hsize_t size);
		hsize_t getSize() const;

		int childCount() const;
		TreeItem child(int child) const;

		QStandardItem* asParent() const
		{
			return _item;
		}

		void setToolTip(const TooltipInfo &tooltipInfo)
		{

			QString tooltip;

			bool newLineNeed = false;
			if (!tooltipInfo.topText.isEmpty())
			{
				tooltip += tooltipInfo.topText;
				newLineNeed = true;
			}

			if (!tooltipInfo.dataDescription.isEmpty())
			{
				if (newLineNeed)
					tooltip += "\n";
				tooltip += QString("Data Type: ") + tooltipInfo.dataDescription;
				newLineNeed = true;
			}
			

			if (!tooltipInfo.dimensions.empty())
			{
				auto dimensions = tooltipInfo.dimensions;
				if (dimensions.size() == 1)
					dimensions.push_back(1);

				QString dimensionString;
				for (auto dim : dimensions)
				{
					if (!dimensionString.isEmpty())
						dimensionString += " x ";
					dimensionString += QString::number(dim);
				}
				if (!dimensionString.isEmpty())
				{
					if (newLineNeed)
						tooltip += "\n";
					tooltip += "Dimensions: (" + dimensionString + ")";
					newLineNeed = true;
				}
			}

			if (!tooltipInfo.bottomText.isEmpty())
			{
				if (newLineNeed)
					tooltip += "\n";
				tooltip  += tooltipInfo.bottomText;
				newLineNeed = true;
			}


			_item->setToolTip(tooltip);
		}

		

	protected:
		void clear()
		{
			delete _item;
			delete _sizeItem;
		}
	};


	
	
	void TreeItem::setIdx(hsize_t idx)
	{
		_item->setData(idx, TreeItem::Value);
	}

	hsize_t TreeItem::getIdx() const
	{
		return _item->data(TreeItem::Value).toULongLong();
	}
	

	void TreeItem::setSortValue(const QVariant& value)
	{
		_item->setData(value, TreeItem::Sorting);
	}
	
	void TreeItem::setVisible(bool visible)
	{
		if(visible == false)
		{ 
			std::string text = _item->text().toStdString();
			int bp = 0; 
			++bp;
		}
		
		_item->setData(visible, TreeItem::Visible);
	}

	void TreeItem::setIcon(const QIcon& icon)
	{
		_item->setIcon(icon);
	}

	void TreeItem::setIcon(const QString& description)
	{
		_item->setIcon(mv::util::StyledIcon(description));
	}

	void TreeItem::setText(const QString& text)
	{
		_item->setText(text);
	}
	void TreeItem::setData(const QVariant& value, int role )
	{
		_item->setData(value, role);
	}
	QVariant TreeItem::data(int role ) const
	{
		return _item->data(role);
	}

	void TreeItem::setSize(hsize_t size)
	{
		_sizeItem->setText(formatBytes1024_QString(size));
		_sizeItem->setData(size, TreeItem::Sorting);
		_sizeItem->setData(size, TreeItem::Value);
	}

	hsize_t TreeItem::getSize() const
	{
		return _sizeItem->data(TreeItem::Value).toULongLong();
	}

	int TreeItem::childCount() const
	{
		return _item->rowCount();
	}

	TreeItem TreeItem::child(int c) const
	{
		QStandardItem *item1 = _item->child(c, Description);
		QStandardItem* item2 = _item->child(c, Size);
		return TreeItem(item1, item2);
	}


	

	hsize_t AccumulateSizeOfChildrenAndSetVisibility(TreeItem currentItem, bool visibility = false)
	{

		hsize_t sum = 0;
		for (int i = 0; i < currentItem.childCount(); ++i)
		{
			auto child = currentItem.child(i);
			std::string childName = child.getText().toStdString();
			auto size = child.getSize();
			sum += size;
			child.setVisible(visibility);
		}
		return sum;
	}


	
	hsize_t getSizeOfDataType(const H5::DataType& type, H5T_class_t type_class)
	{
		hsize_t byte_size = 0;
		// Add byte size for types that have a well-defined fixed size
		// Note: Compound, Variable-Length, and Arrays have more complex size reporting
		// This will append for simple types, or the base size for complex types.
		if (type_class != H5T_NO_CLASS &&
			type_class != H5T_VLEN && // VLEN elements have variable sizes
			type_class != H5T_COMPOUND && // Compound types are a sum of member sizes
			type_class != H5T_STRING && // Strings need special handling for fixed/variable
			type_class != H5T_ARRAY) // Array size is base_type_size * num_elements
		{
			try {
				byte_size = type.getSize();
			}
			catch (const H5::Exception& e) {
				byte_size = 0;
				// Handle cases where getSize() might fail or not be meaningful
				// for certain complex types or invalid types.
			//	qWarning() << "Could not get size for type class" << type_class << ":" << e.getCDetailMsg();
			}
		}
		// Special handling for fixed-length strings (vlen strings don't have a fixed size per string)
		else if (type_class == H5T_STRING && !type.isVariableStr()) {
			try {
				byte_size = type.getSize();				
			}
			catch (const H5::Exception& e) {
				byte_size = 0;
				//	qWarning() << "Could not get size for fixed string type:" << e.getCDetailMsg();
			}
		}
		else if (type_class == H5T_ARRAY)
		{
			try {
				byte_size = type.getSize();
			}
			catch (const H5::Exception& e) {
				byte_size = 0;
				//	qWarning() << "Could not get size for fixed string type:" << e.getCDetailMsg();
			}
		}
		return byte_size;
	 }

	QString getClassTypeDescription(H5T_class_t type_class, const H5::DataType& type, hsize_t byte_size =0 )
	{
		
		QString description;
		switch (type_class) {
		case H5T_NO_CLASS:
			description = "Unknown/Invalid";
			break;
		case H5T_INTEGER:
			description = "Integer";
			break;
		case H5T_FLOAT:
			description = "Floating Point";
			break;
		case H5T_TIME:
			description = "Time/Date";
			break;
		case H5T_STRING:
			// Check if it's a fixed-length string
			if (type.isVariableStr()) {
				description = "Text (Variable Length)";
			}
			else {
				description = "Text (Fixed Length)";
			}
			break;
		case H5T_BITFIELD:
			description = "Bitfield";
			break;
		case H5T_OPAQUE:
			description = "Raw Data";
			break;
		case H5T_COMPOUND:
			description = "Structured Data"; // Individual member sizes add up
			break;
		case H5T_REFERENCE:
			description = "Reference/Link";
			break;
		case H5T_ENUM:
			description = "Enumeration";
			break;
		case H5T_VLEN:
			description = "Variable Length Data"; // Actual size depends on content
			break;
		case H5T_ARRAY:
			description = "Array"; // Size depends on element type and dimensions
			break;
			//		case H5T_COMPLEX:
			//			description = "Complex Number";
			//			break;
		default:
			description = "Unrecognized";
			break;
		}

		if (byte_size)
		{
			description += QString(" (%1 bytes)").arg(byte_size); 
		}
		return description;
	}

	QString CreateToolTip(std::vector<hsize_t> dimensions, const QString &dataTypeDescription, const QString &endText = QString(), const QString& topText = QString())
	{

		QString tooltip;
		
		if (!topText.isEmpty())
			tooltip += topText + "\n";

		if(!dataTypeDescription.isEmpty())
			tooltip += QString("Data Type: ") + dataTypeDescription;
		
		if (!dimensions.empty())
		{
			if (dimensions.size() == 1)
				dimensions.push_back(1);

			QString dimensionString;
			for (auto dim : dimensions)
			{
				if (!dimensionString.isEmpty())
					dimensionString += " x ";
				dimensionString += QString::number(dim);
			}
			if (!dimensionString.isEmpty())
				tooltip += "\nDimensions: (" + dimensionString + ")";
		}
		
		
		if(!endText.isEmpty())
			tooltip += "\n" + endText;

		return tooltip;
	}

	
	void InitializeDatasetTreeItem(const H5::DataType& dataType, H5T_class_t classType, TreeItem currentItem, const std::vector<hsize_t>& dimensions = {})
	{
		switch (classType) {
		case H5T_NO_CLASS:
			currentItem.setTreeItemType(TreeItemType::Unknown);
			// TODO
			break;
		case H5T_INTEGER:
		case H5T_FLOAT:
			currentItem.setIcon("database");
			currentItem.setTreeItemType(TreeItemType::PointDataset);

			break;
		case H5T_TIME:
			// TODO
			break;
		case H5T_STRING:
			currentItem.setIcon("table-cells-large");
			currentItem.setTreeItemType(TreeItemType::ClusterDataset);
			break;
		case H5T_BITFIELD:
			// TODO
			break;
		case H5T_OPAQUE:
			// TODO
			break;
		case H5T_COMPOUND:
			currentItem.setIcon("table-list");
			currentItem.setTreeItemType(TreeItemType::CompoundDatset);
			break;
		case H5T_REFERENCE:
			// TODO
			break;
		case H5T_ENUM:
			currentItem.setIcon("table-cells-large");
			currentItem.setTreeItemType(TreeItemType::ClusterDataset);
			break;
		case H5T_VLEN:
			// TODO
			break;
		case H5T_ARRAY:
			currentItem.setIcon("table");
			currentItem.setTreeItemType(TreeItemType::PointDataset);
			// TODO
			break;
			//		case H5T_COMPLEX:
						// TODO
			//			break;
		default:
			currentItem.setTreeItemType(TreeItemType::Unknown);
			break;
		}

		if (classType != H5T_COMPOUND)
		{
			const auto byte_size = getSizeOfDataType(dataType, classType);

			const std::size_t nrOfElements = getNumberOfElements(dimensions);
			if (nrOfElements)
			{
				const auto size = nrOfElements * byte_size;
				if(size)
					currentItem.setSize(size);
			}
		}

		

	}
	void ProcessDataset(const H5::H5Object& obj, TreeItem currentItem)
	{
		H5::DataSet dataset(obj.getId());
		H5::DataType dataType = dataset.getDataType();
		H5T_class_t classType = dataType.getClass();


		auto dataSpace = dataset.getSpace();
		const auto dimensions = getDimensions(dataSpace);

		InitializeDatasetTreeItem(dataType, classType, currentItem, dimensions);
		
		if (currentItem.getSize() == 0)
		{
			
			if(currentItem.getTreeItemType() != TreeItemType::CompoundDatset)
				currentItem.setSize(dataset.getStorageSize());
				
		}

		QString startText;
#ifdef _DEBUG
		startText = "H5::DataSet";
#endif

		QString endText;
		if (dimensions.size() > 1)
		{

			if (currentItem.getTreeItemType() == TreeItemType::PointDataset)
				endText += "Matrix";
		}
		const auto byte_size = getSizeOfDataType(dataType, classType);
		auto classTypeDescription = getClassTypeDescription(classType, dataType, byte_size);
		
		QString attributeInfo = getAttributeInfo(obj);
		
		if (!attributeInfo.isEmpty())
		{
			endText += "\nAttributes:" + attributeInfo;
		}
		
		currentItem.setToolTip(TooltipInfo(dimensions, classTypeDescription, startText, endText));
		
	}

	TreeItem TreeItem::Create(const QString& text, hsize_t idx)
	{
		QStandardItem* item = new QStandardItem(text);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		item->setData(idx, TreeItem::Value);
		item->setData(true, TreeItem::Visible);
		item->setData(text, TreeItem::Sorting);

		return TreeItem(item, new QStandardItem());
	}

	TreeItem TreeItem::Create(const H5::H5Object& obj, hsize_t idx)
	{
		auto fullPath = obj.getObjName();
		auto name = std::filesystem::path(fullPath).filename().string();
		QString text = QString::fromStdString(name);

		TreeItem newItem = Create(text, idx);

		auto hdf5_type = H5::IdComponent::getHDFObjType(obj.getId());
		if (hdf5_type == H5I_GROUP)
		{
			newItem.setIcon("layer-group");
			newItem.setTreeItemType(TreeItemType::Group);
		}
		else if (hdf5_type == H5I_DATASET)
		{
			ProcessDataset(obj, newItem);
		}
		else
		{
			newItem.setTreeItemType(TreeItemType::Unknown);
			newItem.setIcon("circle-question");
		}
		
		return newItem;
	}
	/// Internal Helper Functions

	template<typename Container, typename List>
	bool contains_only(const Container& container, const List& list)
	{
		if (container.size() == list.size())
		{
			for (auto item : list)
			{
				if (!container.contains(item))
					return false;
			}
			return true;
		}
		return false;
	}

	template<typename T>
	bool ObjectIs(const H5::H5Object& obj)
	{
		try
		{
			// Try to cast the object to a Group
			T dummy = dynamic_cast<const T&>(obj);
			return true; // Successful cast means it is a Group
		}
		catch (std::bad_cast&) {
			return false; // If cast fails, it's not a Group
		}
		return false;
	}

	

	

	

	
	

	

	template<typename T>
	std::vector<T> read_attribute_vector_1D(H5::Attribute& attribute)
	{
		H5::DataSpace dataSpaceOfAttribute = attribute.getSpace();

		const int dimensions = dataSpaceOfAttribute.getSimpleExtentNdims();
		std::size_t nrOfItems = 1;
		if (dimensions == 1)
		{

			std::vector<hsize_t> dimensionSize(dimensions);
			int ndims = dataSpaceOfAttribute.getSimpleExtentDims(&(dimensionSize[0]), NULL);
			for (std::size_t d = 0; d < dimensions; ++d)
			{
				nrOfItems *= dimensionSize[d];
			}
			std::vector<T> data(nrOfItems, 0);
			attribute.read(H5Utils::getH5DataType<T>(), data.data());
			return data;
		}
		return std::vector<T>(); // return empty vector
	}
	
	void ProcessCompound(const H5::H5Object& obj, TreeItem compoundItem)
	{
		H5::DataSet dataset(obj.getId());
		H5::DataSpace datasetSpace = dataset.getSpace();
		std::vector<hsize_t> dimensions = getDimensions(datasetSpace);
		H5::CompType compType = dataset.getCompType();
		auto nrOfComponents = compType.getNmembers();
		for (hsize_t idx = 0; idx < nrOfComponents; ++idx)
		{
			auto name = compType.getMemberName(idx);
			auto qName = QString::fromStdString(name);



			TreeItem newItem = TreeItem::Create(qName, idx);
			
			H5T_class_t componentClass = compType.getMemberClass(idx);
			
			TooltipInfo tooltipInfo;
			tooltipInfo.dimensions = dimensions;
			tooltipInfo.topText = "H5::CompType";
			switch (componentClass)
			{
				case H5T_INTEGER:
				{
					H5::IntType memberType = compType.getMemberIntType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_INTEGER";
					break;
				}
				case H5T_FLOAT:
				{
					H5::FloatType memberType = compType.getMemberFloatType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_FLOAT";
					break;
				}

				case H5T_STRING:
				{
					auto memberType = compType.getMemberStrType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_STRING";
					break;
				}

				case H5T_COMPOUND:
				{
					auto memberType = compType.getMemberCompType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_COMPOUND";
					break;
				}

				case H5T_ENUM:
				{
					auto memberType = compType.getMemberEnumType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_ENUM";
					break;
				}

				case H5T_VLEN:
				{
					auto memberType = compType.getMemberVarLenType(idx);
					InitializeDatasetTreeItem(memberType, componentClass, newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					tooltipInfo.topText += " --> H5T_VLEN";
					break;
				}

				case H5T_ARRAY:
				{
					H5::ArrayType memberType = compType.getMemberArrayType(idx);
					int rank = memberType.getArrayNDims();
					std::vector<hsize_t> memberDimensions = dimensions;
					if (rank > 0)
					{
						std::vector<hsize_t> arrayDims(rank);
						memberType.getArrayDims(arrayDims.data());
						for (auto dim : arrayDims)
						{
							memberDimensions.push_back(dim);
						}
					}
					auto superType = memberType.getSuper();
					InitializeDatasetTreeItem(superType, superType.getClass(), newItem, memberDimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(superType.getClass(), superType);
					tooltipInfo.dimensions = memberDimensions;
					tooltipInfo.topText += " --> H5T_ARRAY";
					break;
				}

				default:
				{
					auto memberType = compType.getMemberDataType(idx);
					InitializeDatasetTreeItem(memberType, memberType.getClass(), newItem, dimensions);
					tooltipInfo.dataDescription = getClassTypeDescription(componentClass, memberType);
					break;
				}
			}

			#ifndef _DEBUG
				tooltipInfo.topText.clear();
			#endif

			newItem.setToolTip(tooltipInfo);
			compoundItem.asParent()->appendRow(newItem);
			/*
			auto size= AccumulateSizeOfChildrenAndSetVisibility(compoundItem, true);
			compoundItem.setSize(size);
			*/
			//dimensions.push_back(nrOfComponents);
			
			
		}

		compoundItem.setToolTip( TooltipInfo({}, getClassTypeDescription(compType.getClass(), compType), "H5::DataSet", QString("%1 components").arg(nrOfComponents)));
		dataset.close();
	}

	bool processGroup(const H5::H5Object& obj, TreeItem currentItem)
	{

		H5::Group group(obj.getId());

		TooltipInfo tooltipInfo;
		
#ifdef _DEBUG
		tooltipInfo.topText = "H5::Group";
#endif	

		// Iterate over the object members of the group
		hsize_t numObjs = group.getNumObjs();

		QMap<H5std_string, hsize_t> childCount;
		for (hsize_t idx = 0; idx < numObjs; ++idx)
		{
			// Get the object by index
			H5std_string childName = group.getObjnameByIdx(idx);

			H5O_info_t childInfo;
			group.getObjinfo(childName, childInfo);

			// Open the object based on its type
			switch (childInfo.type)
			{
			case H5O_TYPE_GROUP:
			{
				H5::Group subGroup = group.openGroup(childName);
				if (addHdf5ObjectTo(subGroup, idx, currentItem.asParent()))
					childCount[childName] = idx;

			}
			break;

			case  H5O_TYPE_DATASET:
			{
				H5::DataSet dataset = group.openDataSet(childName);
				if (addHdf5ObjectTo(dataset, idx, currentItem.asParent()))
					childCount[childName] = idx;
			}
			break;

			}
		}


		
		


		const QList< H5std_string> matrix_datasets = { "data","indices","indptr" };
		if (contains_only(childCount, matrix_datasets))
		{
			H5std_string shapeAttributeName = "shape";
			bool shapeAttributeExists = group.attrExists("shape");
			int num_attrs = group.getNumAttrs();
			for (int i = 0; (!shapeAttributeExists) && (i < num_attrs); ++i)
			{
				H5::Attribute attribute = group.openAttribute(i);
				H5std_string attr_name = attribute.getName();
				//attribute.close();
				if (attr_name.ends_with("shape"))
				{
					shapeAttributeName = attr_name;
					shapeAttributeExists = true;
				}
			}

			if (shapeAttributeExists)
			{
				H5::Attribute shape = group.openAttribute(shapeAttributeName);
				H5::DataSpace dataSpace = shape.getSpace();
				tooltipInfo.dimensions = read_attribute_vector_1D<hsize_t>(shape);
				const auto totalNrOfElements = getNumberOfElements(tooltipInfo.dimensions);


				if (totalNrOfElements)
				{

					// check if child "data" is a H5::DataSet
					if (H5G_DATASET == group.getObjTypeByIdx(childCount["data"]))
					{
						H5::DataSet data = group.openDataSet("data");
						H5::DataType datatype = data.getDataType();
						auto classType = datatype.getClass();
						hsize_t byte_size = getSizeOfDataType(datatype,  classType);
						const auto size = totalNrOfElements * byte_size;
						if (size)
							currentItem.setSize(size);
						else
							currentItem.setSize(data.getStorageSize());
						auto dummy = AccumulateSizeOfChildrenAndSetVisibility(currentItem, false);

						currentItem.setIcon("database");

						tooltipInfo.dataDescription = getClassTypeDescription(classType, datatype, byte_size);
						tooltipInfo.bottomText = "Sparse Matrix";
					}
				}
			}
		}
		else
		{
			const QList< H5std_string> coded_categories = { "codes","categories" };
			if (contains_only(childCount, coded_categories))
			{
				const auto size = AccumulateSizeOfChildrenAndSetVisibility(currentItem);
				currentItem.setSize(size);
				currentItem.setIcon("table-cells-large");
				tooltipInfo.bottomText = "Coded Categories";
			}
		}

		QString attributeInfo = getAttributeInfo(obj);
		if (!attributeInfo.isEmpty())
		{
			tooltipInfo.bottomText += "\nAttributes:" + attributeInfo;
		}

		currentItem.setToolTip(tooltipInfo);

		return (!childCount.empty());
	}
	

	// Function to recursively add HDF5 objects to the QStandardItemModel
	bool addHdf5ObjectTo(const H5::H5Object& obj, hsize_t idx, QStandardItem* parentItem, TreeModel* model)
	{
		if ((parentItem == nullptr) && (model == nullptr))
			return false;

		// Get the object name
		auto fullPath = obj.getObjName();
		auto name = std::filesystem::path(fullPath).filename().string();

		QString qName = QString::fromStdString(name);
		if (parentItem && parentItem->text() == "obs")
		{
			if (qName == "_index")
				return false;
		}

		if (parentItem && parentItem->text() == "raw")
		{
			if (qName == "var")
				return false;
			if (qName == "varm")
				return false;
			if (qName == "varp")
				return false;
		}
		
		// Create a new item for the current object
		TreeItem currentItem = TreeItem::Create(obj, idx);

		bool addObjectToHierarchy = false;  //(currentItem.childCount() || currentItem.getSize());

		

		// Check if the object is a group
		if (currentItem.getTreeItemType() == TreeItemType::Group)
		{
			;

			addObjectToHierarchy = processGroup(obj, currentItem);
		}
		else if (currentItem.getTreeItemType() == TreeItemType::CompoundDatset)
		{
			ProcessCompound(obj, currentItem);
		}

		addObjectToHierarchy = (addObjectToHierarchy || currentItem.childCount() || currentItem.getSize());

		if (!addObjectToHierarchy)
		{
			int bp = 0;
			++bp;

			addObjectToHierarchy = true;
		}
		
		//if (currentItem.getTreeItemType() == TreeItemType::CompoundDatset)

		if (addObjectToHierarchy)
		{

			if (parentItem)
			{
				parentItem->appendRow(currentItem);
			}
			else if (model)
			{
				std::size_t r = model->rowCount();
				model->appendRow(currentItem);

			}
			// do this after adding it to the model otherwise the children don't get checked.
			
			return true;
		}
		else
		{
			TreeItem::Destroy(currentItem);

			return false;
		}

		return false;
	}

	/// "Public" Functions

	QSharedPointer<TreeModel> CreateTreeModel()
	{
		QSharedPointer<TreeModel> model(new TreeModel);
		model->setColumnCount(TreeItem::Columns);
		model->setHorizontalHeaderLabels(TreeItem::HeaderLabels());

		// Connect the itemChanged signal to handle checking
		QObject::connect(model.get(), &TreeModel::itemChanged, [&](QStandardItem* item) {
			// When an item's check state changes, we want to update its children
			if (item->isCheckable())
			{
				if (item->checkState() != Qt::PartiallyChecked)
					checkAllChildren(item, item->checkState());

				checkParent(item->parent(), item->checkState());
			}
			});

		return model;
	}

	void BuildTreeModel(QSharedPointer<TreeModel> model, H5::H5File* file)
	{
		std::set<std::string> ignoreItems = {"uns", "var", "varm", "varp" };
		
		auto nrOfObjects = file->getNumObjs();
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = file->getObjnameByIdx(fo);
			if (ignoreItems.count(objectName1) == 0)
			{
				H5G_obj_t objectType1 = file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet h5Dataset = file->openDataSet(objectName1);
					addHdf5ObjectTo(h5Dataset, fo, nullptr, model.get());
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group h5Group = file->openGroup(objectName1);
					addHdf5ObjectTo(h5Group, fo, nullptr, model.get());
				}
			}
		}
		QStringList defaultItemsToCheck = { "X", "obs" };
		for (int row = 0; row < model->rowCount(); ++row)
		{
			auto currentItem = model->item(row, 0);
			auto currentText = currentItem->text();
			if (defaultItemsToCheck.contains(currentText))
				currentItem->setCheckState(Qt::Checked);
		}
		
	}
	

}