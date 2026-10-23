#include "H5ADUtils.h"

#include "DataContainerInterface.h"

#include <CoreInterface.h>

#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QTextStream>
#include <QUuid>

#include <PointData/DimensionsPickerAction.h>
#include <util/Serialization.h>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace H5AD
{
	using namespace mv;

	namespace
	{
		constexpr std::size_t tiledRowSize = 256;
		constexpr std::size_t tiledColumnSize = 256;

		PointData::ElementTypeSpecifier getTiledElementType(const H5::DataType& dataType, const int storageType)
		{
			if (storageType >= 0)
				return static_cast<PointData::ElementTypeSpecifier>(storageType);

			const auto dataClass = dataType.getClass();
			if (dataClass == H5T_FLOAT)
				return PointData::ElementTypeSpecifier::float32;

			if (dataClass == H5T_INTEGER || dataClass == H5T_ENUM)
			{
				const bool isSigned = H5Tget_sign(dataType.getId()) == H5T_SGN_2;
				switch (dataType.getSize())
				{
					case 1:
						return isSigned ? PointData::ElementTypeSpecifier::int8 : PointData::ElementTypeSpecifier::uint8;

					case 2:
						return isSigned ? PointData::ElementTypeSpecifier::int16 : PointData::ElementTypeSpecifier::uint16;

					default:
						return PointData::ElementTypeSpecifier::float32;
				}
			}

			throw std::runtime_error("Unsupported dense H5AD X data type");
		}

		std::size_t getTiledElementSize(const PointData::ElementTypeSpecifier elementType)
		{
			switch (elementType)
			{
				case PointData::ElementTypeSpecifier::bfloat16:
				case PointData::ElementTypeSpecifier::int16:
				case PointData::ElementTypeSpecifier::uint16:
					return 2;

				case PointData::ElementTypeSpecifier::int8:
				case PointData::ElementTypeSpecifier::uint8:
					return 1;

				default:
					return 4;
			}
		}

		template<typename T>
		void readTiledHyperslab(const H5::DataSet& dataset, const hsize_t* start, const hsize_t* count, const H5::DataType& readType, QByteArray& bytes)
		{
			const auto elementCount = static_cast<std::size_t>(count[0] * count[1]);
			std::vector<T> values(elementCount);
			H5::DataSpace fileSpace = dataset.getSpace();
			fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
			H5::DataSpace memorySpace(2, count);
			dataset.read(values.data(), readType, memorySpace, fileSpace);
			std::memcpy(bytes.data(), values.data(), static_cast<std::size_t>(bytes.size()));
		}

		H5::PredType getTiledReadType(const PointData::ElementTypeSpecifier elementType)
		{
			switch (elementType)
			{
			case PointData::ElementTypeSpecifier::float32:
			case PointData::ElementTypeSpecifier::bfloat16: return H5::PredType::NATIVE_FLOAT;
			case PointData::ElementTypeSpecifier::int32: return H5::PredType::NATIVE_INT32;
			case PointData::ElementTypeSpecifier::uint32: return H5::PredType::NATIVE_UINT32;
			case PointData::ElementTypeSpecifier::int16: return H5::PredType::NATIVE_INT16;
			case PointData::ElementTypeSpecifier::uint16: return H5::PredType::NATIVE_UINT16;
			case PointData::ElementTypeSpecifier::int8: return H5::PredType::NATIVE_INT8;
			case PointData::ElementTypeSpecifier::uint8: return H5::PredType::NATIVE_UINT8;
			}

			throw std::runtime_error("Unsupported tiled H5AD element type");
		}

		QByteArray readH5ADTile(const H5::DataSet& dataset, const PointData::ElementTypeSpecifier elementType, const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t rows, const std::uint64_t columns)
		{
			const hsize_t start[] = { rowOffset, columnOffset };
			const hsize_t count[] = { rows, columns };
			QByteArray bytes(static_cast<qsizetype>(rows * columns * getTiledElementSize(elementType)), Qt::Uninitialized);

			if (elementType == PointData::ElementTypeSpecifier::bfloat16)
			{
				std::vector<float> values(static_cast<std::size_t>(rows * columns));
				H5::DataSpace fileSpace = dataset.getSpace();
				fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
				H5::DataSpace memorySpace(2, count);
				dataset.read(values.data(), H5::PredType::NATIVE_FLOAT, memorySpace, fileSpace);
				auto* destination = reinterpret_cast<biovault::bfloat16_t*>(bytes.data());
				for (std::size_t index = 0; index < values.size(); ++index)
					destination[index] = values[index];
			}
			else
			{
				const auto readType = getTiledReadType(elementType);
				switch (elementType)
				{
				case PointData::ElementTypeSpecifier::float32: readTiledHyperslab<float>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::int32: readTiledHyperslab<std::int32_t>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::uint32: readTiledHyperslab<std::uint32_t>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::int16: readTiledHyperslab<std::int16_t>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::uint16: readTiledHyperslab<std::uint16_t>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::int8: readTiledHyperslab<std::int8_t>(dataset, start, count, readType, bytes); break;
				case PointData::ElementTypeSpecifier::uint8: readTiledHyperslab<std::uint8_t>(dataset, start, count, readType, bytes); break;
				default: throw std::runtime_error("Unsupported tiled H5AD element type");
				}
			}

			return bytes;
		}

		struct StagedPackedTile
		{
			std::uint64_t rowOffset = 0;
			std::uint64_t columnOffset = 0;
			std::uint64_t rows = 0;
			std::uint64_t columns = 0;
			std::size_t superTileIndex = 0;
			mv::util::PackedBlobBlock block;
		};

		struct StagedPackedSuperTile
		{
			QString path;
		};

		struct StagedPointData
		{
			std::uint64_t rows = 0;
			std::uint64_t columns = 0;
			std::size_t elementSize = 0;
		std::size_t rowTileSize = tiledRowSize;
		std::size_t columnTileSize = tiledColumnSize;
			std::uint64_t tileRows = 0;
			std::uint64_t tileColumns = 0;
			std::size_t superTileRows = 4;
			std::size_t superTileColumns = 4;
			mv::util::SharedCodec codec;
			QString stagingDirectory;
			std::vector<StagedPackedSuperTile> superTiles;
			std::vector<StagedPackedTile> tiles;

			~StagedPointData()
			{
				if (!stagingDirectory.isEmpty() && !QDir(stagingDirectory).removeRecursively())
					qWarning() << "Unable to remove H5AD staging directory:" << stagingDirectory;
			}
		};

		struct StagedReadCache
		{
			std::unordered_map<std::size_t, std::unique_ptr<mv::util::PackedBlobFileReader>> readers;
		};

		QString createStagingDirectory(const QString& stagingRoot, const std::uint64_t rows, const std::uint64_t columns, const std::size_t elementSize, const pointdata::storage::StorageBackendConfiguration& configuration, const mv::util::SharedCodec& codec)
		{
			if (stagingRoot.isEmpty())
				throw std::invalid_argument("H5AD staging requires an application-managed staging directory");

			const auto root = stagingRoot;
			const auto stagingDirectory = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));

			if (!QDir().mkpath(stagingDirectory))
				throw std::runtime_error(QString("Unable to create H5AD staging directory: %1").arg(stagingDirectory).toStdString());

			QFile manifest(QDir(stagingDirectory).filePath(QStringLiteral("manifest.txt")));
			if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text))
				throw std::runtime_error(QString("Unable to create H5AD staging manifest: %1").arg(manifest.fileName()).toStdString());

			QTextStream stream(&manifest);
			stream << "Rows=" << rows << "\n";
			stream << "Columns=" << columns << "\n";
			stream << "ElementSize=" << elementSize << "\n";
			stream << "RowTileSize=" << configuration.rowTileSize << "\n";
			stream << "ColumnTileSize=" << configuration.columnTileSize << "\n";
			stream << "SuperTileRows=" << configuration.superTileRows << "\n";
			stream << "SuperTileColumns=" << configuration.superTileColumns << "\n";
			stream << "Storage=PackedTileFiles\n";
			stream << "Codec=" << codec->getName() << "\n";
			return stagingDirectory;
		}

		mv::util::SharedCodec createStagingCodec()
		{
			const auto* currentProject = mv::projects().getCurrentProject();
			if (!currentProject)
				throw std::runtime_error("Unable to create H5AD staging codec without an active project");

			return currentProject->getCompressionAction().createCodec(nullptr);
		}

		std::shared_ptr<StagedPointData> stageDenseData(const H5::DataSet& dataset, const PointData::ElementTypeSpecifier elementType, const std::uint64_t rows, const std::uint64_t columns, const QString& stagingRoot, const pointdata::storage::StorageBackendConfiguration& configuration, const H5AD::ProgressCallback& progress)
		{
			const auto elementSize = getTiledElementSize(elementType);
			const auto staging = std::make_shared<StagedPointData>();
			staging->rows = rows;
			staging->columns = columns;
			staging->elementSize = elementSize;
			staging->rowTileSize = configuration.rowTileSize;
			staging->columnTileSize = configuration.columnTileSize;
			staging->superTileRows = configuration.superTileRows;
			staging->superTileColumns = configuration.superTileColumns;
			staging->tileRows = (rows + staging->rowTileSize - 1) / staging->rowTileSize;
			staging->tileColumns = (columns + staging->columnTileSize - 1) / staging->columnTileSize;
			staging->codec = createStagingCodec();
			const auto stagingDirectory = createStagingDirectory(stagingRoot, rows, columns, elementSize, configuration, staging->codec);
			staging->stagingDirectory = stagingDirectory;
			const auto tileCount = staging->tileRows * staging->tileColumns;
			staging->tiles.resize(static_cast<std::size_t>(tileCount));
			std::uint64_t completedTileCount = 0;
			if (progress)
				progress(0.0f);

			for (std::uint64_t superTileRow = 0; superTileRow < staging->tileRows; superTileRow += staging->superTileRows)
			{
				for (std::uint64_t superTileColumn = 0; superTileColumn < staging->tileColumns; superTileColumn += staging->superTileColumns)
				{
					const auto superTileIndex = staging->superTiles.size();
					const auto fileName = QStringLiteral("supertile_%1.ptpack").arg(superTileIndex, 8, 10, QLatin1Char('0'));
					const auto filePath = QDir(stagingDirectory).filePath(fileName);
					mv::util::PackedBlobFileWriter writer(filePath, staging->codec);

					for (std::uint64_t tileRow = superTileRow; tileRow < std::min<std::uint64_t>(staging->tileRows, superTileRow + staging->superTileRows); ++tileRow)
					{
						for (std::uint64_t tileColumn = superTileColumn; tileColumn < std::min<std::uint64_t>(staging->tileColumns, superTileColumn + staging->superTileColumns); ++tileColumn)
						{
							const auto rowOffset = tileRow * staging->rowTileSize;
							const auto columnOffset = tileColumn * staging->columnTileSize;
							const auto tileRows = std::min<std::uint64_t>(staging->rowTileSize, rows - rowOffset);
							const auto tileColumns = std::min<std::uint64_t>(staging->columnTileSize, columns - columnOffset);
							const auto bytes = readH5ADTile(dataset, elementType, rowOffset, columnOffset, tileRows, tileColumns);
							const auto block = writer.append(bytes);

							const auto tileIndex = static_cast<std::size_t>(tileRow * staging->tileColumns + tileColumn);
							staging->tiles[tileIndex] = { rowOffset, columnOffset, tileRows, tileColumns, superTileIndex, block };
							if (progress)
								progress(static_cast<float>(++completedTileCount) / static_cast<float>(tileCount));
						}
					}

					writer.close();
					staging->superTiles.push_back({ filePath });
				}
			}

			return staging;
		}

		QByteArray readStagedTile(const StagedPointData& staging, const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t rows, const std::uint64_t columns)
		{
			thread_local std::unordered_map<const StagedPointData*, std::unique_ptr<StagedReadCache>> caches;
			auto& cache = caches[&staging];
			const auto tileRow = rowOffset / staging.rowTileSize;
			const auto tileColumn = columnOffset / staging.columnTileSize;
			const auto tileIndex = static_cast<std::size_t>(tileRow * staging.tileColumns + tileColumn);
			if (tileIndex >= staging.tiles.size())
				throw std::out_of_range("Requested tile is not present in H5AD packed staging");

			const auto& tile = staging.tiles[tileIndex];
			if (tile.rowOffset != rowOffset || tile.columnOffset != columnOffset || tile.rows != rows || tile.columns != columns)
				throw std::out_of_range("Requested tile does not match H5AD packed staging geometry");

			auto reader = cache->readers.find(tile.superTileIndex);
			if (reader == cache->readers.end())
			{
				const auto& superTile = staging.superTiles[tile.superTileIndex];
				reader = cache->readers.emplace(tile.superTileIndex, std::make_unique<mv::util::PackedBlobFileReader>(superTile.path, staging.codec)).first;
			}

			return reader->second->read(tile.block);
		}

		void attachPackedTileSource(const std::shared_ptr<StagedPointData>& staging, Dataset<Points>& pointsDataset)
		{
			QStringList filePaths;
			filePaths.reserve(static_cast<qsizetype>(staging->superTiles.size()));
			for (const auto& superTile : staging->superTiles)
				filePaths.push_back(superTile.path);

			std::vector<std::uint64_t> offsets;
			std::vector<std::uint64_t> compressedSizes;
			std::vector<std::uint64_t> uncompressedSizes;
			std::vector<std::size_t> superTileIndices;
			offsets.reserve(staging->tiles.size());
			compressedSizes.reserve(staging->tiles.size());
			uncompressedSizes.reserve(staging->tiles.size());
			superTileIndices.reserve(staging->tiles.size());
			for (const auto& tile : staging->tiles)
			{
				offsets.push_back(tile.block.offset);
				compressedSizes.push_back(tile.block.compressedSize);
				uncompressedSizes.push_back(tile.block.uncompressedSize);
				superTileIndices.push_back(tile.superTileIndex);
			}

			pointsDataset->attachPackedTileSource(filePaths, staging->codec->getName(), offsets, compressedSizes, uncompressedSizes, superTileIndices, staging);
		}

		template<typename T>
		std::vector<T> readH5ADRange(const H5::DataSet& dataset, const std::uint64_t offset, const std::uint64_t count, const H5::DataType& readType)
		{
			std::vector<T> values(static_cast<std::size_t>(count));
			const hsize_t start[] = { offset };
			const hsize_t extent[] = { count };
			H5::DataSpace fileSpace = dataset.getSpace();
			fileSpace.selectHyperslab(H5S_SELECT_SET, extent, start);
			H5::DataSpace memorySpace(1, extent);
			dataset.read(values.data(), readType, memorySpace, fileSpace);
			return values;
		}

		std::shared_ptr<StagedPointData> stageSparseData(const H5::DataSet& dataSet, const H5::DataSet& indicesSet, const H5::DataSet& indptrSet, const std::uint64_t rows, const std::uint64_t columns, const QString& stagingRoot, const pointdata::storage::StorageBackendConfiguration& configuration, const H5AD::ProgressCallback& progress)
		{
			constexpr std::size_t elementSize = sizeof(float);
			const auto staging = std::make_shared<StagedPointData>();
			staging->rows = rows;
			staging->columns = columns;
			staging->elementSize = elementSize;
			staging->rowTileSize = configuration.rowTileSize;
			staging->columnTileSize = configuration.columnTileSize;
			staging->superTileRows = configuration.superTileRows;
			staging->superTileColumns = configuration.superTileColumns;
			staging->tileRows = (rows + staging->rowTileSize - 1) / staging->rowTileSize;
			staging->tileColumns = (columns + staging->columnTileSize - 1) / staging->columnTileSize;
			staging->codec = createStagingCodec();
			const auto stagingDirectory = createStagingDirectory(stagingRoot, rows, columns, elementSize, configuration, staging->codec);
			staging->stagingDirectory = stagingDirectory;
			const auto tileCount = staging->tileRows * staging->tileColumns;
			staging->tiles.resize(static_cast<std::size_t>(tileCount));
			std::uint64_t completedTileCount = 0;
			if (progress)
				progress(0.0f);

			for (std::uint64_t superTileRow = 0; superTileRow < staging->tileRows; superTileRow += staging->superTileRows)
			{
				const auto superTileRowCount = std::min<std::uint64_t>(staging->superTileRows * staging->rowTileSize, rows - superTileRow * staging->rowTileSize);
				const auto sourceRowOffset = superTileRow * staging->rowTileSize;
				const auto indptr = readH5ADRange<std::uint64_t>(indptrSet, sourceRowOffset, superTileRowCount + 1, H5::PredType::NATIVE_UINT64);
				const auto valueCount = indptr.back() - indptr.front();
				const auto indices = valueCount > 0 ? readH5ADRange<std::uint64_t>(indicesSet, indptr.front(), valueCount, H5::PredType::NATIVE_UINT64) : std::vector<std::uint64_t>();
				const auto values = valueCount > 0 ? readH5ADRange<float>(dataSet, indptr.front(), valueCount, H5::PredType::NATIVE_FLOAT) : std::vector<float>();

				for (std::uint64_t superTileColumn = 0; superTileColumn < staging->tileColumns; superTileColumn += staging->superTileColumns)
				{
					const auto superTileIndex = staging->superTiles.size();
					const auto fileName = QStringLiteral("supertile_%1.ptpack").arg(superTileIndex, 8, 10, QLatin1Char('0'));
					const auto filePath = QDir(stagingDirectory).filePath(fileName);
					mv::util::PackedBlobFileWriter writer(filePath, staging->codec);

					for (std::uint64_t tileRow = superTileRow; tileRow < std::min<std::uint64_t>(staging->tileRows, superTileRow + staging->superTileRows); ++tileRow)
					{
						const auto tileRowOffset = tileRow * staging->rowTileSize;
						const auto tileRowCount = std::min<std::uint64_t>(staging->rowTileSize, rows - tileRowOffset);

						for (std::uint64_t tileColumn = superTileColumn; tileColumn < std::min<std::uint64_t>(staging->tileColumns, superTileColumn + staging->superTileColumns); ++tileColumn)
						{
							const auto tileColumnOffset = tileColumn * staging->columnTileSize;
							const auto tileColumnCount = std::min<std::uint64_t>(staging->columnTileSize, columns - tileColumnOffset);
							QByteArray bytes(static_cast<qsizetype>(tileRowCount * tileColumnCount * elementSize), Qt::Uninitialized);
							std::memset(bytes.data(), 0, static_cast<std::size_t>(bytes.size()));
							auto* destination = reinterpret_cast<float*>(bytes.data());

							for (std::uint64_t rowIndex = 0; rowIndex < tileRowCount; ++rowIndex)
							{
								const auto sourceRow = tileRowOffset + rowIndex;
								const auto begin = indptr[static_cast<std::size_t>(sourceRow - sourceRowOffset)] - indptr.front();
								const auto end = indptr[static_cast<std::size_t>(sourceRow - sourceRowOffset + 1)] - indptr.front();

								for (std::uint64_t index = begin; index < end; ++index)
								{
									const auto column = indices[static_cast<std::size_t>(index)];
									if (column >= tileColumnOffset && column < tileColumnOffset + tileColumnCount)
										destination[rowIndex * tileColumnCount + column - tileColumnOffset] = values[static_cast<std::size_t>(index)];
								}
							}

							const auto block = writer.append(bytes);
							const auto tileIndex = static_cast<std::size_t>(tileRow * staging->tileColumns + tileColumn);
							staging->tiles[tileIndex] = { tileRowOffset, tileColumnOffset, tileRowCount, tileColumnCount, superTileIndex, block };
							if (progress)
								progress(static_cast<float>(++completedTileCount) / static_cast<float>(tileCount));
						}
					}

					writer.close();
					staging->superTiles.push_back({ filePath });
				}
			}

			return staging;
		}

		bool loadSparseDataAsTiled(H5::Group& group, LoaderInfo& loaderInfo, const H5AD::ProgressCallback& progress)
		{
			if (!ContainsSparseMatrix(group) || !group.attrExists("shape"))
				return false;

			if (group.attrExists("encoding-type"))
			{
				std::string encodingType;
				auto encodingAttribute = group.openAttribute("encoding-type");
				encodingAttribute.read(encodingAttribute.getDataType(), encodingType);
				if (encodingType != "csr_matrix")
					return false;
			}

			H5::Attribute shapeAttribute = group.openAttribute("shape");
			hsize_t shape[2] = {};
			H5::DataSpace shapeSpace = shapeAttribute.getSpace();
			if (shapeSpace.getSimpleExtentNdims() != 1 || shapeSpace.getSimpleExtentNpoints() != 2)
				return false;
			shapeAttribute.read(H5::PredType::NATIVE_HSIZE, shape);

			const auto rowCount = static_cast<std::uint64_t>(shape[0]);
			const auto columnCount = static_cast<std::uint64_t>(shape[1]);
			const auto dataSet = group.openDataSet("data");
			const auto indicesSet = group.openDataSet("indices");
			const auto indptrSet = group.openDataSet("indptr");
			const auto& configuration = loaderInfo._tiledStorageConfiguration;
			const auto staging = stageSparseData(dataSet, indicesSet, indptrSet, rowCount, columnCount, loaderInfo._stagingDirectory, configuration, progress);
			Points::TiledTileGenerator generator = [staging](const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t rows, const std::uint64_t columns) {
				return readStagedTile(*staging, rowOffset, columnOffset, rows, columns);
			};

			loaderInfo._pointsDataset->initializeTiledStorage(rowCount, columnCount, PointData::ElementTypeSpecifier::float32, configuration, std::move(generator));
			attachPackedTileSource(staging, loaderInfo._pointsDataset);
			loaderInfo._pointsDataset->setTiledLoaderThreadSafe(true);
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
			return true;
		}

		bool loadDenseDataAsTiled(const H5::DataSet& dataset, LoaderInfo& loaderInfo, const int storageType, const H5AD::ProgressCallback& progress)
		{
			const auto dataSpace = dataset.getSpace();
			if (dataSpace.getSimpleExtentNdims() != 2)
				return false;

			hsize_t dimensions[2] = {};
			dataSpace.getSimpleExtentDims(dimensions);
			if (dimensions[0] == 0 || dimensions[1] == 0 || loaderInfo._sourceFileName.isEmpty())
				return false;

			const auto elementType = getTiledElementType(dataset.getDataType(), storageType);
			const auto& configuration = loaderInfo._tiledStorageConfiguration;
			const auto sourceDataset = dataset;
			const auto totalRows = static_cast<std::uint64_t>(dimensions[0]);
			const auto totalColumns = static_cast<std::uint64_t>(dimensions[1]);
			const auto staging = stageDenseData(sourceDataset, elementType, totalRows, totalColumns, loaderInfo._stagingDirectory, configuration, progress);
			Points::TiledTileGenerator generator = [staging](const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t rows, const std::uint64_t columns) {
				return readStagedTile(*staging, rowOffset, columnOffset, rows, columns);
			};

			loaderInfo._pointsDataset->initializeTiledStorage(dimensions[0], dimensions[1], elementType, configuration, std::move(generator));
			attachPackedTileSource(staging, loaderInfo._pointsDataset);
			loaderInfo._pointsDataset->setTiledLoaderThreadSafe(true);
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
			return true;
		}
	}

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
					mdd.data.resize(mdd_float.data.size());
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

	bool ContainsSparseMatrix(H5::Group& group)
	{
		bool hasData = H5Utils::contains_name(group, "data");
		bool hasIndices = H5Utils::contains_name(group, "indices");
		bool hasIndicesPtr = H5Utils::contains_name(group, "indptr");

		return hasData && hasIndices && hasIndicesPtr;
	}

	bool LoadSparseMatrix(H5::Group& group, LoaderInfo &loaderInfo)
	{
		bool loadSuccess = false;

		auto nrOfObjects = group.getNumObjs();
		auto h5groupName = group.getObjName();

		if (nrOfObjects <= 2)
			return loadSuccess;

		if (!ContainsSparseMatrix(group))
			return loadSuccess;

		H5Utils::VectorHolder data;
		H5Utils::VectorHolder indices;
		H5Utils::VectorHolder indptr;

		bool readDataSuccess = H5Utils::read_vector(group, "data", data);
		bool readIndicesSuccess = H5Utils::read_vector(group, "indices", indices);
		bool readIndicesPtrSuccess = H5Utils::read_vector(group, "indptr", indptr);

		if(!(readDataSuccess && readIndicesSuccess && readIndicesPtrSuccess))
			return loadSuccess;

		std::uint64_t xsize = indptr.size() > 0 ? indptr.size() - 1 : 0;

		if(xsize != loaderInfo._pointsDataset->getNumPoints())
			return loadSuccess;

		std::uint64_t ysize = indices.visit<std::uint64_t>([](auto& vec) { return *std::max_element(vec.cbegin(), vec.cend()); }) + 1;

		// Data name
		QString numericalDatasetName = QString(h5groupName.c_str()) /* + " (numerical)" */;
		while (numericalDatasetName[0] == '/')
			numericalDatasetName.remove(0, 1);
		while (numericalDatasetName[0] == '\\')
			numericalDatasetName.remove(0, 1);

		// Create Dataset
		Dataset<Points> numericalDataset = mv::data().createDerivedDataset(numericalDatasetName, loaderInfo._pointsDataset); // core->addDataset("Points", numericalDatasetName, parent);
		numericalDataset->setProperty("Sample Names", loaderInfo._sampleNames);

		data.visit([&numericalDataset](auto& vec) {
			typedef typename std::decay_t<decltype(vec)> v;
			numericalDataset->setDataElementType<typename v::value_type>();
			});

		auto exceedsXGigabytes = [](size_t length, size_t gigabytes = 4) -> bool{
			constexpr size_t BYTES_PER_GB = 1024ll * 1024 * 1024;
			const size_t maxBytes = gigabytes * BYTES_PER_GB;

			// Get size of the type
			const size_t elementSize = sizeof(float);

			// Check for multiplication overflow
			if (length > std::numeric_limits<size_t>::max() / elementSize) {
				return true;
			}

			// Calculate total size and compare
			const size_t totalBytes = length * elementSize;
			return totalBytes > maxBytes;
		};

		if (exceedsXGigabytes(xsize * ysize, 4))
		{
			qDebug() << "H5AD loader: Store sparse data as sparse";

			// Store sparse data as sparse
			std::vector<float> data_float = data.getVectorAs<float>();
			std::vector<size_t> column_index = indices.getVectorAs<size_t>();
			std::vector<size_t> row_offset = indptr.getVectorAs<size_t>();

			auto numericalDataset_p = static_cast<Points*>(numericalDataset.getDataset());

			Points::Experimental::setSparseData(numericalDataset_p, xsize, ysize, std::move(column_index), std::move(row_offset), std::move(data_float));

			// Experimental: other plugins cannot yet handle this sparse data
			numericalDataset->lock();
		}
		else
		{
			qDebug() << "H5AD loader: Store sparse data as dense";

			// Store sparse data as dense
			DataContainerInterface dci(numericalDataset);
			dci.resize(xsize, ysize);
			dci.set_sparse_row_data(indices, indptr, data, TRANSFORM::None());
		}

		events().notifyDatasetDataChanged(numericalDataset);

		// Dimension names
		std::vector<QString> dimensionNames(ysize);
		for (std::size_t l = 0; l < ysize; ++l)
			dimensionNames[l] = QString::number(l);

		numericalDataset->setDimensionNames(dimensionNames);

		events().notifyDatasetDataChanged(numericalDataset);
		
		return loadSuccess;
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
			if ((*it)->getId() == name)
			{
				return *it;
			}

			if((*it)->getDataset()->getGuiName() == name)
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



	bool load_X(std::unique_ptr<H5::H5File>& h5fILE, LoaderInfo &loaderInfo, int storageType, ProgressCallback progress)
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
						if (!loaderInfo._useTiledStorage || !loadDenseDataAsTiled(dataset, loaderInfo, storageType, progress))
							H5AD::LoadData(dataset, loaderInfo, storageType);
						break;

					}
				}
				else if (objectType1 == H5G_GROUP)
				{
					if (objectName1 == "X")
					{
						H5::Group group = h5fILE->openGroup(objectName1);
						if (!loaderInfo._useTiledStorage || !loadSparseDataAsTiled(group, loaderInfo, progress))
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
			throw std::runtime_error(std::string("H5AD point-data staging failed: ") + e.what());
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
	void LoadSampleNamesAndMetaData(H5::DataSet& dataset, LoaderInfo &loaderInfo, const MetadataProgressCallback& progress)
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

			std::size_t componentIndex = 0;
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

				if (progress)
					progress(static_cast<float>(++componentIndex) / static_cast<float>(std::max<std::size_t>(1, nrOfComponents)), QString::fromStdString(component->first));

			}

			Dataset<Points> numericalMetaDataset = H5Utils::addNumericalMetaData(numericalMetaData, numericalMetaDataDimensionNames, true, loaderInfo._pointsDataset, h5datasetName.c_str());
			numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
		}
	}


	template<typename numericalMetaDataType>
	void LoadSampleNamesAndMetaData(H5::Group& group, LoaderInfo& loaderInfo, const MetadataProgressCallback& progress)
	{
		static_assert(std::is_same<numericalMetaDataType, float>::value, "");
		auto nrOfObjects = group.getNumObjs();

		std::filesystem::path path(group.getObjName());
		std::string h5GroupName = path.filename().string();

		if (ContainsSparseMatrix(group))
		{
			bool loadSparseSuccess = LoadSparseMatrix(group, loaderInfo);
			if (progress)
				progress(1.0f, QString::fromStdString(h5GroupName));
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
									events().notifyDatasetDataChanged(foundDataset->getDataset());
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
																	events().notifyDatasetDataChanged(foundDataset->getDataset());
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
										// multi-dimensional,  only 2 supported for now
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

															events().notifyDatasetDataChanged(foundDataset->getDataset());
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
						LoadSampleNamesAndMetaData<numericalMetaDataType>(group2, loaderInfo, progress);
					}

					if (progress)
						progress(static_cast<float>(load_colors * nrOfObjects + go + 1) / static_cast<float>(std::max<hsize_t>(1, nrOfObjects * 2)), QString::fromStdString(objectName1));
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

	void LoadSampleNamesAndMetaDataFloat(H5::DataSet& dataset, LoaderInfo &loaderInfo, MetadataProgressCallback progress)
	{
		 H5AD::LoadSampleNamesAndMetaData<float>(dataset, loaderInfo, progress);
	}

	void LoadSampleNamesAndMetaDataFloat(H5::Group& group, LoaderInfo& loaderInfo, MetadataProgressCallback progress)
	{
		 H5AD::LoadSampleNamesAndMetaData<float>(group, loaderInfo, progress);
	}
	

}// namespace
