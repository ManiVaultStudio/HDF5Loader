#include "H5ADUtils.h"

#include "DataContainerInterface.h"

#include <CoreInterface.h>

#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QTextStream>
#include <QUuid>

#include <PointData/DimensionsPickerAction.h>
#include <PointData/storage/sparse/SparsePointsStorageBackend.h>
#include <PointData/storage/tiled/TiledPointsStorageBackend.h>
#include <util/BlobCodec.h>
#include <util/CodecRegistry.h>
#include <util/Serialization.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <deque>
#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <list>
#include <limits>
#include <memory>
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

		bool shouldLoadClusterSet(const LoaderInfo& loaderInfo, const std::string& objectName)
		{
			if (!loaderInfo._filterClusterSets)
				return true;

			auto key = QString::fromLocal8Bit(objectName.c_str());
			while (key.startsWith('/'))
				key.remove(0, 1);
			if (key.startsWith("obs/"))
				key.remove(0, 4);
			else if (key == "obs")
				key.clear();

			if (key.isEmpty() || !loaderInfo._availableClusterSets.contains(key))
				return true;

			return loaderInfo._selectedClusterSets.contains(key);
		}

		PointData::ElementTypeSpecifier getTiledElementType(const H5::DataType& dataType, const int storageType)
		{
			if (storageType >= 0)
				return static_cast<PointData::ElementTypeSpecifier>(storageType);

			const auto dataClass = dataType.getClass();
			if (storageType == -2 && dataClass == H5T_FLOAT)
				return PointData::ElementTypeSpecifier::bfloat16;

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
		T convertSparseValue(const float value)
		{
			if (!std::isfinite(value))
				return T {};

			const auto truncatedValue = std::trunc(static_cast<double>(value));
			const auto boundedValue = std::clamp(truncatedValue, static_cast<double>(std::numeric_limits<T>::lowest()), static_cast<double>(std::numeric_limits<T>::max()));
			return static_cast<T>(boundedValue);
		}

		void writeSparseTileValue(QByteArray& bytes, const std::size_t index, const PointData::ElementTypeSpecifier elementType, const float value)
		{
			switch (elementType)
			{
				case PointData::ElementTypeSpecifier::bfloat16:
					reinterpret_cast<biovault::bfloat16_t*>(bytes.data())[index] = value;
					break;

				case PointData::ElementTypeSpecifier::int16:
					reinterpret_cast<std::int16_t*>(bytes.data())[index] = convertSparseValue<std::int16_t>(value);
					break;

				case PointData::ElementTypeSpecifier::uint16:
					reinterpret_cast<std::uint16_t*>(bytes.data())[index] = convertSparseValue<std::uint16_t>(value);
					break;

				case PointData::ElementTypeSpecifier::int32:
					reinterpret_cast<std::int32_t*>(bytes.data())[index] = convertSparseValue<std::int32_t>(value);
					break;

				case PointData::ElementTypeSpecifier::uint32:
					reinterpret_cast<std::uint32_t*>(bytes.data())[index] = convertSparseValue<std::uint32_t>(value);
					break;

				case PointData::ElementTypeSpecifier::int8:
					reinterpret_cast<std::int8_t*>(bytes.data())[index] = convertSparseValue<std::int8_t>(value);
					break;

				case PointData::ElementTypeSpecifier::uint8:
					reinterpret_cast<std::uint8_t*>(bytes.data())[index] = convertSparseValue<std::uint8_t>(value);
					break;

				default:
					reinterpret_cast<float*>(bytes.data())[index] = value;
					break;
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

		template<typename T>
		std::vector<T> readH5ADRange(const H5::DataSet& dataset, std::uint64_t offset, std::uint64_t count, const H5::DataType& readType);

		template<typename T>
		std::vector<std::uint8_t> readSparseValuesAs(const H5::DataSet& dataset, const std::uint64_t count, const H5::DataType& readType)
		{
			const auto typedValues = readH5ADRange<T>(dataset, 0, count, readType);
			std::vector<std::uint8_t> values(typedValues.size() * sizeof(T));
			if (!values.empty())
				std::memcpy(values.data(), typedValues.data(), values.size());
			return values;
		}

		std::vector<std::uint8_t> readSparseValues(const H5::DataSet& dataset, const std::uint64_t count, const PointData::ElementTypeSpecifier elementType)
		{
			if (elementType == PointData::ElementTypeSpecifier::bfloat16)
			{
				const auto floatValues = readH5ADRange<float>(dataset, 0, count, H5::PredType::NATIVE_FLOAT);
				std::vector<biovault::bfloat16_t> typedValues(floatValues.size());
				for (std::size_t index = 0; index < floatValues.size(); ++index)
					typedValues[index] = floatValues[index];
				std::vector<std::uint8_t> values(typedValues.size() * sizeof(biovault::bfloat16_t));
				if (!values.empty())
					std::memcpy(values.data(), typedValues.data(), values.size());
				return values;
			}

			const auto readType = getTiledReadType(elementType);
			switch (elementType)
			{
				case PointData::ElementTypeSpecifier::float32:
					return readSparseValuesAs<float>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::int32:
					return readSparseValuesAs<std::int32_t>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::uint32:
					return readSparseValuesAs<std::uint32_t>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::int16:
					return readSparseValuesAs<std::int16_t>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::uint16:
					return readSparseValuesAs<std::uint16_t>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::int8:
					return readSparseValuesAs<std::int8_t>(dataset, count, readType);

				case PointData::ElementTypeSpecifier::uint8:
					return readSparseValuesAs<std::uint8_t>(dataset, count, readType);

				default:
					throw std::runtime_error("Unsupported sparse H5AD element type");
			}
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
			struct Strip
			{
				std::uint64_t rowOffset = 0;
				std::uint64_t columnOffset = 0;
				std::uint64_t rows = 0;
				std::uint64_t columns = 0;
				mv::util::PackedBlobBlock block;
			};
			std::vector<Strip> rowStrips;
			std::vector<Strip> columnStrips;
		};

		struct StagedPackedSuperTile
		{
			QString path;
		};

		struct DenseTilePayload
		{
			std::size_t tileIndex = 0;
			QByteArray bytes;
			std::uint64_t rows = 0;
			std::uint64_t columns = 0;
			std::size_t elementSize = 0;
		};

		struct DenseSuperTileResult
		{
			std::size_t superTileIndex = 0;
			QString path;
			std::uint64_t uncompressedBytes = 0;
			std::uint64_t encodeAndWriteMilliseconds = 0;
			std::uint64_t finalizeNanoseconds = 0;
			std::uint64_t rowStripPreparationNanoseconds = 0;
			std::uint64_t rowStripEncodeNanoseconds = 0;
			std::uint64_t rowStripWriteNanoseconds = 0;
			std::uint64_t columnStripPreparationNanoseconds = 0;
			std::uint64_t columnStripEncodeNanoseconds = 0;
			std::uint64_t columnStripWriteNanoseconds = 0;
			std::uint64_t wholeTileEncodeNanoseconds = 0;
			std::uint64_t wholeTileWriteNanoseconds = 0;
			struct EncodedTile
			{
				std::size_t tileIndex = 0;
				mv::util::PackedBlobBlock block;
				std::vector<StagedPackedTile::Strip> rowStrips;
				std::vector<StagedPackedTile::Strip> columnStrips;
			};
			std::vector<EncodedTile> blocks;
		};

		DenseSuperTileResult encodeDenseSuperTile(const std::size_t superTileIndex, const QString& filePath, std::vector<DenseTilePayload> tiles, const mv::util::SharedCodec& codec, const bool stripStorage, const std::uint64_t rowStripHeight, const std::uint64_t columnStripWidth)
		{
			const auto startTime = std::chrono::steady_clock::now();
			mv::util::PackedBlobFileWriter writer(filePath, codec);
			DenseSuperTileResult result;
			result.superTileIndex = superTileIndex;
			result.path = filePath;
			result.blocks.reserve(tiles.size());

			for (const auto& tile : tiles)
			{
				result.uncompressedBytes += static_cast<std::uint64_t>(tile.bytes.size());
				DenseSuperTileResult::EncodedTile encodedTile;
				encodedTile.tileIndex = tile.tileIndex;
				if (!stripStorage)
				{
					mv::util::PackedBlobAppendTimings timings;
					encodedTile.block = writer.append(tile.bytes, timings);
					result.wholeTileEncodeNanoseconds += timings.encodeNanoseconds;
					result.wholeTileWriteNanoseconds += timings.writeNanoseconds;
				}
				else
				{
					if (rowStripHeight == 0 || columnStripWidth == 0 || tile.elementSize == 0 || tile.rows * tile.columns * tile.elementSize != static_cast<std::uint64_t>(tile.bytes.size()))
						throw std::runtime_error("Invalid tile payload geometry for experimental strip encoding");
					for (std::uint64_t firstRow = 0; firstRow < tile.rows; firstRow += rowStripHeight)
					{
						const auto preparationStart = std::chrono::steady_clock::now();
						const auto stripRows = std::min(rowStripHeight, tile.rows - firstRow);
						const auto byteOffset = firstRow * tile.columns * tile.elementSize;
						const auto byteCount = stripRows * tile.columns * tile.elementSize;
						QByteArray stripBytes(tile.bytes.constData() + byteOffset, static_cast<qsizetype>(byteCount));
						result.rowStripPreparationNanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - preparationStart).count());
						mv::util::PackedBlobAppendTimings timings;
						const auto block = writer.append(stripBytes, timings);
						result.rowStripEncodeNanoseconds += timings.encodeNanoseconds;
						result.rowStripWriteNanoseconds += timings.writeNanoseconds;
						encodedTile.rowStrips.push_back({ firstRow, 0, stripRows, tile.columns, block });
						result.uncompressedBytes += byteCount;
					}
					for (std::uint64_t firstColumn = 0; firstColumn < tile.columns; firstColumn += columnStripWidth)
					{
						const auto preparationStart = std::chrono::steady_clock::now();
						const auto stripColumns = std::min(columnStripWidth, tile.columns - firstColumn);
						QByteArray stripBytes(static_cast<qsizetype>(tile.rows * stripColumns * tile.elementSize), Qt::Uninitialized);
						for (std::uint64_t row = 0; row < tile.rows; ++row)
						{
							const auto sourceOffset = (row * tile.columns + firstColumn) * tile.elementSize;
							const auto destinationOffset = row * stripColumns * tile.elementSize;
							std::memcpy(stripBytes.data() + destinationOffset, tile.bytes.constData() + sourceOffset, static_cast<std::size_t>(stripColumns * tile.elementSize));
						}
						result.columnStripPreparationNanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - preparationStart).count());
						mv::util::PackedBlobAppendTimings timings;
						const auto block = writer.append(stripBytes, timings);
						result.columnStripEncodeNanoseconds += timings.encodeNanoseconds;
						result.columnStripWriteNanoseconds += timings.writeNanoseconds;
						encodedTile.columnStrips.push_back({ 0, firstColumn, tile.rows, stripColumns, block });
						result.uncompressedBytes += static_cast<std::uint64_t>(stripBytes.size());
					}
				}
				result.blocks.push_back(std::move(encodedTile));
			}

			const auto finalizeStart = std::chrono::steady_clock::now();
			writer.close();
			result.finalizeNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - finalizeStart).count());
			result.encodeAndWriteMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count());
			return result;
		}

		struct StagedBlobReader
		{
			explicit StagedBlobReader(const QString& filePath, const mv::util::SharedCodec& codec) :
				reader(filePath, codec)
			{
			}

			std::mutex mutex;
			mv::util::PackedBlobFileReader reader;
		};

		struct StagedReadCache
		{
			std::mutex mutex;
			std::unordered_map<std::size_t, std::shared_ptr<StagedBlobReader>> readers;
		};

		struct StagedStripKey
		{
			std::size_t superTileIndex = 0;
			std::uint64_t offset = 0;
			bool operator==(const StagedStripKey& other) const { return superTileIndex == other.superTileIndex && offset == other.offset; }
		};

		struct StagedStripKeyHash
		{
			std::size_t operator()(const StagedStripKey& key) const
			{
				const auto a = std::hash<std::size_t>{}(key.superTileIndex);
				const auto b = std::hash<std::uint64_t>{}(key.offset);
				return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
			}
		};

		struct StagedStripCache
		{
			struct Entry
			{
				std::shared_ptr<QByteArray> bytes;
				std::list<StagedStripKey>::iterator lruPosition;
			};
			std::mutex mutex;
			std::unordered_map<StagedStripKey, Entry, StagedStripKeyHash> entries;
			std::list<StagedStripKey> lru;
			std::uint64_t sizeBytes = 0;
			std::uint64_t capacityBytes = 0;
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
			bool experimentalStripStorage = false;
			std::size_t rowStripHeight = 8;
			std::size_t columnStripWidth = 8;
			mv::util::SharedCodec codec;
			QString stagingDirectory;
			std::vector<StagedPackedSuperTile> superTiles;
			std::vector<StagedPackedTile> tiles;
			mutable StagedReadCache readCache;
			mutable StagedStripCache stripCache;

			~StagedPointData()
			{
				{
					const std::lock_guard lock(readCache.mutex);
					readCache.readers.clear();
				}

				if (!stagingDirectory.isEmpty() && !QDir(stagingDirectory).removeRecursively())
					qWarning() << "Unable to remove H5AD staging directory:" << stagingDirectory;
			}
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
			stream << "ExperimentalStripStorage=" << (configuration.experimentalStripStorage ? "true" : "false") << "\n";
			stream << "RowStripHeight=" << configuration.rowStripHeight << "\n";
			stream << "ColumnStripWidth=" << configuration.columnStripWidth << "\n";
			stream << "Codec=" << codec->getName() << "\n";
			return stagingDirectory;
		}

		mv::util::SharedCodec createStagingCodec(const QString& codecName)
		{
			auto* currentProject = mv::projects().getCurrentProject();
			if (!currentProject)
				throw std::runtime_error("Unable to create H5AD staging codec without an active project");

			const auto projectCodecName = mv::util::BlobCodec::typeToString(mv::util::codecRegistry().typeFromDisplayName(currentProject->getCompressionAction().getCodecTypeAction().getCurrentText()));
			if (codecName.isEmpty() || codecName == projectCodecName)
				return currentProject->getCompressionAction().createCodec(nullptr);

			return mv::util::codecRegistry().createCodec(nullptr, codecName);
		}

		std::shared_ptr<StagedPointData> stageDenseData(const H5::DataSet& dataset, const PointData::ElementTypeSpecifier elementType, const std::uint64_t rows, const std::uint64_t columns, const QString& stagingRoot, const pointdata::storage::StorageBackendConfiguration& configuration, const H5AD::ProgressCallback& progress)
		{
			const auto stagingStartTime = std::chrono::steady_clock::now();
			const auto elementSize = getTiledElementSize(elementType);
			const auto staging = std::make_shared<StagedPointData>();
			staging->rows = rows;
			staging->columns = columns;
			staging->elementSize = elementSize;
			staging->rowTileSize = configuration.rowTileSize;
			staging->columnTileSize = configuration.columnTileSize;
			staging->superTileRows = configuration.superTileRows;
			staging->superTileColumns = configuration.superTileColumns;
			staging->experimentalStripStorage = configuration.experimentalStripStorage;
			staging->rowStripHeight = configuration.rowStripHeight;
			staging->columnStripWidth = configuration.columnStripWidth;
			staging->stripCache.capacityBytes = configuration.experimentalStripStorage ? std::min<std::uint64_t>(configuration.cacheCapacityBytes, 256ull * 1024ull * 1024ull) : 0;
			staging->tileRows = (rows + staging->rowTileSize - 1) / staging->rowTileSize;
			staging->tileColumns = (columns + staging->columnTileSize - 1) / staging->columnTileSize;
			staging->codec = createStagingCodec(configuration.tileCodecName);
			const auto stagingDirectory = createStagingDirectory(stagingRoot, rows, columns, elementSize, configuration, staging->codec);
			staging->stagingDirectory = stagingDirectory;
			const auto tileCount = staging->tileRows * staging->tileColumns;
			staging->tiles.resize(static_cast<std::size_t>(tileCount));
			std::uint64_t completedTileCount = 0;
			std::uint64_t completedReadTileCount = 0;
			std::uint64_t inFlightBytes = 0;
			std::uint64_t readMilliseconds = 0;
			std::uint64_t encodeAndWriteMilliseconds = 0;
			std::uint64_t stagedBytes = 0;
			std::uint64_t rowStripPreparationNanoseconds = 0;
			std::uint64_t rowStripEncodeNanoseconds = 0;
			std::uint64_t rowStripWriteNanoseconds = 0;
			std::uint64_t columnStripPreparationNanoseconds = 0;
			std::uint64_t columnStripEncodeNanoseconds = 0;
			std::uint64_t columnStripWriteNanoseconds = 0;
			std::uint64_t wholeTileEncodeNanoseconds = 0;
			std::uint64_t wholeTileWriteNanoseconds = 0;
			std::uint64_t finalizeNanoseconds = 0;
			std::atomic<std::size_t> activeEncoders = 0;
			std::atomic<std::size_t> maximumActiveEncoders = 0;
			constexpr std::size_t maxInFlightSuperTiles = 4;
			constexpr std::uint64_t maxInFlightBytes = 256ull * 1024ull * 1024ull;
			std::deque<std::future<DenseSuperTileResult>> encodingJobs;
			float reportedProgress = 0.0f;
			auto lastProgressLog = stagingStartTime;
			auto reportProgress = [&](const float value) {
				reportedProgress = std::max(reportedProgress, value);
				if (progress)
					progress(reportedProgress);
				const auto now = std::chrono::steady_clock::now();
				if (now - lastProgressLog >= std::chrono::seconds(10))
				{
					qInfo().noquote() << QStringLiteral("H5AD dense staging progress: read %1/%2 tiles; encoded %3/%2 tiles; HDF5 reads %4 ms; active encoders %5; queued supertiles %6; elapsed %7 ms")
						.arg(completedReadTileCount)
						.arg(tileCount)
						.arg(completedTileCount)
						.arg(readMilliseconds)
						.arg(activeEncoders.load(std::memory_order_relaxed))
						.arg(encodingJobs.size())
						.arg(std::chrono::duration_cast<std::chrono::milliseconds>(now - stagingStartTime).count());
					lastProgressLog = now;
				}
			};
			reportProgress(0.0f);

			auto collectEncodedSuperTile = [&] {
				auto encoded = encodingJobs.front().get();
				encodingJobs.pop_front();
				inFlightBytes -= encoded.uncompressedBytes;
				encodeAndWriteMilliseconds += encoded.encodeAndWriteMilliseconds;
				rowStripPreparationNanoseconds += encoded.rowStripPreparationNanoseconds;
				rowStripEncodeNanoseconds += encoded.rowStripEncodeNanoseconds;
				rowStripWriteNanoseconds += encoded.rowStripWriteNanoseconds;
				columnStripPreparationNanoseconds += encoded.columnStripPreparationNanoseconds;
				columnStripEncodeNanoseconds += encoded.columnStripEncodeNanoseconds;
				columnStripWriteNanoseconds += encoded.columnStripWriteNanoseconds;
				wholeTileEncodeNanoseconds += encoded.wholeTileEncodeNanoseconds;
				wholeTileWriteNanoseconds += encoded.wholeTileWriteNanoseconds;
				finalizeNanoseconds += encoded.finalizeNanoseconds;
				staging->superTiles[encoded.superTileIndex] = { encoded.path };
				for (auto& encodedTile : encoded.blocks)
				{
					auto& tile = staging->tiles[encodedTile.tileIndex];
					tile.block = encodedTile.block;
					tile.rowStrips = std::move(encodedTile.rowStrips);
					tile.columnStrips = std::move(encodedTile.columnStrips);
				}
				completedTileCount += encoded.blocks.size();
				reportProgress(static_cast<float>(0.8 + 0.2 * static_cast<double>(completedTileCount) / static_cast<double>(tileCount)));
			};

			for (std::uint64_t superTileRow = 0; superTileRow < staging->tileRows; superTileRow += staging->superTileRows)
			{
				for (std::uint64_t superTileColumn = 0; superTileColumn < staging->tileColumns; superTileColumn += staging->superTileColumns)
				{
					const auto superTileIndex = static_cast<std::size_t>(superTileRow / staging->superTileRows * ((staging->tileColumns + staging->superTileColumns - 1) / staging->superTileColumns) + superTileColumn / staging->superTileColumns);
					const auto fileName = QStringLiteral("supertile_%1.ptpack").arg(superTileIndex, 8, 10, QLatin1Char('0'));
					const auto filePath = QDir(stagingDirectory).filePath(fileName);
					const auto lastTileRow = std::min<std::uint64_t>(staging->tileRows, superTileRow + staging->superTileRows);
					const auto lastTileColumn = std::min<std::uint64_t>(staging->tileColumns, superTileColumn + staging->superTileColumns);
					std::uint64_t superTileByteCount = 0;
					for (std::uint64_t tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
					{
						for (std::uint64_t tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
						{
							const auto rowOffset = tileRow * staging->rowTileSize;
							const auto columnOffset = tileColumn * staging->columnTileSize;
							const auto rowsInTile = std::min<std::uint64_t>(staging->rowTileSize, rows - rowOffset);
							const auto columnsInTile = std::min<std::uint64_t>(staging->columnTileSize, columns - columnOffset);
							superTileByteCount += rowsInTile * columnsInTile * elementSize;
						}
					}

					const auto encodedSuperTileByteCount = superTileByteCount * (staging->experimentalStripStorage ? 3 : 1);
					while (!encodingJobs.empty() && (encodingJobs.size() >= maxInFlightSuperTiles || inFlightBytes >= maxInFlightBytes || encodedSuperTileByteCount > maxInFlightBytes - inFlightBytes))
						collectEncodedSuperTile();

					std::vector<DenseTilePayload> tilePayloads;
					tilePayloads.reserve(static_cast<std::size_t>((lastTileRow - superTileRow) * (lastTileColumn - superTileColumn)));

					for (std::uint64_t tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
					{
						for (std::uint64_t tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
						{
							reportProgress(static_cast<float>(0.8 * static_cast<double>(completedReadTileCount) / static_cast<double>(tileCount)));

							const auto rowOffset = tileRow * staging->rowTileSize;
							const auto columnOffset = tileColumn * staging->columnTileSize;
							const auto tileRows = std::min<std::uint64_t>(staging->rowTileSize, rows - rowOffset);
							const auto tileColumns = std::min<std::uint64_t>(staging->columnTileSize, columns - columnOffset);
							const auto readStartTime = std::chrono::steady_clock::now();
							const auto bytes = readH5ADTile(dataset, elementType, rowOffset, columnOffset, tileRows, tileColumns);
							readMilliseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - readStartTime).count());
							stagedBytes += static_cast<std::uint64_t>(bytes.size());

							const auto tileIndex = static_cast<std::size_t>(tileRow * staging->tileColumns + tileColumn);
							staging->tiles[tileIndex] = { rowOffset, columnOffset, tileRows, tileColumns, superTileIndex, {} };
							tilePayloads.push_back({ tileIndex, bytes, tileRows, tileColumns, elementSize });
							++completedReadTileCount;
						}
					}

					const auto codec = createStagingCodec(configuration.tileCodecName);
					staging->superTiles.resize(std::max(staging->superTiles.size(), superTileIndex + 1));
					inFlightBytes += encodedSuperTileByteCount;
					const auto stripStorage = staging->experimentalStripStorage;
					const auto rowStripHeight = static_cast<std::uint64_t>(staging->rowStripHeight);
					const auto columnStripWidth = static_cast<std::uint64_t>(staging->columnStripWidth);
					encodingJobs.emplace_back(std::async(std::launch::async, [superTileIndex, filePath, tilePayloads = std::move(tilePayloads), codec, stripStorage, rowStripHeight, columnStripWidth, &activeEncoders, &maximumActiveEncoders]() mutable {
						struct ActiveEncoder
						{
							std::atomic<std::size_t>& active;
							explicit ActiveEncoder(std::atomic<std::size_t>& count) : active(count) { active.fetch_add(1, std::memory_order_relaxed); }
							~ActiveEncoder() { active.fetch_sub(1, std::memory_order_relaxed); }
						} activeEncoder(activeEncoders);
						const auto active = activeEncoders.load(std::memory_order_relaxed);
						auto observed = maximumActiveEncoders.load(std::memory_order_relaxed);
						while (observed < active && !maximumActiveEncoders.compare_exchange_weak(observed, active, std::memory_order_relaxed))
						{}
						return encodeDenseSuperTile(superTileIndex, filePath, std::move(tilePayloads), codec, stripStorage, rowStripHeight, columnStripWidth);
					}));
				}
			}

			while (!encodingJobs.empty())
				collectEncodedSuperTile();

			const auto datasetProperties = dataset.getCreatePlist();
			QString sourceLayout = QStringLiteral("contiguous");
			if (datasetProperties.getLayout() == H5D_CHUNKED)
			{
				hsize_t sourceChunkDimensions[2] = {};
				datasetProperties.getChunk(2, sourceChunkDimensions);
				sourceLayout = QStringLiteral("chunked %1x%2").arg(sourceChunkDimensions[0]).arg(sourceChunkDimensions[1]);
			}
			const auto totalMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stagingStartTime).count());
			std::uint64_t packedFileBytes = 0;
			std::uint64_t rowStripCount = 0;
			std::uint64_t columnStripCount = 0;
			for (const auto& superTile : staging->superTiles)
				packedFileBytes += static_cast<std::uint64_t>(QFileInfo(superTile.path).size());
			for (const auto& tile : staging->tiles)
			{
				rowStripCount += tile.rowStrips.size();
				columnStripCount += tile.columnStrips.size();
			}
			qInfo().noquote() << QStringLiteral("H5AD dense staging: %1 MiB in %2 ms; HDF5 reads %3 ms; encode/write %4 worker-ms; row strips: prepare %5 worker-ms, codec %6 worker-ms, write %7 worker-ms; column strips: prepare %8 worker-ms, codec %9 worker-ms, write %10 worker-ms; whole tiles: codec %11 worker-ms, write %12 worker-ms; file finalize %13 worker-ms; encoder peak %14/%15; source %16; %17 tiles; %18 packed MiB; format %19 (%20 row and %21 column strips)")
				.arg(static_cast<double>(stagedBytes) / (1024.0 * 1024.0), 0, 'f', 1)
				.arg(totalMilliseconds)
				.arg(readMilliseconds)
				.arg(encodeAndWriteMilliseconds)
				.arg(static_cast<double>(rowStripPreparationNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(rowStripEncodeNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(rowStripWriteNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripPreparationNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripEncodeNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripWriteNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(wholeTileEncodeNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(wholeTileWriteNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(finalizeNanoseconds) / 1.0e6, 0, 'f', 1)
				.arg(maximumActiveEncoders.load(std::memory_order_relaxed))
				.arg(std::size_t{ 4 })
				.arg(sourceLayout)
				.arg(tileCount)
				.arg(static_cast<double>(packedFileBytes) / (1024.0 * 1024.0), 0, 'f', 1)
				.arg(staging->experimentalStripStorage ? QStringLiteral("%1x%2 experimental strip").arg(staging->rowStripHeight).arg(staging->columnStripWidth) : QStringLiteral("whole tile"))
				.arg(rowStripCount)
				.arg(columnStripCount);

			return staging;
		}

		std::shared_ptr<StagedBlobReader> getStagedBlobReader(const StagedPointData& staging, const std::size_t superTileIndex)
		{
			if (superTileIndex >= staging.superTiles.size())
				throw std::out_of_range("Requested block references a missing H5AD packed super-tile");

			const std::lock_guard cacheLock(staging.readCache.mutex);
			auto readerIt = staging.readCache.readers.find(superTileIndex);
			if (readerIt != staging.readCache.readers.end())
				return readerIt->second;

			const auto& superTile = staging.superTiles[superTileIndex];
			auto reader = std::make_shared<StagedBlobReader>(superTile.path, staging.codec);
			staging.readCache.readers.emplace(superTileIndex, reader);
			return reader;
		}

		QByteArray readStagedBlock(const StagedPointData& staging, const std::size_t superTileIndex, const mv::util::PackedBlobBlock& block)
		{
			const StagedStripKey key { superTileIndex, block.offset };
			{
				const std::lock_guard cacheLock(staging.stripCache.mutex);
				auto cached = staging.stripCache.entries.find(key);
				if (cached != staging.stripCache.entries.end())
				{
					staging.stripCache.lru.splice(staging.stripCache.lru.begin(), staging.stripCache.lru, cached->second.lruPosition);
					return *cached->second.bytes;
				}
			}

			const auto reader = getStagedBlobReader(staging, superTileIndex);
			QByteArray decoded;
			{
				const std::lock_guard readerLock(reader->mutex);
				{
					const std::lock_guard cacheLock(staging.stripCache.mutex);
					auto cached = staging.stripCache.entries.find(key);
					if (cached != staging.stripCache.entries.end())
					{
						staging.stripCache.lru.splice(staging.stripCache.lru.begin(), staging.stripCache.lru, cached->second.lruPosition);
						return *cached->second.bytes;
					}
				}
				decoded = reader->reader.read(block);
			}

			if (static_cast<std::uint64_t>(decoded.size()) <= staging.stripCache.capacityBytes)
			{
				const std::lock_guard cacheLock(staging.stripCache.mutex);
				while (!staging.stripCache.lru.empty() && staging.stripCache.sizeBytes + static_cast<std::uint64_t>(decoded.size()) > staging.stripCache.capacityBytes)
				{
					const auto oldestKey = staging.stripCache.lru.back();
					const auto oldest = staging.stripCache.entries.find(oldestKey);
					if (oldest != staging.stripCache.entries.end())
					{
						staging.stripCache.sizeBytes -= static_cast<std::uint64_t>(oldest->second.bytes->size());
						staging.stripCache.entries.erase(oldest);
					}
					staging.stripCache.lru.pop_back();
				}
				staging.stripCache.lru.push_front(key);
				auto bytes = std::make_shared<QByteArray>(decoded);
				staging.stripCache.sizeBytes += static_cast<std::uint64_t>(bytes->size());
				staging.stripCache.entries.emplace(key, StagedStripCache::Entry { std::move(bytes), staging.stripCache.lru.begin() });
			}
			return decoded;
		}

		QByteArray readStagedRange(const StagedPointData& staging, const std::uint64_t firstRow, const std::uint64_t rowCount, const std::uint64_t firstColumn, const std::uint64_t columnCount)
		{
			if (firstRow > staging.rows || rowCount > staging.rows - firstRow || firstColumn > staging.columns || columnCount > staging.columns - firstColumn)
				throw std::out_of_range("Requested H5AD staged range is out of bounds");
			if (rowCount == 0 || columnCount == 0)
				return {};
			if (rowCount > static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max()) / columnCount / staging.elementSize)
				throw std::overflow_error("Requested H5AD staged range exceeds the supported size");

			QByteArray output(static_cast<qsizetype>(rowCount * columnCount * staging.elementSize), Qt::Uninitialized);
			auto* outputBytes = output.data();
			const auto lastRow = firstRow + rowCount;
			const auto lastColumn = firstColumn + columnCount;
			const auto firstTileRow = firstRow / staging.rowTileSize;
			const auto lastTileRow = (lastRow - 1) / staging.rowTileSize;
			const auto firstTileColumn = firstColumn / staging.columnTileSize;
			const auto lastTileColumn = (lastColumn - 1) / staging.columnTileSize;
			const auto processTile = [&](const std::uint64_t tileRow, const std::uint64_t tileColumn) {
				const auto tileIndex = static_cast<std::size_t>(tileRow * staging.tileColumns + tileColumn);
				const auto& tile = staging.tiles.at(tileIndex);
				const auto tileFirstRow = std::max(firstRow, tile.rowOffset);
				const auto tileLastRow = std::min(lastRow, tile.rowOffset + tile.rows);
				const auto tileFirstColumn = std::max(firstColumn, tile.columnOffset);
				const auto tileLastColumn = std::min(lastColumn, tile.columnOffset + tile.columns);
				if (tile.rowStrips.empty() || tile.columnStrips.empty())
					throw std::runtime_error("Experimental strip metadata is missing for an H5AD tile");

				std::uint64_t rowCost = 0;
				for (const auto& strip : tile.rowStrips)
					if (strip.rowOffset < tileLastRow - tile.rowOffset && strip.rowOffset + strip.rows > tileFirstRow - tile.rowOffset)
						rowCost += strip.block.uncompressedSize;
				std::uint64_t columnCost = 0;
				for (const auto& strip : tile.columnStrips)
					if (strip.columnOffset < tileLastColumn - tile.columnOffset && strip.columnOffset + strip.columns > tileFirstColumn - tile.columnOffset)
						columnCost += strip.block.uncompressedSize;

				const auto copyStrip = [&](const StagedPackedTile::Strip& strip, const bool rowOriented) {
					const auto stripStart = rowOriented ? strip.rowOffset : strip.columnOffset;
					const auto requestStart = rowOriented ? tileFirstRow - tile.rowOffset : tileFirstColumn - tile.columnOffset;
					const auto requestEnd = rowOriented ? tileLastRow - tile.rowOffset : tileLastColumn - tile.columnOffset;
					const auto stripExtent = rowOriented ? strip.rows : strip.columns;
					if (stripStart >= requestEnd || stripStart + stripExtent <= requestStart)
						return;

					const auto stripBytes = readStagedBlock(staging, tile.superTileIndex, strip.block);
					if (stripBytes.size() != static_cast<qsizetype>(strip.block.uncompressedSize))
						throw std::runtime_error("Decoded H5AD strip has an unexpected size");
					const auto localFirstRow = rowOriented ? std::max(tileFirstRow - tile.rowOffset, strip.rowOffset) : tileFirstRow - tile.rowOffset;
					const auto localLastRow = rowOriented ? std::min(tileLastRow - tile.rowOffset, strip.rowOffset + strip.rows) : tileLastRow - tile.rowOffset;
					const auto localFirstColumn = tileFirstColumn - tile.columnOffset;
					const auto localLastColumn = tileLastColumn - tile.columnOffset;
					for (auto row = localFirstRow; row < localLastRow; ++row)
					{
						const auto copyFirstColumn = rowOriented ? localFirstColumn : std::max(localFirstColumn, strip.columnOffset);
						const auto copyLastColumn = rowOriented ? localLastColumn : std::min(localLastColumn, strip.columnOffset + strip.columns);
						const auto sourceColumn = rowOriented ? copyFirstColumn : copyFirstColumn - strip.columnOffset;
						const auto sourceRow = rowOriented ? row - strip.rowOffset : row;
						const auto sourceWidth = rowOriented ? tile.columns : strip.columns;
						const auto destinationOffset = ((tile.rowOffset + row - firstRow) * columnCount + (tile.columnOffset + copyFirstColumn - firstColumn)) * staging.elementSize;
						const auto sourceOffset = (sourceRow * sourceWidth + sourceColumn) * staging.elementSize;
						const auto copyBytes = (copyLastColumn - copyFirstColumn) * staging.elementSize;
						std::memcpy(outputBytes + destinationOffset, stripBytes.constData() + sourceOffset, static_cast<std::size_t>(copyBytes));
					}
				};

				if (rowCost <= columnCost)
				{
					for (const auto& strip : tile.rowStrips)
						copyStrip(strip, true);
				}
				else
				{
					for (const auto& strip : tile.columnStrips)
						copyStrip(strip, false);
				}
			};

			constexpr std::size_t maxConcurrentStripReads = 8;
			std::deque<std::future<void>> rangeJobs;
			for (auto tileRow = firstTileRow; tileRow <= lastTileRow; ++tileRow)
			{
				for (auto tileColumn = firstTileColumn; tileColumn <= lastTileColumn; ++tileColumn)
				{
					rangeJobs.emplace_back(std::async(std::launch::async, processTile, tileRow, tileColumn));
					if (rangeJobs.size() >= maxConcurrentStripReads)
					{
						rangeJobs.front().get();
						rangeJobs.pop_front();
					}
				}
			}
			while (!rangeJobs.empty())
			{
				rangeJobs.front().get();
				rangeJobs.pop_front();
			}
			return output;
		}

		QByteArray readStagedTile(const StagedPointData& staging, const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t rows, const std::uint64_t columns)
		{
			const auto tileRow = rowOffset / staging.rowTileSize;
			const auto tileColumn = columnOffset / staging.columnTileSize;
			const auto tileIndex = static_cast<std::size_t>(tileRow * staging.tileColumns + tileColumn);
			if (tileIndex >= staging.tiles.size())
				throw std::out_of_range("Requested tile is not present in H5AD packed staging");

			const auto& tile = staging.tiles[tileIndex];
			if (tile.rowOffset != rowOffset || tile.columnOffset != columnOffset || tile.rows != rows || tile.columns != columns)
				throw std::out_of_range("Requested tile does not match H5AD packed staging geometry");
			if (tile.superTileIndex >= staging.superTiles.size())
				throw std::out_of_range("Requested tile references a missing H5AD packed super-tile");
			if (staging.experimentalStripStorage)
				return readStagedRange(staging, rowOffset, rows, columnOffset, columns);

			return readStagedBlock(staging, tile.superTileIndex, tile.block);
		}

		mv::pointdata::storage::TiledStorageBackendCreationOptions::PackedTileSource createPackedTileSource(const std::shared_ptr<StagedPointData>& staging)
		{
			mv::pointdata::storage::TiledStorageBackendCreationOptions::PackedTileSource packedSource;
			packedSource.filePaths.reserve(static_cast<qsizetype>(staging->superTiles.size()));
			for (const auto& superTile : staging->superTiles)
				packedSource.filePaths.push_back(superTile.path);

			packedSource.offsets.reserve(staging->tiles.size());
			packedSource.compressedSizes.reserve(staging->tiles.size());
			packedSource.uncompressedSizes.reserve(staging->tiles.size());
			packedSource.superTileIndices.reserve(staging->tiles.size());
			for (const auto& tile : staging->tiles)
			{
				packedSource.offsets.push_back(tile.block.offset);
				packedSource.compressedSizes.push_back(tile.block.compressedSize);
				packedSource.uncompressedSizes.push_back(tile.block.uncompressedSize);
				packedSource.superTileIndices.push_back(tile.superTileIndex);
			}

			packedSource.codecName = staging->codec->getName();
			packedSource.sourceLifetime = staging;
			return packedSource;
		}

		std::unique_ptr<mv::pointdata::storage::PointsStorageBackend> createStagedTiledBackend(const std::uint64_t rows, const std::uint64_t columns, const Points::ElementTypeSpecifier elementType, const mv::pointdata::storage::StorageBackendConfiguration& configuration, const std::shared_ptr<StagedPointData>& staging)
		{
			auto creation = std::make_shared<mv::pointdata::storage::TiledStorageBackendCreationOptions>();
			creation->rows = rows;
			creation->columns = columns;
			creation->elementSize = getTiledElementSize(elementType);
			creation->elementTypeIndex = static_cast<std::uint32_t>(elementType);
			creation->rowTileSize = configuration.rowTileSize;
			creation->columnTileSize = configuration.columnTileSize;
			creation->cacheCapacity = configuration.cacheCapacity;
			creation->cacheCapacityBytes = configuration.cacheCapacityBytes;
			creation->packedTiles = configuration.packedTiles;
			creation->superTileRows = configuration.superTileRows;
			creation->superTileColumns = configuration.superTileColumns;
			creation->tileCodecName = configuration.tileCodecName;
			creation->loaderThreadSafe = true;
			creation->generator = [staging](const std::uint64_t rowOffset, const std::uint64_t columnOffset, const std::uint64_t tileRows, const std::uint64_t tileColumns) {
				return readStagedTile(*staging, rowOffset, columnOffset, tileRows, tileColumns);
			};
			// PackedTileSource is accepted only when the backend itself is configured for packed tiles.
			if (configuration.packedTiles && !configuration.experimentalStripStorage)
				creation->packedSource = createPackedTileSource(staging);

			auto tiledStorage = mv::pointdata::storage::TiledPointsStorageBackend::create(*creation);
			auto tiledBackend = std::make_unique<mv::pointdata::storage::TiledPointsStorageBackend>(tiledStorage, static_cast<std::uint32_t>(elementType));
			if (configuration.experimentalStripStorage)
				tiledBackend->setRangeLoader([staging](const std::uint64_t firstRow, const std::uint64_t rowCount, const std::uint64_t firstColumn, const std::uint64_t columnCount) {
					return readStagedRange(*staging, firstRow, rowCount, firstColumn, columnCount);
				});
			return tiledBackend;
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

		std::shared_ptr<StagedPointData> stageSparseData(const H5::DataSet& dataSet, const H5::DataSet& indicesSet, const H5::DataSet& indptrSet, const std::uint64_t rows, const std::uint64_t columns, const QString& stagingRoot, const PointData::ElementTypeSpecifier elementType, const pointdata::storage::StorageBackendConfiguration& configuration, const std::size_t bucketingThreadCount, const std::size_t stagingThreadCount, const std::size_t rowChunkTileFactor, const H5AD::ProgressCallback& progress)
		{
			struct SparseTileEntry
			{
				std::uint64_t index = 0;
				std::uint64_t row = 0;
			};
			struct PendingEncoding
			{
				std::future<DenseSuperTileResult> future;
				std::uint64_t bytes = 0;
			};
			struct ActiveCounter
			{
				std::atomic<std::size_t>& counter;
				explicit ActiveCounter(std::atomic<std::size_t>& activeCounter) : counter(activeCounter) { counter.fetch_add(1, std::memory_order_relaxed); }
				~ActiveCounter() { counter.fetch_sub(1, std::memory_order_relaxed); }
			};

			const auto elementSize = getTiledElementSize(elementType);
			const auto staging = std::make_shared<StagedPointData>();
			staging->rows = rows;
			staging->columns = columns;
			staging->elementSize = elementSize;
			staging->rowTileSize = configuration.rowTileSize;
			staging->columnTileSize = configuration.columnTileSize;
			staging->superTileRows = configuration.superTileRows;
			staging->superTileColumns = configuration.superTileColumns;
			staging->experimentalStripStorage = configuration.experimentalStripStorage;
			staging->rowStripHeight = configuration.rowStripHeight;
			staging->columnStripWidth = configuration.columnStripWidth;
			staging->stripCache.capacityBytes = configuration.experimentalStripStorage ? std::min<std::uint64_t>(configuration.cacheCapacityBytes, 256ull * 1024ull * 1024ull) : 0;
			staging->tileRows = (rows + staging->rowTileSize - 1) / staging->rowTileSize;
			staging->tileColumns = (columns + staging->columnTileSize - 1) / staging->columnTileSize;
			staging->codec = createStagingCodec(configuration.tileCodecName);
			const auto stagingDirectory = createStagingDirectory(stagingRoot, rows, columns, elementSize, configuration, staging->codec);
			staging->stagingDirectory = stagingDirectory;
			const auto tileCount = staging->tileRows * staging->tileColumns;
			staging->tiles.resize(static_cast<std::size_t>(tileCount));
			std::uint64_t completedTileCount = 0;
			const auto effectiveBucketingWorkerCount = std::max<std::size_t>(1, bucketingThreadCount);
			const auto maxPendingEncoders = std::max<std::size_t>(1, stagingThreadCount);
			const auto requestedChunkTileRows = std::max<std::size_t>(staging->superTileRows, rowChunkTileFactor);
			const auto chunkTileRows = ((requestedChunkTileRows + staging->superTileRows - 1) / staging->superTileRows) * staging->superTileRows;
			constexpr std::uint64_t maxPendingTileBytes = 4ull * 1024ull * 1024ull * 1024ull;
			const auto columnSuperTileCount = (staging->tileColumns + staging->superTileColumns - 1) / staging->superTileColumns;
			std::deque<PendingEncoding> pending;
			std::uint64_t pendingBytes = 0;
			std::atomic<std::uint64_t> hdf5ReadMilliseconds = 0;
			std::atomic<std::uint64_t> stagedNonZeroCount = 0;
			std::atomic<std::uint64_t> bucketingWorkerMilliseconds = 0;
			std::atomic<std::uint64_t> materializationWorkerMilliseconds = 0;
			std::atomic<std::uint64_t> encodeAndWriteWorkerMilliseconds = 0;
			std::atomic<std::uint64_t> rowStripPreparationNanoseconds = 0;
			std::atomic<std::uint64_t> rowStripEncodeNanoseconds = 0;
			std::atomic<std::uint64_t> rowStripWriteNanoseconds = 0;
			std::atomic<std::uint64_t> columnStripPreparationNanoseconds = 0;
			std::atomic<std::uint64_t> columnStripEncodeNanoseconds = 0;
			std::atomic<std::uint64_t> columnStripWriteNanoseconds = 0;
			std::atomic<std::uint64_t> finalizeNanoseconds = 0;
			std::atomic<std::size_t> activeBucketers = 0;
			std::atomic<std::size_t> maximumActiveBucketers = 0;
			std::atomic<std::size_t> activeEncoders = 0;
			std::atomic<std::size_t> maximumActiveEncoders = 0;
			const auto stagingStart = std::chrono::steady_clock::now();
			float reportedProgress = 0.0f;
			auto lastProgressLog = stagingStart;
			auto reportProgress = [&](const float value) {
				reportedProgress = std::max(reportedProgress, value);
				if (progress)
					progress(reportedProgress);
				const auto now = std::chrono::steady_clock::now();
				if (now - lastProgressLog >= std::chrono::seconds(10))
				{
					qInfo().noquote() << QStringLiteral("H5AD sparse CSR staging progress: encoded %1/%2 tiles; HDF5 reads %3 ms; active encoders %4/%5; queued encoders %6; elapsed %7 ms")
						.arg(completedTileCount)
						.arg(tileCount)
						.arg(hdf5ReadMilliseconds.load(std::memory_order_relaxed))
						.arg(activeEncoders.load(std::memory_order_relaxed))
						.arg(maxPendingEncoders)
						.arg(pending.size())
						.arg(std::chrono::duration_cast<std::chrono::milliseconds>(now - stagingStart).count());
					lastProgressLog = now;
				}
			};
			auto updateMaximum = [](std::atomic<std::size_t>& maximum, const std::size_t value) {
				auto observed = maximum.load(std::memory_order_relaxed);
				while (observed < value && !maximum.compare_exchange_weak(observed, value, std::memory_order_relaxed))
				{}
			};
			auto collectOldest = [&]() {
				auto encoded = pending.front().future.get();
				encodeAndWriteWorkerMilliseconds.fetch_add(encoded.encodeAndWriteMilliseconds, std::memory_order_relaxed);
				rowStripPreparationNanoseconds.fetch_add(encoded.rowStripPreparationNanoseconds, std::memory_order_relaxed);
				rowStripEncodeNanoseconds.fetch_add(encoded.rowStripEncodeNanoseconds, std::memory_order_relaxed);
				rowStripWriteNanoseconds.fetch_add(encoded.rowStripWriteNanoseconds, std::memory_order_relaxed);
				columnStripPreparationNanoseconds.fetch_add(encoded.columnStripPreparationNanoseconds, std::memory_order_relaxed);
				columnStripEncodeNanoseconds.fetch_add(encoded.columnStripEncodeNanoseconds, std::memory_order_relaxed);
				columnStripWriteNanoseconds.fetch_add(encoded.columnStripWriteNanoseconds, std::memory_order_relaxed);
				finalizeNanoseconds.fetch_add(encoded.finalizeNanoseconds, std::memory_order_relaxed);
				pendingBytes -= pending.front().bytes;
				pending.pop_front();
				staging->superTiles[encoded.superTileIndex] = { encoded.path };
				for (auto& encodedTile : encoded.blocks)
				{
					auto& tile = staging->tiles[encodedTile.tileIndex];
					tile.block = encodedTile.block;
					tile.rowStrips = std::move(encodedTile.rowStrips);
					tile.columnStrips = std::move(encodedTile.columnStrips);
					++completedTileCount;
				}
				reportProgress(0.4f + 0.6f * static_cast<float>(completedTileCount) / static_cast<float>(tileCount));
			};
			reportProgress(0.0f);

			for (std::uint64_t superTileRow = 0; superTileRow < staging->tileRows; superTileRow += chunkTileRows)
			{
				const auto sourceRowOffset = superTileRow * staging->rowTileSize;
				const auto lastTileRow = std::min<std::uint64_t>(staging->tileRows, superTileRow + chunkTileRows);
				const auto superTileRowCount = std::min<std::uint64_t>((lastTileRow - superTileRow) * staging->rowTileSize, rows - sourceRowOffset);
				const auto readStart = std::chrono::steady_clock::now();
				auto indptr = readH5ADRange<std::uint64_t>(indptrSet, sourceRowOffset, superTileRowCount + 1, H5::PredType::NATIVE_UINT64);
				const auto valueCount = indptr.back() - indptr.front();
				auto indices = valueCount > 0 ? readH5ADRange<std::uint64_t>(indicesSet, indptr.front(), valueCount, H5::PredType::NATIVE_UINT64) : std::vector<std::uint64_t>();
				auto values = valueCount > 0 ? readH5ADRange<float>(dataSet, indptr.front(), valueCount, H5::PredType::NATIVE_FLOAT) : std::vector<float>();
				hdf5ReadMilliseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - readStart).count()), std::memory_order_relaxed);
				reportProgress(0.2f * static_cast<float>(lastTileRow) / static_cast<float>(staging->tileRows));

				const auto activeWorkerCount = std::max<std::size_t>(1, std::min<std::size_t>(effectiveBucketingWorkerCount, static_cast<std::size_t>(superTileRowCount)));
				std::vector<std::vector<std::vector<SparseTileEntry>>> entriesByWorkerAndColumn(activeWorkerCount, std::vector<std::vector<SparseTileEntry>>(static_cast<std::size_t>(columnSuperTileCount)));
				std::vector<std::future<void>> bucketJobs;
				bucketJobs.reserve(activeWorkerCount);
				const auto bucketingStart = std::chrono::steady_clock::now();
				for (std::size_t workerIndex = 0; workerIndex < activeWorkerCount; ++workerIndex)
				{
					const auto firstRow = superTileRowCount * workerIndex / activeWorkerCount;
					const auto lastRow = superTileRowCount * (workerIndex + 1) / activeWorkerCount;
					bucketJobs.emplace_back(std::async(std::launch::async, [&, workerIndex, firstRow, lastRow]() {
					ActiveCounter activeCounter(activeBucketers);
					const auto active = activeBucketers.load(std::memory_order_relaxed);
						updateMaximum(maximumActiveBucketers, active);
						auto& workerBuckets = entriesByWorkerAndColumn[workerIndex];
						for (auto row = firstRow; row < lastRow; ++row)
						{
							const auto begin = indptr[static_cast<std::size_t>(row)] - indptr.front();
							const auto end = indptr[static_cast<std::size_t>(row + 1)] - indptr.front();
							for (auto index = begin; index < end; ++index)
							{
								const auto column = indices[static_cast<std::size_t>(index)];
								if (column >= columns)
									throw std::runtime_error("CSR H5AD column index exceeds matrix dimensions");
								const auto tileColumn = column / staging->columnTileSize;
								const auto columnSuperTile = tileColumn / staging->superTileColumns;
								workerBuckets[static_cast<std::size_t>(columnSuperTile)].push_back({ index, row });
							}
						}
					}));
				}
				std::exception_ptr bucketingException;
				for (auto& job : bucketJobs)
					try { job.get(); } catch (...) { if (!bucketingException) bucketingException = std::current_exception(); }
				if (bucketingException)
					std::rethrow_exception(bucketingException);
				stagedNonZeroCount.fetch_add(valueCount, std::memory_order_relaxed);
				bucketingWorkerMilliseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bucketingStart).count()), std::memory_order_relaxed);
				reportProgress(0.4f * static_cast<float>(lastTileRow) / static_cast<float>(staging->tileRows));

				auto sharedIndices = std::make_shared<std::vector<std::uint64_t>>(std::move(indices));
				auto sharedValues = std::make_shared<std::vector<float>>(std::move(values));
				for (std::uint64_t columnSuperTileIndex = 0; columnSuperTileIndex < columnSuperTileCount; ++columnSuperTileIndex)
				{
					const auto superTileIndex = staging->superTiles.size();
					const auto fileName = QStringLiteral("supertile_%1.ptpack").arg(superTileIndex, 8, 10, QLatin1Char('0'));
					const auto filePath = QDir(stagingDirectory).filePath(fileName);
					const auto superTileColumn = columnSuperTileIndex * staging->superTileColumns;
					const auto lastTileColumn = std::min<std::uint64_t>(staging->tileColumns, superTileColumn + staging->superTileColumns);
					const auto tileColumnCount = lastTileColumn - superTileColumn;
					std::uint64_t outputBytes = 0;
					for (auto tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
						for (auto tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
							outputBytes += std::min<std::uint64_t>(staging->rowTileSize, rows - tileRow * staging->rowTileSize) * std::min<std::uint64_t>(staging->columnTileSize, columns - tileColumn * staging->columnTileSize) * elementSize;
					const auto encodedOutputBytes = outputBytes * (configuration.experimentalStripStorage ? 3 : 1);
					while (!pending.empty() && (pending.size() >= maxPendingEncoders || pendingBytes + encodedOutputBytes > maxPendingTileBytes))
						collectOldest();

					std::vector<std::vector<SparseTileEntry>> entriesForColumnByWorker;
					entriesForColumnByWorker.reserve(activeWorkerCount);
					for (auto& workerBuckets : entriesByWorkerAndColumn)
						entriesForColumnByWorker.push_back(std::move(workerBuckets[static_cast<std::size_t>(columnSuperTileIndex)]));

					staging->superTiles.resize(superTileIndex + 1);
					for (auto tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
						for (auto tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
						{
							const auto tileRowOffset = tileRow * staging->rowTileSize;
							const auto tileColumnOffset = tileColumn * staging->columnTileSize;
							const auto tileRowCount = std::min<std::uint64_t>(staging->rowTileSize, rows - tileRowOffset);
							const auto tileColumnValueCount = std::min<std::uint64_t>(staging->columnTileSize, columns - tileColumnOffset);
							const auto tileIndex = static_cast<std::size_t>(tileRow * staging->tileColumns + tileColumn);
							staging->tiles[tileIndex] = { tileRowOffset, tileColumnOffset, tileRowCount, tileColumnValueCount, superTileIndex, {} };
						}

					const auto codec = createStagingCodec(configuration.tileCodecName);
					const auto rowStripHeight = static_cast<std::uint64_t>(staging->rowStripHeight);
					const auto columnStripWidth = static_cast<std::uint64_t>(staging->columnStripWidth);
					pending.push_back({ std::async(std::launch::async, [&, superTileIndex, superTileRow, lastTileRow, superTileColumn, lastTileColumn, tileColumnCount, filePath, entriesForColumnByWorker = std::move(entriesForColumnByWorker), sharedIndices, sharedValues, codec, rowStripHeight, columnStripWidth]() mutable {
						ActiveCounter activeCounter(activeEncoders);
						const auto active = activeEncoders.load(std::memory_order_relaxed);
						updateMaximum(maximumActiveEncoders, active);
						const auto materializationStart = std::chrono::steady_clock::now();
						const auto tileBufferCount = static_cast<std::size_t>((lastTileRow - superTileRow) * tileColumnCount);
						std::vector<QByteArray> tileBuffers(tileBufferCount);
						for (auto tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
							for (auto tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
							{
								const auto tileRowCount = std::min<std::uint64_t>(staging->rowTileSize, rows - tileRow * staging->rowTileSize);
								const auto tileColumnValueCount = std::min<std::uint64_t>(staging->columnTileSize, columns - tileColumn * staging->columnTileSize);
								auto& bytes = tileBuffers[static_cast<std::size_t>((tileRow - superTileRow) * tileColumnCount + tileColumn - superTileColumn)];
								bytes = QByteArray(static_cast<qsizetype>(tileRowCount * tileColumnValueCount * elementSize), Qt::Uninitialized);
								std::memset(bytes.data(), 0, static_cast<std::size_t>(bytes.size()));
							}
						for (const auto& workerEntries : entriesForColumnByWorker)
							for (const auto& entry : workerEntries)
							{
								const auto tileRow = superTileRow + entry.row / staging->rowTileSize;
								const auto column = (*sharedIndices)[static_cast<std::size_t>(entry.index)];
								const auto tileColumn = column / staging->columnTileSize;
								const auto tileColumnValueCount = std::min<std::uint64_t>(staging->columnTileSize, columns - tileColumn * staging->columnTileSize);
								auto& bytes = tileBuffers[static_cast<std::size_t>((tileRow - superTileRow) * tileColumnCount + tileColumn - superTileColumn)];
								writeSparseTileValue(bytes, static_cast<std::size_t>((entry.row % staging->rowTileSize) * tileColumnValueCount + column % staging->columnTileSize), elementType, (*sharedValues)[static_cast<std::size_t>(entry.index)]);
							}
						materializationWorkerMilliseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - materializationStart).count()), std::memory_order_relaxed);
						std::vector<DenseTilePayload> payloads;
						payloads.reserve(tileBufferCount);
						for (auto tileRow = superTileRow; tileRow < lastTileRow; ++tileRow)
							for (auto tileColumn = superTileColumn; tileColumn < lastTileColumn; ++tileColumn)
							{
								const auto tileIndex = static_cast<std::size_t>(tileRow * staging->tileColumns + tileColumn);
								payloads.push_back({ tileIndex, std::move(tileBuffers[static_cast<std::size_t>((tileRow - superTileRow) * tileColumnCount + tileColumn - superTileColumn)]), staging->tiles[tileIndex].rows, staging->tiles[tileIndex].columns, elementSize });
							}
						auto result = encodeDenseSuperTile(superTileIndex, filePath, std::move(payloads), codec, staging->experimentalStripStorage, rowStripHeight, columnStripWidth);
						return result;
					}), encodedOutputBytes });
					pendingBytes += encodedOutputBytes;
				}
			}
			while (!pending.empty())
				collectOldest();
			std::uint64_t packedFileBytes = 0;
			std::uint64_t rowStripCount = 0;
			std::uint64_t columnStripCount = 0;
			for (const auto& superTile : staging->superTiles)
				packedFileBytes += static_cast<std::uint64_t>(QFileInfo(superTile.path).size());
			for (const auto& tile : staging->tiles)
			{
				rowStripCount += tile.rowStrips.size();
				columnStripCount += tile.columnStrips.size();
			}
			qInfo().noquote() << QStringLiteral("H5AD sparse CSR staging: %1 nonzero values across %2 tiles in %3 chunks (%4 tile rows each); HDF5 reads %5 ms; bucketing %6 worker-ms; tile materialization %7 worker-ms; compression/write %8 worker-ms; row strips: prepare %9 worker-ms, codec %10 worker-ms, write %11 worker-ms; column strips: prepare %12 worker-ms, codec %13 worker-ms, write %14 worker-ms; file finalize %15 worker-ms; maximum concurrent bucketers %16/%17; encoders %18/%19; elapsed %20 ms; %21 packed MiB; format %22 (%23 row and %24 column strips)")
				.arg(stagedNonZeroCount.load(std::memory_order_relaxed))
				.arg(staging->tiles.size())
				.arg((staging->tileRows + chunkTileRows - 1) / chunkTileRows)
				.arg(chunkTileRows)
				.arg(hdf5ReadMilliseconds.load(std::memory_order_relaxed))
				.arg(bucketingWorkerMilliseconds.load(std::memory_order_relaxed))
				.arg(materializationWorkerMilliseconds.load(std::memory_order_relaxed))
				.arg(encodeAndWriteWorkerMilliseconds.load(std::memory_order_relaxed))
				.arg(static_cast<double>(rowStripPreparationNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(rowStripEncodeNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(rowStripWriteNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripPreparationNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripEncodeNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(columnStripWriteNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(static_cast<double>(finalizeNanoseconds.load(std::memory_order_relaxed)) / 1.0e6, 0, 'f', 1)
				.arg(maximumActiveBucketers.load(std::memory_order_relaxed))
				.arg(effectiveBucketingWorkerCount)
				.arg(maximumActiveEncoders.load(std::memory_order_relaxed))
				.arg(maxPendingEncoders)
				.arg(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stagingStart).count())
				.arg(static_cast<double>(packedFileBytes) / (1024.0 * 1024.0), 0, 'f', 1)
				.arg(staging->experimentalStripStorage ? QStringLiteral("%1x%2 experimental strip").arg(staging->rowStripHeight).arg(staging->columnStripWidth) : QStringLiteral("whole tile"))
				.arg(rowStripCount)
				.arg(columnStripCount);

			return staging;
		}

		bool loadSparseDataAsTiled(H5::Group& group, LoaderInfo& loaderInfo, const int storageType, const H5AD::ProgressCallback& progress)
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
			const auto elementType = getTiledElementType(dataSet.getDataType(), storageType);
			const auto staging = stageSparseData(dataSet, indicesSet, indptrSet, rowCount, columnCount, loaderInfo._stagingDirectory, elementType, configuration, loaderInfo._bucketingThreadCount, loaderInfo._stagingThreadCount, loaderInfo._rowChunkTileFactor, progress);
			loaderInfo._pointsDataset->initializeStorageBackend(createStagedTiledBackend(rowCount, columnCount, elementType, configuration, staging));
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
			return true;
		}

		bool loadSparseDataAsSparse(H5::Group& group, LoaderInfo& loaderInfo, const H5AD::ProgressCallback& progress)
		{
			if (!ContainsSparseMatrix(group) || !group.attrExists("shape"))
				return false;

			bool columnCompressed = false;
			if (group.attrExists("encoding-type"))
			{
				std::string encodingType;
				auto encodingAttribute = group.openAttribute("encoding-type");
				encodingAttribute.read(encodingAttribute.getDataType(), encodingType);
				if (encodingType != "csr_matrix" && encodingType != "csc_matrix")
					return false;
				columnCompressed = encodingType == "csc_matrix";
			}

			const auto shapeAttribute = group.openAttribute("shape");
			const auto shapeSpace = shapeAttribute.getSpace();
			if (shapeSpace.getSimpleExtentNdims() != 1 || shapeSpace.getSimpleExtentNpoints() != 2)
				return false;

			hsize_t shape[2] = {};
			shapeAttribute.read(H5::PredType::NATIVE_HSIZE, shape);
			const auto rows        = static_cast<std::uint64_t>(shape[0]);
			const auto columns     = static_cast<std::uint64_t>(shape[1]);
			const auto dataSet     = group.openDataSet("data");
			const auto indicesSet  = group.openDataSet("indices");
			const auto indptrSet   = group.openDataSet("indptr");
			const auto dataSpace    = dataSet.getSpace();
			const auto indicesSpace = indicesSet.getSpace();
			const auto indptrSpace   = indptrSet.getSpace();
			if (dataSpace.getSimpleExtentNdims() != 1 || indicesSpace.getSimpleExtentNdims() != 1 || indptrSpace.getSimpleExtentNdims() != 1 || dataSpace.getSimpleExtentNpoints() != indicesSpace.getSimpleExtentNpoints())
				return false;

			const auto storedEntryCount = static_cast<std::uint64_t>(dataSpace.getSimpleExtentNpoints());
			const auto compressedSize   = columnCompressed ? columns : rows;
			if (compressedSize >= std::numeric_limits<std::uint64_t>::max() || compressedSize + 1 > std::numeric_limits<std::size_t>::max() || compressedSize + 1 > std::numeric_limits<hssize_t>::max() || storedEntryCount > std::numeric_limits<std::size_t>::max() ||
				indptrSpace.getSimpleExtentNpoints() != static_cast<hssize_t>(compressedSize + 1))
				return false;
			const auto elementType = getTiledElementType(dataSet.getDataType(), loaderInfo._storageType);
			const auto elementSize = getTiledElementSize(elementType);
			if (storedEntryCount > std::numeric_limits<std::size_t>::max() / elementSize || storedEntryCount > static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max()) / elementSize)
				throw std::overflow_error("Sparse H5AD values exceed the supported array size");

			if (progress)
				progress(0.0f);
			auto pointers = readH5ADRange<std::uint64_t>(indptrSet, 0, compressedSize + 1, H5::PredType::NATIVE_UINT64);
			auto indices  = storedEntryCount > 0 ? readH5ADRange<std::uint64_t>(indicesSet, 0, storedEntryCount, H5::PredType::NATIVE_UINT64) : std::vector<std::uint64_t>();
			auto values   = readSparseValues(dataSet, storedEntryCount, elementType);
			const auto layout = columnCompressed ? mv::pointdata::storage::SparseMatrixLayout::ColumnCompressed : mv::pointdata::storage::SparseMatrixLayout::RowCompressed;
			auto sparseBackend = std::make_unique<mv::pointdata::storage::SparsePointsStorageBackend>(rows, columns, layout, static_cast<std::uint32_t>(elementType), elementSize, std::move(pointers), std::move(indices), std::move(values));
			loaderInfo._pointsDataset->initializeStorageBackend(std::move(sparseBackend));
			loaderInfo._pointsDataset->setDimensionNames(loaderInfo._originalDimensionNames);
			loaderInfo._pointsDataset->setProperty("Sample Names", loaderInfo._sampleNames);
			if (progress)
				progress(1.0f);
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
			loaderInfo._pointsDataset->initializeStorageBackend(createStagedTiledBackend(totalRows, totalColumns, elementType, configuration, staging));
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
						if (loaderInfo._storageBackendType == pointdata::storage::StorageBackendType::Sparse)
							throw std::runtime_error("Sparse storage requires a sparse H5AD X matrix");
						if (loaderInfo._storageBackendType != pointdata::storage::StorageBackendType::Tiled || !loadDenseDataAsTiled(dataset, loaderInfo, storageType, progress))
							H5AD::LoadData(dataset, loaderInfo, storageType);
						break;

					}
				}
				else if (objectType1 == H5G_GROUP)
				{
					if (objectName1 == "X")
					{
						H5::Group group = h5fILE->openGroup(objectName1);
						if (loaderInfo._storageBackendType == pointdata::storage::StorageBackendType::Sparse)
						{
							if (!loadSparseDataAsSparse(group, loaderInfo, progress))
								throw std::runtime_error("Unsupported sparse H5AD X matrix format");
						}
						else if (loaderInfo._storageBackendType != pointdata::storage::StorageBackendType::Tiled || !loadSparseDataAsTiled(group, loaderInfo, storageType, progress))
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
							if (shouldLoadClusterSet(loaderInfo, h5datasetName + "/" + component->first))
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
		else
		{
			const auto datasetClass = dataset.getDataType().getClass();
			if (datasetClass != H5T_INTEGER && datasetClass != H5T_FLOAT && datasetClass != H5T_ENUM)
				return;

			const auto dataspace = dataset.getSpace();
			if (dataspace.getSimpleExtentNdims() != 2)
				return;
			hsize_t matrixShape[2] = {};
			dataspace.getSimpleExtentDims(matrixShape, nullptr);
			if (matrixShape[0] != loaderInfo._pointsDataset->getNumPoints() || matrixShape[1] == 0)
				return;

			std::vector<QString> dimensionNames(matrixShape[1]);
			for (std::size_t column = 0; column < matrixShape[1]; ++column)
				dimensionNames[column] = QString::number(column + 1);

			const auto matrixRows = static_cast<std::uint64_t>(matrixShape[0]);
			const auto matrixColumns = static_cast<std::uint64_t>(matrixShape[1]);
			const auto baseName = QString::fromLocal8Bit(h5datasetName.c_str());

			if (loaderInfo._metadataStorageBackendType == pointdata::storage::StorageBackendType::Tiled)
			{
				const auto elementType = getTiledElementType(dataset.getDataType(), loaderInfo._metadataStorageType);
				auto staging = stageDenseData(dataset, elementType, matrixRows, matrixColumns, loaderInfo._metadataStagingDirectory, loaderInfo._metadataTiledStorageConfiguration, {});
				auto tiledBackend = createStagedTiledBackend(matrixRows, matrixColumns, elementType, loaderInfo._metadataTiledStorageConfiguration, staging);
				mv::Dataset<Points> numericalMetaDataset = mv::data().createDerivedDataset<Points>(baseName, loaderInfo._pointsDataset, {}, false);
				numericalMetaDataset->initializeStorageBackend(std::move(tiledBackend));
				numericalMetaDataset->setDimensionNames(dimensionNames);
				numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
				mv::events().notifyDatasetAdded(numericalMetaDataset);
				mv::events().notifyDatasetDataChanged(numericalMetaDataset);
			}
			else
			{
				H5Utils::MultiDimensionalData<numericMetaDataType> matrixData;
				if (!H5Utils::read_multi_dimensional_data(dataset, matrixData))
					return;
				auto numericalMetaDataset = H5Utils::addNumericalMetaData(matrixData.data, dimensionNames, false, loaderInfo._pointsDataset, baseName, loaderInfo._metadataStorageType);
				numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
			}

			if (progress)
				progress(1.0f, baseName);
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
					if (shouldLoadClusterSet(loaderInfo, group.getObjName()))
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


									if (shouldLoadClusterSet(loaderInfo, group.getObjName()))
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
					if (shouldLoadClusterSet(loaderInfo, group.getObjName()))
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
											{
												if (shouldLoadClusterSet(loaderInfo, dataSet.getObjName()))
													H5Utils::addClusterMetaData(indices, dataSet.getObjName().c_str(), loaderInfo._pointsDataset);
											}
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

																	if (shouldLoadClusterSet(loaderInfo, dataSet.getObjName()))
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
												if (mdd.size[0] == loaderInfo._pointsDataset->getNumPoints() && mdd.size[1] > 0)
												{

													QString baseString = dataSet.getObjName().c_str();
													std::vector<QString> dimensionNames(mdd.size[1]);
													for (std::size_t l = 0; l < mdd.size[1]; ++l)
													{
														dimensionNames[l] = QString::number(l + 1);
													}
													mv::Dataset<Points> numericalMetaDataset = H5Utils::addNumericalMetaData(mdd.data, dimensionNames, false, loaderInfo._pointsDataset, baseString, loaderInfo._metadataStorageType);
													numericalMetaDataset->setProperty("Sample Names", loaderInfo._sampleNames);
													if (loaderInfo._metadataStorageBackendType == pointdata::storage::StorageBackendType::Tiled)
													{
														const auto rows = static_cast<std::uint64_t>(mdd.size[0]);
														const auto columns = static_cast<std::uint64_t>(mdd.size[1]);
														const auto elementType = getTiledElementType(dataSet.getDataType(), loaderInfo._metadataStorageType);
														auto staging = stageDenseData(dataSet, elementType, rows, columns, loaderInfo._metadataStagingDirectory, loaderInfo._metadataTiledStorageConfiguration, {});
														numericalMetaDataset->initializeStorageBackend(createStagedTiledBackend(rows, columns, elementType, loaderInfo._metadataTiledStorageConfiguration, staging));
													}
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
										if (shouldLoadClusterSet(loaderInfo, dataSet.getObjName()))
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
