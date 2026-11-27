#include "HDF5_AD_Loader.h"

#include "H5ADUtils.h"

#include "H5Utils.h"

#include <Application.h>
#include <workflow/WorkflowOptions.h>
#include <workflow/WorkflowPlan.h>
#include <util/Miscellaneous.h>
#include <util/HardwareSpec.h>
#include <util/RamComponentSpec.h>

#include <QGuiApplication>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>
#include <QtGlobal>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <omp.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <thread>

#include <PointData/PointData.h>

using namespace mv;

namespace
{

bool addDatasetPayloadSize(const H5::DataSet& dataset, std::uint64_t& totalBytes)
{
	const auto valueCount = dataset.getSpace().getSimpleExtentNpoints();
	const auto elementSize = dataset.getDataType().getSize();
	if (valueCount < 0 || (elementSize != 0 && static_cast<std::uint64_t>(valueCount) > std::numeric_limits<std::uint64_t>::max() / elementSize))
		return false;

	const auto datasetBytes = static_cast<std::uint64_t>(valueCount) * elementSize;
	if (totalBytes > std::numeric_limits<std::uint64_t>::max() - datasetBytes)
		return false;

	totalBytes += datasetBytes;
	return true;
}

bool addGroupPayloadSize(H5::Group& group, std::uint64_t& totalBytes, const std::size_t depth = 0)
{
	if (depth > 64)
		return false;

	for (hsize_t index = 0; index < group.getNumObjs(); ++index)
	{
		const auto name = group.getObjnameByIdx(index);
		const auto objectType = group.getObjTypeByIdx(index);
		if (objectType == H5G_DATASET)
		{
			auto dataset = group.openDataSet(name);
			if (!addDatasetPayloadSize(dataset, totalBytes))
				return false;
		}
		else if (objectType == H5G_GROUP)
		{
			auto subgroup = group.openGroup(name);
			if (!addGroupPayloadSize(subgroup, totalBytes, depth + 1))
				return false;
		}
	}

	return true;
}

QJsonObject createImportBenchmarkJson(const H5AD::ImportBenchmarkData& benchmark, const H5ADImportOptions& options, const QString& sourceFilePath, const std::uint64_t rows, const std::uint64_t columns, const bool sparseInput, const bool succeeded)
{
	const auto& dense = benchmark.denseStaging;
	const auto totalMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - benchmark.startedAt).count());
	QString targetElementTypeName;
	const auto targetElementTypeNames = PointData::getElementTypeNames();
	if (dense.targetElementType >= 0 && static_cast<std::size_t>(dense.targetElementType) < targetElementTypeNames.size())
		targetElementTypeName = QString::fromLatin1(targetElementTypeNames[static_cast<std::size_t>(dense.targetElementType)]);
	QJsonObject source {
		{ QStringLiteral("path"), sourceFilePath },
		{ QStringLiteral("file_size_bytes"), static_cast<qint64>(QFileInfo(sourceFilePath).size()) },
		{ QStringLiteral("rows"), static_cast<qint64>(rows) },
		{ QStringLiteral("columns"), static_cast<qint64>(columns) },
		{ QStringLiteral("sparse_encoding_detected"), sparseInput }
	};
	QJsonObject configuration {
		{ QStringLiteral("storage_backend"), pointdata::storage::getStorageBackendTypeName(options.storageBackendType) },
		{ QStringLiteral("requested_storage_type_index"), options.storageType },
		{ QStringLiteral("bucketing_thread_count"), static_cast<qint64>(options.bucketingThreadCount) },
		{ QStringLiteral("staging_thread_count_setting"), static_cast<qint64>(options.stagingThreadCount) },
		{ QStringLiteral("row_chunk_tile_factor"), static_cast<qint64>(options.rowChunkTileFactor) },
		{ QStringLiteral("optional_metadata"), QJsonArray::fromStringList(options.optionalObjects) },
		{ QStringLiteral("optional_matrices"), QJsonArray::fromStringList(options.optionalMatrixObjects) }
	};
	const auto& tiled = options.tiledStorageConfiguration;
	configuration.insert(QStringLiteral("tile_rows"), static_cast<qint64>(tiled.rowTileSize));
	configuration.insert(QStringLiteral("tile_columns"), static_cast<qint64>(tiled.columnTileSize));
	configuration.insert(QStringLiteral("cache_capacity_tiles"), static_cast<qint64>(tiled.cacheCapacity));
	configuration.insert(QStringLiteral("cache_capacity_bytes"), static_cast<qint64>(tiled.cacheCapacityBytes));
	configuration.insert(QStringLiteral("packed_tiles"), tiled.packedTiles);
	configuration.insert(QStringLiteral("supertile_rows"), static_cast<qint64>(tiled.superTileRows));
	configuration.insert(QStringLiteral("supertile_columns"), static_cast<qint64>(tiled.superTileColumns));
	configuration.insert(QStringLiteral("storage_layout"), QStringLiteral("row_column_strips"));
	configuration.insert(QStringLiteral("row_strip_height"), static_cast<qint64>(tiled.rowStripHeight));
	configuration.insert(QStringLiteral("column_strip_width"), static_cast<qint64>(tiled.columnStripWidth));
	configuration.insert(QStringLiteral("configured_codec"), tiled.tileCodecName);
	configuration.insert(QStringLiteral("zstd_compression_level"), options.zstdCompressionLevel);

	QJsonObject staging {
		{ QStringLiteral("instrumentation_available"), dense.tileCount > 0 },
		{ QStringLiteral("elapsed_ms"), static_cast<qint64>(dense.elapsedMilliseconds) },
		{ QStringLiteral("source_layout"), dense.sourceLayout },
		{ QStringLiteral("source_element_type"), dense.sourceElementType },
		{ QStringLiteral("source_element_size_bytes"), static_cast<qint64>(dense.sourceElementSizeBytes) },
		{ QStringLiteral("source_chunk_rows"), static_cast<qint64>(dense.sourceChunkRows) },
		{ QStringLiteral("source_chunk_columns"), static_cast<qint64>(dense.sourceChunkColumns) },
		{ QStringLiteral("target_element_type_index"), dense.targetElementType },
		{ QStringLiteral("target_element_type"), targetElementTypeName },
		{ QStringLiteral("target_element_size_bytes"), static_cast<qint64>(dense.elementSizeBytes) },
		{ QStringLiteral("codec"), dense.codecName },
		{ QStringLiteral("tiles_read"), static_cast<qint64>(dense.tileCount) },
		{ QStringLiteral("hdf5_read_calls"), static_cast<qint64>(dense.tileCount) },
		{ QStringLiteral("supertiles_written"), static_cast<qint64>(dense.superTileCount) },
		{ QStringLiteral("staged_bytes"), static_cast<qint64>(dense.stagedBytes) },
		{ QStringLiteral("codec_input_bytes"), static_cast<qint64>(dense.codecInputBytes) },
		{ QStringLiteral("packed_file_bytes"), static_cast<qint64>(dense.packedFileBytes) },
		{ QStringLiteral("hdf5_read_ms"), static_cast<qint64>(dense.hdf5ReadMilliseconds) },
		{ QStringLiteral("encode_write_worker_ms"), static_cast<qint64>(dense.encodeAndWriteWorkerMilliseconds) },
		{ QStringLiteral("row_strip_count"), static_cast<qint64>(dense.rowStripCount) },
		{ QStringLiteral("column_strip_count"), static_cast<qint64>(dense.columnStripCount) },
		{ QStringLiteral("row_strip_prepare_ms"), static_cast<double>(dense.rowStripPreparationNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_strip_encode_ms"), static_cast<double>(dense.rowStripEncodeNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_strip_write_ms"), static_cast<double>(dense.rowStripWriteNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_prepare_ms"), static_cast<double>(dense.columnStripPreparationNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_encode_ms"), static_cast<double>(dense.columnStripEncodeNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_write_ms"), static_cast<double>(dense.columnStripWriteNanoseconds) / 1.0e6 },
		{ QStringLiteral("file_finalize_ms"), static_cast<double>(dense.finalizeNanoseconds) / 1.0e6 },
		{ QStringLiteral("maximum_active_encoders_observed"), static_cast<qint64>(dense.maximumActiveEncoders) },
		{ QStringLiteral("maximum_queued_supertiles_observed"), static_cast<qint64>(dense.maximumQueuedSuperTiles) },
		{ QStringLiteral("maximum_inflight_bytes_observed"), static_cast<qint64>(dense.maximumInFlightBytes) },
		{ QStringLiteral("dense_hdf5_read_concurrency"), dense.tileCount > 0 ? 1 : 0 },
		{ QStringLiteral("dense_encoder_queue_limit"), 8 },
		{ QStringLiteral("dense_encoder_byte_limit"), 256 * 1024 * 1024 }
	};
	const auto& sparse = benchmark.sparseStaging;
	QJsonObject sparseStaging {
		{ QStringLiteral("instrumentation_available"), sparse.elapsedMilliseconds > 0 },
		{ QStringLiteral("completed"), sparse.completed },
		{ QStringLiteral("source_element_type"), sparse.sourceElementType },
		{ QStringLiteral("codec"), sparse.codecName },
		{ QStringLiteral("stored_entry_count"), static_cast<qint64>(sparse.storedEntryCount) },
		{ QStringLiteral("stored_entry_density"), rows != 0 && columns != 0 ? static_cast<double>(sparse.storedEntryCount) / (static_cast<double>(rows) * static_cast<double>(columns)) : 0.0 },
		{ QStringLiteral("input_value_bytes"), static_cast<qint64>(sparse.inputValueBytes) },
		{ QStringLiteral("input_index_bytes"), static_cast<qint64>(sparse.inputIndexBytes) },
		{ QStringLiteral("input_pointer_bytes"), static_cast<qint64>(sparse.inputPointerBytes) },
		{ QStringLiteral("dense_equivalent_bytes"), static_cast<qint64>(sparse.denseEquivalentBytes) },
		{ QStringLiteral("staged_bytes"), static_cast<qint64>(sparse.stagedBytes) },
		{ QStringLiteral("codec_input_bytes"), static_cast<qint64>(sparse.codecInputBytes) },
		{ QStringLiteral("packed_file_bytes"), static_cast<qint64>(sparse.packedFileBytes) },
		{ QStringLiteral("tile_count"), static_cast<qint64>(sparse.tileCount) },
		{ QStringLiteral("supertile_count"), static_cast<qint64>(sparse.superTileCount) },
		{ QStringLiteral("chunk_count"), static_cast<qint64>(sparse.chunkCount) },
		{ QStringLiteral("rows_per_chunk"), static_cast<qint64>(sparse.rowsPerChunk) },
		{ QStringLiteral("hdf5_read_calls"), static_cast<qint64>(sparse.hdf5ReadCalls) },
		{ QStringLiteral("indptr_read_calls"), static_cast<qint64>(sparse.indptrReadCalls) },
		{ QStringLiteral("indices_read_calls"), static_cast<qint64>(sparse.indicesReadCalls) },
		{ QStringLiteral("values_read_calls"), static_cast<qint64>(sparse.valuesReadCalls) },
		{ QStringLiteral("hdf5_read_ms"), static_cast<qint64>(sparse.hdf5ReadMilliseconds) },
		{ QStringLiteral("indptr_read_ms"), static_cast<qint64>(sparse.indptrReadMilliseconds) },
		{ QStringLiteral("indices_read_ms"), static_cast<qint64>(sparse.indicesReadMilliseconds) },
		{ QStringLiteral("values_read_ms"), static_cast<qint64>(sparse.valuesReadMilliseconds) },
		{ QStringLiteral("bucketing_ms"), static_cast<qint64>(sparse.bucketingMilliseconds) },
		{ QStringLiteral("materialization_worker_ms"), static_cast<qint64>(sparse.materializationWorkerMilliseconds) },
		{ QStringLiteral("encode_write_worker_ms"), static_cast<qint64>(sparse.encodeAndWriteWorkerMilliseconds) },
		{ QStringLiteral("row_strip_prepare_ms"), static_cast<double>(sparse.rowStripPreparationNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_strip_encode_ms"), static_cast<double>(sparse.rowStripEncodeNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_strip_write_ms"), static_cast<double>(sparse.rowStripWriteNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_strip_worker_cpu_ms"), static_cast<double>(sparse.rowStripWorkerCpuNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_writer_lock_wait_ms"), static_cast<double>(sparse.rowWriterLockWaitNanoseconds) / 1.0e6 },
		{ QStringLiteral("row_writer_lock_hold_ms"), static_cast<double>(sparse.rowWriterLockHoldNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_prepare_ms"), static_cast<double>(sparse.columnStripPreparationNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_encode_ms"), static_cast<double>(sparse.columnStripEncodeNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_write_ms"), static_cast<double>(sparse.columnStripWriteNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_strip_worker_cpu_ms"), static_cast<double>(sparse.columnStripWorkerCpuNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_writer_lock_wait_ms"), static_cast<double>(sparse.columnWriterLockWaitNanoseconds) / 1.0e6 },
		{ QStringLiteral("column_writer_lock_hold_ms"), static_cast<double>(sparse.columnWriterLockHoldNanoseconds) / 1.0e6 },
		{ QStringLiteral("pending_supertile_future_wait_ms"), static_cast<double>(sparse.pendingFutureWaitNanoseconds) / 1.0e6 },
		{ QStringLiteral("file_finalize_ms"), static_cast<double>(sparse.finalizeNanoseconds) / 1.0e6 },
		{ QStringLiteral("elapsed_ms"), static_cast<qint64>(sparse.elapsedMilliseconds) },
		{ QStringLiteral("backend_construction_ms"), static_cast<double>(sparse.backendConstructionNanoseconds) / 1.0e6 },
		{ QStringLiteral("dataset_backend_initialization_ms"), static_cast<double>(sparse.datasetBackendInitializationNanoseconds) / 1.0e6 },
		{ QStringLiteral("maximum_pending_bytes"), static_cast<qint64>(sparse.maximumPendingBytes) },
		{ QStringLiteral("pending_byte_limit"), static_cast<qint64>(sparse.pendingByteLimit) },
		{ QStringLiteral("maximum_active_bucketers"), static_cast<qint64>(sparse.maximumActiveBucketers) },
		{ QStringLiteral("maximum_active_encoders"), static_cast<qint64>(sparse.maximumActiveEncoders) },
		{ QStringLiteral("staging_queue_limit"), static_cast<qint64>(sparse.maximumPendingEncoders) },
		{ QStringLiteral("maximum_pending_supertile_jobs"), static_cast<qint64>(sparse.maximumPendingSuperTiles) },
		{ QStringLiteral("configured_bucketing_workers"), static_cast<qint64>(sparse.bucketingWorkers) },
		{ QStringLiteral("effective_staging_workers"), static_cast<qint64>(sparse.stagingWorkers) },
		{ QStringLiteral("strip_worker_pool_size"), static_cast<qint64>(sparse.stripWorkerPoolSize) },
		{ QStringLiteral("maximum_active_strip_workers"), static_cast<qint64>(sparse.maximumActiveStripWorkers) }
	};
	QJsonObject stages {
		{ QStringLiteral("point_data_staging_ms"), static_cast<qint64>(benchmark.pointDataStageMilliseconds) },
		{ QStringLiteral("point_data_stage_unattributed_ms"), sparse.elapsedMilliseconds > 0 ? std::max<double>(0.0, static_cast<double>(benchmark.pointDataStageMilliseconds) - sparse.elapsedMilliseconds - static_cast<double>(sparse.backendConstructionNanoseconds) / 1.0e6 - static_cast<double>(sparse.datasetBackendInitializationNanoseconds) / 1.0e6) : 0.0 },
		{ QStringLiteral("metadata_ms"), static_cast<qint64>(benchmark.metadataStageMilliseconds) },
		{ QStringLiteral("publish_ms"), static_cast<qint64>(benchmark.publishStageMilliseconds) },
		{ QStringLiteral("total_workflow_ms"), static_cast<qint64>(totalMilliseconds) }
	};
	QJsonObject metadataJobs;
	for (auto it = benchmark.metadataJobMilliseconds.cbegin(); it != benchmark.metadataJobMilliseconds.cend(); ++it)
		metadataJobs.insert(it.key(), static_cast<qint64>(it.value()));
	unsigned int hdf5Major = 0;
	unsigned int hdf5Minor = 0;
	unsigned int hdf5Release = 0;
	H5get_libversion(&hdf5Major, &hdf5Minor, &hdf5Release);
	const auto& hardware = mv::util::HardwareSpec::getSystemHardwareSpec();
	const auto ramSpec = hardware.getHardwareComponentSpec<mv::util::RamComponentSpec>(QStringLiteral("RAM"));
	const auto installedMemoryBytes = ramSpec && ramSpec->isInitialized() ? ramSpec->getNumberOfBytes() : 0;
	QJsonObject environment {
		{ QStringLiteral("operating_system"), QSysInfo::prettyProductName() },
		{ QStringLiteral("logical_processors"), QThread::idealThreadCount() },
		{ QStringLiteral("hardware_concurrency"), static_cast<qint64>(std::thread::hardware_concurrency()) },
		{ QStringLiteral("installed_memory_bytes"), static_cast<qint64>(installedMemoryBytes) },
		{ QStringLiteral("qt_version"), QString::fromLatin1(qVersion()) },
		{ QStringLiteral("hdf5_version"), QStringLiteral("%1.%2.%3").arg(hdf5Major).arg(hdf5Minor).arg(hdf5Release) }
	};
	return {
		{ QStringLiteral("schema_version"), 1 },
		{ QStringLiteral("benchmark_type"), QStringLiteral("h5ad_import") },
		{ QStringLiteral("status"), succeeded ? QStringLiteral("success") : QStringLiteral("failed_or_canceled") },
		{ QStringLiteral("started_utc"), benchmark.startedUtc.toString(Qt::ISODateWithMs) },
		{ QStringLiteral("finished_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
		{ QStringLiteral("source"), source },
		{ QStringLiteral("configuration"), configuration },
		{ QStringLiteral("stages"), stages },
		{ QStringLiteral("dense_staging"), staging },
		{ QStringLiteral("sparse_staging"), sparseStaging },
		{ QStringLiteral("metadata_jobs_ms"), metadataJobs },
		{ QStringLiteral("environment"), environment },
		{ QStringLiteral("report_path"), benchmark.outputFilePath },
		{ QStringLiteral("timing_notes"), QStringLiteral("Import durations include current OS and HDF5 cache state. Worker timings may overlap and exceed wall time. Strip worker CPU timings sum CPU time across worker tasks and can be compared with elapsed wall time to estimate aggregate CPU use. Sparse staging reports HDF5 reads, bucketing, tile materialization, encoding and writing, queue/concurrency peaks, output sizes, and backend setup. Dense staging reports tile and strip timings. Byte counts are payload estimates and do not include allocator/container overhead. Peak process memory is not sampled by this report.") }
	};
}

void writeImportBenchmark(const H5AD::ImportBenchmarkData& benchmark, const H5ADImportOptions& options, const QString& sourceFilePath, const std::uint64_t rows, const std::uint64_t columns, const bool sparseInput, const bool succeeded)
{
	QSaveFile output(benchmark.outputFilePath);
	if (!output.open(QIODevice::WriteOnly))
	{
		qWarning().noquote() << QStringLiteral("H5AD import benchmark: unable to open %1: %2").arg(benchmark.outputFilePath, output.errorString());
		return;
	}
	const auto json = QJsonDocument(createImportBenchmarkJson(benchmark, options, sourceFilePath, rows, columns, sparseInput, succeeded)).toJson(QJsonDocument::Indented);
	if (output.write(json) != json.size() || !output.commit())
		qWarning().noquote() << QStringLiteral("H5AD import benchmark: unable to save %1: %2").arg(benchmark.outputFilePath, output.errorString());
}

}

HDF5_AD_Loader::HDF5_AD_Loader(mv::CoreInterface* core, QObject* workflowReceiver, const QString& stagingDirectory)
{
	_core = core;
	_workflowReceiver = workflowReceiver;
	_stagingDirectory = stagingDirectory;
}

bool HDF5_AD_Loader::open(const QString& fileName)
{
	try
	{
		_file.reset(new H5::H5File(fileName.toLatin1().constData(), H5F_ACC_RDONLY));
		auto nrOfObjects = _file->getNumObjs();
		bool dataFound = false;

		for (auto fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			if (objectName1 == "X")
			{
				dataFound = true;
				
			}
			else if(objectName1 == "var")
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);
					_var_indexName = H5AD::LoadIndexStrings(dataset, _dimensionNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					_var_indexName = H5AD::LoadIndexStrings(group, _dimensionNames);
				}
			}
			else if (objectName1 == "obs")
			{
				H5G_obj_t objectType1 = _file->getObjTypeByIdx(fo);

				if (objectType1 == H5G_DATASET)
				{
					H5::DataSet dataset = _file->openDataSet(objectName1);
					_obs_indexName = H5AD::LoadIndexStrings(dataset, _sampleNames);
				}
				else if (objectType1 == H5G_GROUP)
				{
					H5::Group group = _file->openGroup(objectName1);
					_obs_indexName = H5AD::LoadIndexStrings(group, _sampleNames);
				}
			}
		}
		
		if(dataFound && (!_dimensionNames.empty()))
		{
			_fileName = fileName;
			return true;
		}
	}
	catch (...)
	{
		_fileName.clear();
		_file->close();
		_file.release();
	}
	return false;
}

 const std::vector<QString>& HDF5_AD_Loader::getDimensionNames() const
{
	return _dimensionNames;
}

QStringList HDF5_AD_Loader::getOptionalObjects() const
{
	QStringList names;
	if (!_file)
		return names;

	const std::set<std::string> ignoredObjects = { "X", "uns", "var", "varm", "varp" };
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
	{
		const auto name = _file->getObjnameByIdx(index);
		if (ignoredObjects.count(name) == 0)
			names.append(QString::fromStdString(name));
	}
	return names;
}

QMap<QString, H5ADClusterSetInfo> HDF5_AD_Loader::getObsClusterSets() const
{
	QMap<QString, H5ADClusterSetInfo> clusterSets;
	if (!_file)
		return clusterSets;
	const auto observationCount = static_cast<std::uint64_t>(_sampleNames.size());
	H5G_obj_t obsObjectType = H5G_UNKNOWN;
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		if (_file->getObjnameByIdx(index) == "obs")
		{
			obsObjectType = _file->getObjTypeByIdx(index);
			break;
		}

	if (obsObjectType == H5G_DATASET)
	{
		try
		{
			const auto dataset = _file->openDataSet("obs");
			const auto dataSpace = dataset.getSpace();
			const auto valueCount = dataSpace.getSimpleExtentNpoints();
			if (dataset.getDataType().getClass() == H5T_COMPOUND && valueCount >= 0 && (observationCount == 0 || static_cast<std::uint64_t>(valueCount) == observationCount))
			{
				const auto compoundType = dataset.getCompType();
				for (int member = 0; member < compoundType.getNmembers(); ++member)
					if (compoundType.getMemberClass(member) == H5T_STRING)
					{
						const auto name = QString::fromLocal8Bit(compoundType.getMemberName(member).c_str());
						clusterSets.insert(name, H5ADClusterSetInfo { false, 0, QObject::tr("String field · %1 observations; category count requires scanning values").arg(valueCount) });
					}
			}
		}
		catch (const H5::Exception&)
		{
		}
		return clusterSets;
	}

	if (obsObjectType != H5G_GROUP)
		return clusterSets;

	try
	{
		auto obsGroup = _file->openGroup("obs");

		const auto visitGroup = [&clusterSets, observationCount](const auto& self, H5::Group& group, const QString& parentPath, const std::size_t depth) -> void {
			if (depth > 32)
				return;

			const auto groupPath = QString::fromLocal8Bit(group.getObjName().c_str());
			const auto groupName = groupPath.section('/', -1);
			if (group.nameExists("codes") && group.nameExists("categories"))
			{
				try
				{
					const auto codes = group.openDataSet("codes");
					const auto categories = group.openDataSet("categories");
					const auto codeCount = codes.getSpace().getSimpleExtentNpoints();
					const auto categoryCount = categories.getSpace().getSimpleExtentNpoints();
					if (codeCount >= 0 && categoryCount >= 0 && (observationCount == 0 || static_cast<std::uint64_t>(codeCount) == observationCount))
					{
						const auto key = parentPath.isEmpty() ? groupName : parentPath;
						clusterSets.insert(key, H5ADClusterSetInfo { true, static_cast<std::uint64_t>(categoryCount), QObject::tr("Encoded category labels · %1 observations").arg(codeCount) });
					}
					return;
				}
				catch (const H5::Exception&)
				{
				}
			}

			for (hsize_t index = 0; index < group.getNumObjs(); ++index)
			{
				const auto name = group.getObjnameByIdx(index);
				if (name == "__categories" || name == "index" || name == "_index" || name.find("_color") != std::string::npos)
					continue;

				const auto key = parentPath.isEmpty() ? QString::fromLocal8Bit(name.c_str()) : parentPath + "/" + QString::fromLocal8Bit(name.c_str());
				const auto objectType = group.getObjTypeByIdx(index);
				if (objectType == H5G_GROUP)
				{
					auto subgroup = group.openGroup(name);
					self(self, subgroup, key, depth + 1);
					continue;
				}
				if (objectType != H5G_DATASET)
					continue;

				try
				{
					const auto dataset = group.openDataSet(name);
					const auto dataSpace = dataset.getSpace();
					const auto dimensions = dataSpace.getSimpleExtentNdims();
					const auto valueCount = dataSpace.getSimpleExtentNpoints();
					if (dimensions != 1 || valueCount < 0 || (observationCount != 0 && static_cast<std::uint64_t>(valueCount) != observationCount))
						continue;

					const auto typeClass = dataset.getDataType().getClass();
					if (typeClass == H5T_STRING || typeClass == H5T_ENUM || (dataset.attrExists("categories") && (typeClass == H5T_INTEGER || typeClass == H5T_FLOAT)))
					{
						H5ADClusterSetInfo info;
						info.description = QObject::tr("Categorical field · %1 observations").arg(valueCount);
						if (dataset.attrExists("categories") && group.nameExists("__categories"))
						{
							try
							{
								auto categoriesGroup = group.openGroup("__categories");
								if (categoriesGroup.nameExists(name))
								{
									const auto categoriesDataSet = categoriesGroup.openDataSet(name);
									const auto categoryCount = categoriesDataSet.getSpace().getSimpleExtentNpoints();
									if (categoryCount >= 0)
									{
										info.clusterCountAvailable = true;
										info.clusterCount = static_cast<std::uint64_t>(categoryCount);
									}
								}
							}
							catch (const H5::Exception&)
							{
							}
						}
						clusterSets.insert(key, info);
					}
				}
				catch (const H5::Exception&)
				{
				}
			}
		};

		visitGroup(visitGroup, obsGroup, {}, 0);
	}
	catch (const H5::Exception&)
	{
	}

	return clusterSets;
}

QStringList HDF5_AD_Loader::getObsmMatrices() const
{
	QStringList names;
	if (!_file)
		return names;

	try
	{
		auto group = _file->openGroup("obsm");
		for (hsize_t index = 0; index < group.getNumObjs(); ++index)
		{
			if (group.getObjTypeByIdx(index) != H5G_DATASET)
				continue;

			const auto name = group.getObjnameByIdx(index);
			const auto dataset = group.openDataSet(name);
			if (dataset.getDataType().getClass() != H5T_FLOAT && dataset.getDataType().getClass() != H5T_INTEGER)
				continue;

			const auto space = dataset.getSpace();
			if (space.getSimpleExtentNdims() == 2)
				names.append(QString::fromLocal8Bit(name.c_str()));
		}
	}
	catch (const H5::Exception&)
	{
	}
	return names;
}

QString HDF5_AD_Loader::getObsmMatrixDescription(const QString& name) const
{
	if (!_file)
		return {};

	try
	{
		const auto dataset = _file->openDataSet(std::string("obsm/") + name.toStdString());
		const auto space = dataset.getSpace();
		if (space.getSimpleExtentNdims() != 2)
			return {};

		hsize_t shape[2] = {};
		space.getSimpleExtentDims(shape);
		if (shape[1] != 0 && static_cast<std::uint64_t>(shape[0]) > std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(shape[1]))
			return QObject::tr("%1 × %2, dense, size unavailable").arg(shape[0]).arg(shape[1]);
		const auto elementCount = static_cast<std::uint64_t>(shape[0]) * static_cast<std::uint64_t>(shape[1]);
		if (dataset.getDataType().getSize() != 0 && elementCount > std::numeric_limits<std::uint64_t>::max() / dataset.getDataType().getSize())
			return QObject::tr("%1 × %2, dense, size unavailable").arg(shape[0]).arg(shape[1]);
		const auto bytes = elementCount * dataset.getDataType().getSize();
		return QObject::tr("%1 × %2, dense, %3").arg(shape[0]).arg(shape[1]).arg(mv::util::getNoBytesHumanReadable(bytes));
	}
	catch (const H5::Exception&)
	{
		return QObject::tr("Matrix details unavailable");
	}
}

std::pair<std::uint64_t, std::uint64_t> HDF5_AD_Loader::getObsmMatrixShape(const QString& name) const
{
	if (!_file)
		return { 0, 0 };

	try
	{
		const auto dataset = _file->openDataSet(std::string("obsm/") + name.toStdString());
		const auto space = dataset.getSpace();
		if (space.getSimpleExtentNdims() != 2)
			return { 0, 0 };

		hsize_t shape[2] = {};
		space.getSimpleExtentDims(shape);
		return { shape[0], shape[1] };
	}
	catch (const H5::Exception&)
	{
		return { 0, 0 };
	}
}

QMap<QString, std::uint64_t> HDF5_AD_Loader::getOptionalObjectSizeEstimates() const
{
	QMap<QString, std::uint64_t> estimates;
	if (!_file)
		return estimates;

	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
	{
		const auto name = _file->getObjnameByIdx(index);
		if (name == "X" || name == "uns" || name == "var" || name == "varm" || name == "varp")
			continue;

		const auto objectType = _file->getObjTypeByIdx(index);
		std::uint64_t totalBytes = 0;
		bool estimated = false;
		try
		{
			if (objectType == H5G_DATASET)
			{
				auto dataset = _file->openDataSet(name);
				estimated = addDatasetPayloadSize(dataset, totalBytes);
			}
			else if (objectType == H5G_GROUP)
			{
				auto group = _file->openGroup(name);
				estimated = addGroupPayloadSize(group, totalBytes);
			}
		}
		catch (const H5::Exception&)
		{
			estimated = false;
		}

		if (estimated)
			estimates.insert(QString::fromLocal8Bit(name.c_str()), totalBytes);
	}

	return estimates;
}

std::pair<std::uint64_t, std::uint64_t> HDF5_AD_Loader::getMatrixShape() const
{
	if (!_file)
		return { 0, 0 };

	H5G_obj_t objectType = H5G_UNKNOWN;
	try
	{
		for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		{
			if (_file->getObjnameByIdx(index) == "X")
			{
				objectType = _file->getObjTypeByIdx(index);
				break;
			}
		}
	}
	catch (const H5::Exception&)
	{
		return { 0, 0 };
	}

	try
	{
		if (objectType == H5G_DATASET)
		{
			const auto dataSet = _file->openDataSet("X");
			const auto space = dataSet.getSpace();
			if (space.getSimpleExtentNdims() != 2)
				return { 0, 0 };

			hsize_t dimensions[2] = {};
			space.getSimpleExtentDims(dimensions);
			return { dimensions[0], dimensions[1] };
		}

		if (objectType == H5G_GROUP)
		{
			const auto group = _file->openGroup("X");
			if (!group.attrExists("shape"))
				return { 0, 0 };

			const auto attribute = group.openAttribute("shape");
			const auto space = attribute.getSpace();
			if (space.getSimpleExtentNdims() != 1 || space.getSimpleExtentNpoints() != 2)
				return { 0, 0 };

			hsize_t dimensions[2] = {};
			attribute.read(H5::PredType::NATIVE_HSIZE, dimensions);
			return { dimensions[0], dimensions[1] };
		}
	}
	catch (const H5::Exception&)
	{
		return { 0, 0 };
	}

	return { 0, 0 };
}

std::vector<H5ADDimensionStatistics> HDF5_AD_Loader::scanDimensionStatistics(const std::function<void(double)>& progress) const
{
	return analyzeDimensionStatistics(true, progress);
}

std::vector<H5ADDimensionStatistics> HDF5_AD_Loader::estimateDimensionStatistics(const std::function<void(double)>& progress) const
{
	return analyzeDimensionStatistics(false, progress);
}

std::vector<H5ADDimensionStatistics> HDF5_AD_Loader::analyzeDimensionStatistics(const bool fullScan, const std::function<void(double)>& progress) const
{
	const auto [rowCount, columnCount] = getMatrixShape();
	if (!_file || rowCount == 0 || columnCount == 0 || columnCount > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
		return {};
	constexpr std::uint64_t maximumSampledRows = 512;
	constexpr std::uint64_t maximumSampledValues = 1024 * 1024;
	const auto sampleRowBudget = std::max<std::uint64_t>(1, std::min(maximumSampledRows, maximumSampledValues / columnCount));
	const auto sampledRowStride = fullScan ? std::uint64_t { 1 } : std::max<std::uint64_t>(1, rowCount / sampleRowBudget + (rowCount % sampleRowBudget != 0));
	const auto sampledRowCount = fullScan ? rowCount : rowCount / sampledRowStride + (rowCount % sampledRowStride != 0);

	struct WorkerAccumulator
	{
		std::uint64_t finiteCount = 0;
		std::uint64_t zeroCount = 0;
		std::uint64_t nanCount = 0;
		double reference = 0.0;
		double sumDelta = 0.0;
		double sumSquaredDelta = 0.0;
		double minimum = std::numeric_limits<double>::infinity();
		double maximum = -std::numeric_limits<double>::infinity();

		void add(const double value)
		{
			if (std::isnan(value))
			{
				++nanCount;
				return;
			}
			if (!std::isfinite(value))
				return;
			if (value == 0.0)
				++zeroCount;
			if (finiteCount == 0)
				reference = value;
			const auto delta = value - reference;
			sumDelta += delta;
			sumSquaredDelta += delta * delta;
			++finiteCount;
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}
	};

	struct Accumulator
	{
		std::uint64_t finiteCount = 0;
		std::uint64_t zeroCount = 0;
		std::uint64_t nanCount = 0;
		double mean = 0.0;
		double sumSquaredDelta = 0.0;
		double minimum = std::numeric_limits<double>::infinity();
		double maximum = -std::numeric_limits<double>::infinity();

		void add(const double value)
		{
			if (std::isnan(value))
			{
				++nanCount;
				return;
			}
			if (!std::isfinite(value))
				return;
			if (value == 0.0)
				++zeroCount;
			++finiteCount;
			const auto delta = value - mean;
			mean += delta / static_cast<double>(finiteCount);
			sumSquaredDelta += delta * (value - mean);
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}

		void addZeros(const std::uint64_t count)
		{
			if (count == 0)
				return;
			zeroCount += count;
			if (finiteCount == 0)
			{
				finiteCount = count;
				minimum = maximum = mean = 0.0;
				return;
			}
			const auto combinedCount = finiteCount + count;
			const auto delta = -mean;
			sumSquaredDelta += delta * delta * static_cast<double>(finiteCount) * static_cast<double>(count) / static_cast<double>(combinedCount);
			mean += delta * static_cast<double>(count) / static_cast<double>(combinedCount);
			finiteCount = combinedCount;
			minimum = std::min(minimum, 0.0);
			maximum = std::max(maximum, 0.0);
		}

		void merge(const WorkerAccumulator& other)
		{
			nanCount += other.nanCount;
			zeroCount += other.zeroCount;
			if (other.finiteCount == 0)
				return;

			const auto otherMean = other.reference + other.sumDelta / static_cast<double>(other.finiteCount);
			const auto otherSumSquaredDelta = std::max(0.0, other.sumSquaredDelta - other.sumDelta * other.sumDelta / static_cast<double>(other.finiteCount));
			if (finiteCount == 0)
			{
				finiteCount = other.finiteCount;
				mean = otherMean;
				sumSquaredDelta = otherSumSquaredDelta;
				minimum = other.minimum;
				maximum = other.maximum;
				return;
			}

			const auto combinedCount = finiteCount + other.finiteCount;
			const auto delta = otherMean - mean;
			sumSquaredDelta += otherSumSquaredDelta + delta * delta * static_cast<double>(finiteCount) * static_cast<double>(other.finiteCount) / static_cast<double>(combinedCount);
			mean += delta * static_cast<double>(other.finiteCount) / static_cast<double>(combinedCount);
			finiteCount = combinedCount;
			minimum = std::min(minimum, other.minimum);
			maximum = std::max(maximum, other.maximum);
		}
	};

	std::vector<Accumulator> accumulators(static_cast<std::size_t>(columnCount));
	std::vector<std::uint64_t> storedPerColumn;
	std::vector<std::uint64_t> totalStoredPerColumn;
	bool weightedColumnSample = false;
	struct WorkerStatistics
	{
		std::vector<WorkerAccumulator> accumulators;
		std::vector<std::uint64_t> storedPerColumn;
	};
	const auto hardwareThreadCount = std::max(1u, std::thread::hardware_concurrency());
	const auto bytesPerWorker = static_cast<std::uint64_t>(columnCount) * (sizeof(WorkerAccumulator) + sizeof(std::uint64_t));
	const auto workersByMemory = std::max<std::uint64_t>(1, (128ULL * 1024 * 1024) / std::max<std::uint64_t>(1, bytesPerWorker));
	const auto workerCount = static_cast<std::size_t>(std::min<std::uint64_t>({ hardwareThreadCount, 16, workersByMemory }));
	std::vector<WorkerStatistics> workerStatistics(workerCount);
	for (auto& worker : workerStatistics)
	{
		worker.accumulators.resize(static_cast<std::size_t>(columnCount));
		worker.storedPerColumn.resize(static_cast<std::size_t>(columnCount), 0);
	}
	constexpr std::uint64_t minimumEntriesPerWorker = 128 * 1024;
	const auto getActiveWorkerCount = [&](const std::uint64_t entryCount) {
		const auto neededWorkers = std::max<std::uint64_t>(1, entryCount / minimumEntriesPerWorker + (entryCount % minimumEntriesPerWorker != 0));
		return std::min({ workerCount, static_cast<std::size_t>(omp_get_max_threads()), static_cast<std::size_t>(neededWorkers) });
	};
	const auto processEntriesInParallel = [&](const std::uint64_t entryCount, const auto& processEntry) {
		const auto activeWorkerCount = getActiveWorkerCount(entryCount);
		if (activeWorkerCount == 1)
		{
			for (std::uint64_t entry = 0; entry < entryCount; ++entry)
				processEntry(std::size_t { 0 }, entry);
			return;
		}

		#pragma omp parallel num_threads(static_cast<int>(activeWorkerCount))
		{
			const auto workerIndex = static_cast<std::size_t>(omp_get_thread_num());
			const auto actualWorkerCount = static_cast<std::uint64_t>(omp_get_num_threads());
			const auto firstEntry = entryCount * workerIndex / actualWorkerCount;
			const auto lastEntry = entryCount * (workerIndex + 1) / actualWorkerCount;
			for (auto entry = firstEntry; entry < lastEntry; ++entry)
				processEntry(workerIndex, entry);
		}
	};
	H5G_obj_t matrixObjectType = H5G_UNKNOWN;
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		if (_file->getObjnameByIdx(index) == "X")
		{
			matrixObjectType = _file->getObjTypeByIdx(index);
			break;
		}

	// Larger blocks amortize OpenMP region setup and HDF5 call overhead.
	constexpr std::uint64_t maximumValuesPerRead = 2 * 1024 * 1024;
	if (matrixObjectType == H5G_DATASET)
	{
		const auto matrix = _file->openDataSet("X");
		const auto typeClass = matrix.getDataType().getClass();
		if (typeClass != H5T_FLOAT && typeClass != H5T_INTEGER && typeClass != H5T_ENUM)
			return {};

		const auto columnBlockSize = std::min(columnCount, maximumValuesPerRead);
		const auto rowBlockSize = std::max<std::uint64_t>(1, maximumValuesPerRead / columnBlockSize);
		for (std::uint64_t sampledRowOffset = 0; sampledRowOffset < sampledRowCount; sampledRowOffset += rowBlockSize)
		{
			const auto rowsInBlock = std::min(rowBlockSize, sampledRowCount - sampledRowOffset);
			const auto rowOffset = sampledRowOffset * sampledRowStride;
			for (std::uint64_t columnOffset = 0; columnOffset < columnCount; columnOffset += columnBlockSize)
			{
				const auto columnsInBlock = std::min(columnBlockSize, columnCount - columnOffset);
				const hsize_t start[] = { rowOffset, columnOffset };
				const hsize_t stride[] = { sampledRowStride, 1 };
				const hsize_t count[] = { rowsInBlock, columnsInBlock };
				const hsize_t block[] = { 1, 1 };
				auto fileSpace = matrix.getSpace();
				fileSpace.selectHyperslab(H5S_SELECT_SET, count, start, stride, block);
				H5::DataSpace memorySpace(2, count);
				std::vector<double> values(static_cast<std::size_t>(rowsInBlock * columnsInBlock));
				matrix.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
				processEntriesInParallel(values.size(), [&](const std::size_t workerIndex, const std::uint64_t valueIndex) {
					const auto column = columnOffset + valueIndex % columnsInBlock;
					workerStatistics[workerIndex].accumulators[static_cast<std::size_t>(column)].add(values[static_cast<std::size_t>(valueIndex)]);
				});
			}
			if (progress)
				progress(static_cast<double>(sampledRowOffset + rowsInBlock) / static_cast<double>(sampledRowCount));
		}
	}
	else if (matrixObjectType == H5G_GROUP)
	{
		const auto sparseInfo = getSparseMatrixInfo();
		if (!sparseInfo.available)
			return {};
		weightedColumnSample = !fullScan && sparseInfo.columnCompressed;
		const auto matrix = _file->openGroup("X");
		const auto valuesData = matrix.openDataSet("data");
		const auto indicesData = matrix.openDataSet("indices");
		const auto pointerData = matrix.openDataSet("indptr");
		const auto valuesTypeClass = valuesData.getDataType().getClass();
		if (valuesTypeClass != H5T_FLOAT && valuesTypeClass != H5T_INTEGER && valuesTypeClass != H5T_ENUM)
			return {};
		storedPerColumn.resize(static_cast<std::size_t>(columnCount), 0);
		if (weightedColumnSample)
			totalStoredPerColumn.resize(static_cast<std::size_t>(columnCount), 0);
		if (!sparseInfo.columnCompressed)
		{
			if (fullScan)
			{
				constexpr std::uint64_t majorBlockSize = 65536;
				for (std::uint64_t rowOffset = 0; rowOffset < rowCount; rowOffset += majorBlockSize)
				{
					const auto rowsInBlock = std::min(majorBlockSize, rowCount - rowOffset);
					const hsize_t pointerStart[] = { rowOffset };
					const hsize_t pointerCount[] = { rowsInBlock + 1 };
					auto pointerFileSpace = pointerData.getSpace();
					pointerFileSpace.selectHyperslab(H5S_SELECT_SET, pointerCount, pointerStart);
					H5::DataSpace pointerMemorySpace(1, pointerCount);
					std::vector<std::uint64_t> pointers(static_cast<std::size_t>(rowsInBlock + 1));
					pointerData.read(pointers.data(), H5::PredType::NATIVE_UINT64, pointerMemorySpace, pointerFileSpace);
					if (!std::is_sorted(pointers.begin(), pointers.end()))
						return {};

					const auto firstEntry = pointers.front();
					const auto lastEntry = pointers.back();
					for (std::uint64_t entryOffset = firstEntry; entryOffset < lastEntry; entryOffset += maximumValuesPerRead)
					{
						const auto entriesInBlock = std::min(maximumValuesPerRead, lastEntry - entryOffset);
						const hsize_t entryStart[] = { entryOffset };
						const hsize_t entryCount[] = { entriesInBlock };
						auto valuesFileSpace = valuesData.getSpace();
						valuesFileSpace.selectHyperslab(H5S_SELECT_SET, entryCount, entryStart);
						auto indicesFileSpace = indicesData.getSpace();
						indicesFileSpace.selectHyperslab(H5S_SELECT_SET, entryCount, entryStart);
						H5::DataSpace entryMemorySpace(1, entryCount);
						std::vector<double> values(static_cast<std::size_t>(entriesInBlock));
						std::vector<std::uint64_t> indices(static_cast<std::size_t>(entriesInBlock));
						valuesData.read(values.data(), H5::PredType::NATIVE_DOUBLE, entryMemorySpace, valuesFileSpace);
						indicesData.read(indices.data(), H5::PredType::NATIVE_UINT64, entryMemorySpace, indicesFileSpace);

						processEntriesInParallel(entriesInBlock, [&](const std::size_t workerIndex, const std::uint64_t offset) {
							const auto column = indices[static_cast<std::size_t>(offset)];
							if (column < columnCount)
							{
								workerStatistics[workerIndex].accumulators[static_cast<std::size_t>(column)].add(values[static_cast<std::size_t>(offset)]);
								++workerStatistics[workerIndex].storedPerColumn[static_cast<std::size_t>(column)];
							}
						});
					}
					if (progress)
						progress(static_cast<double>(rowOffset + rowsInBlock) / static_cast<double>(rowCount));
				}
			}
			else
			{
				constexpr std::uint64_t rowPointerBlockSize = 4096;
				for (std::uint64_t rowOffset = 0; rowOffset < rowCount;)
				{
					const auto rowsInBlock = std::min(rowPointerBlockSize, rowCount - rowOffset);
					const auto rowBlockEnd = rowOffset + rowsInBlock;
					const auto firstSampledRow = rowOffset + (sampledRowStride - rowOffset % sampledRowStride) % sampledRowStride;
					if (firstSampledRow >= rowBlockEnd)
					{
						if (firstSampledRow >= rowCount)
							break;
						rowOffset = (firstSampledRow / rowPointerBlockSize) * rowPointerBlockSize;
						continue;
					}
					const hsize_t pointerStart[] = { rowOffset };
					const hsize_t pointerCount[] = { rowsInBlock + 1 };
					auto pointerFileSpace = pointerData.getSpace();
					pointerFileSpace.selectHyperslab(H5S_SELECT_SET, pointerCount, pointerStart);
					H5::DataSpace pointerMemorySpace(1, pointerCount);
					std::vector<std::uint64_t> pointers(static_cast<std::size_t>(rowsInBlock + 1));
					pointerData.read(pointers.data(), H5::PredType::NATIVE_UINT64, pointerMemorySpace, pointerFileSpace);
					if (!std::is_sorted(pointers.begin(), pointers.end()))
						return {};

					std::vector<std::pair<std::uint64_t, std::uint64_t>> selectedEntryRanges;
					std::uint64_t selectedEntryCount = 0;
					const auto readSelectedEntries = [&] {
						if (selectedEntryCount == 0)
							return;

						const hsize_t memoryDimensions[] = { selectedEntryCount };
						H5::DataSpace memorySpace(1, memoryDimensions);
						auto valuesFileSpace = valuesData.getSpace();
						auto indicesFileSpace = indicesData.getSpace();
						for (std::size_t rangeIndex = 0; rangeIndex < selectedEntryRanges.size(); ++rangeIndex)
						{
							const hsize_t start[] = { selectedEntryRanges[rangeIndex].first };
							const hsize_t count[] = { selectedEntryRanges[rangeIndex].second };
							const auto operation = rangeIndex == 0 ? H5S_SELECT_SET : H5S_SELECT_OR;
							valuesFileSpace.selectHyperslab(operation, count, start);
							indicesFileSpace.selectHyperslab(operation, count, start);
						}
						std::vector<double> values(static_cast<std::size_t>(selectedEntryCount));
						std::vector<std::uint64_t> indices(static_cast<std::size_t>(selectedEntryCount));
						valuesData.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, valuesFileSpace);
						indicesData.read(indices.data(), H5::PredType::NATIVE_UINT64, memorySpace, indicesFileSpace);
						processEntriesInParallel(selectedEntryCount, [&](const std::size_t workerIndex, const std::uint64_t entryIndex) {
							const auto column = indices[static_cast<std::size_t>(entryIndex)];
							if (column < columnCount)
							{
								workerStatistics[workerIndex].accumulators[static_cast<std::size_t>(column)].add(values[static_cast<std::size_t>(entryIndex)]);
								++workerStatistics[workerIndex].storedPerColumn[static_cast<std::size_t>(column)];
							}
						});
						selectedEntryRanges.clear();
						selectedEntryCount = 0;
					};

					for (auto row = firstSampledRow; row < rowBlockEnd; row += sampledRowStride)
					{
						const auto localRow = static_cast<std::size_t>(row - rowOffset);
						const auto rowFirstEntry = pointers[localRow];
						const auto rowLastEntry = pointers[localRow + 1];
						if (rowLastEntry < rowFirstEntry)
							return {};
						for (auto entryOffset = rowFirstEntry; entryOffset < rowLastEntry;)
						{
							const auto entriesInRange = std::min(maximumValuesPerRead, rowLastEntry - entryOffset);
							if (selectedEntryCount != 0 && selectedEntryCount + entriesInRange > maximumValuesPerRead)
								readSelectedEntries();
							selectedEntryRanges.emplace_back(entryOffset, entriesInRange);
							selectedEntryCount += entriesInRange;
							entryOffset += entriesInRange;
							if (selectedEntryCount >= maximumValuesPerRead)
								readSelectedEntries();
						}
					}
					readSelectedEntries();
					if (progress)
						progress(static_cast<double>(rowBlockEnd) / static_cast<double>(rowCount));
					rowOffset = rowBlockEnd;
				}
			}
		}
		else
		{
			constexpr std::uint64_t majorBlockSize = 4096;
			for (std::uint64_t majorOffset = 0; majorOffset < columnCount; majorOffset += majorBlockSize)
			{
				const auto majorsInBlock = std::min(majorBlockSize, columnCount - majorOffset);
				const hsize_t pointerStart[] = { majorOffset };
				const hsize_t pointerCount[] = { majorsInBlock + 1 };
				auto pointerFileSpace = pointerData.getSpace();
				pointerFileSpace.selectHyperslab(H5S_SELECT_SET, pointerCount, pointerStart);
				H5::DataSpace pointerMemorySpace(1, pointerCount);
				std::vector<std::uint64_t> pointers(static_cast<std::size_t>(majorsInBlock + 1));
				pointerData.read(pointers.data(), H5::PredType::NATIVE_UINT64, pointerMemorySpace, pointerFileSpace);
				if (!std::is_sorted(pointers.begin(), pointers.end()))
					return {};
				const auto firstEntry = pointers.front();
				const auto lastEntry = pointers.back();
				if (lastEntry < firstEntry)
					return {};

				if (!fullScan)
				{
					struct SampleRange
					{
						std::uint64_t offset;
						std::uint64_t count;
						std::size_t column;
					};
					const auto maxSamplesPerColumn = std::max<std::uint64_t>(1, std::min<std::uint64_t>(128, maximumSampledValues / columnCount));
					std::vector<SampleRange> sampleRanges;
					std::uint64_t sampleValueCount = 0;
					for (std::uint64_t localMajor = 0; localMajor < majorsInBlock; ++localMajor)
					{
						const auto column = majorOffset + localMajor;
						const auto columnFirst = pointers[static_cast<std::size_t>(localMajor)];
						const auto columnLast = pointers[static_cast<std::size_t>(localMajor + 1)];
						if (columnLast < columnFirst)
							return {};
						const auto storedCount = columnLast - columnFirst;
						totalStoredPerColumn[static_cast<std::size_t>(column)] = storedCount;
						const auto sampledCount = std::min(storedCount, maxSamplesPerColumn);
						if (sampledCount == 0)
							continue;
						const auto segmentCount = std::min<std::uint64_t>(4, sampledCount);
						const auto valuesPerSegment = sampledCount / segmentCount;
						const auto extraValues = sampledCount % segmentCount;
						std::uint64_t sampledBefore = 0;
						for (std::uint64_t segment = 0; segment < segmentCount; ++segment)
						{
							const auto segmentSize = valuesPerSegment + (segment < extraValues ? 1 : 0);
							const auto availableGap = storedCount - sampledCount;
							const auto segmentOffset = segmentCount == 1 ? availableGap / 2 : availableGap * segment / (segmentCount - 1);
							sampleRanges.push_back({ columnFirst + segmentOffset + sampledBefore, segmentSize, static_cast<std::size_t>(column) });
							sampleValueCount += segmentSize;
							sampledBefore += segmentSize;
						}
					}

					if (sampleValueCount != 0)
					{
						const hsize_t memoryDimensions[] = { sampleValueCount };
						H5::DataSpace memorySpace(1, memoryDimensions);
						auto valuesFileSpace = valuesData.getSpace();
						for (std::size_t rangeIndex = 0; rangeIndex < sampleRanges.size(); ++rangeIndex)
						{
							const hsize_t start[] = { sampleRanges[rangeIndex].offset };
							const hsize_t count[] = { sampleRanges[rangeIndex].count };
							valuesFileSpace.selectHyperslab(rangeIndex == 0 ? H5S_SELECT_SET : H5S_SELECT_OR, count, start);
						}
						std::vector<double> values(static_cast<std::size_t>(sampleValueCount));
						valuesData.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, valuesFileSpace);
						std::uint64_t valueOffset = 0;
						for (const auto& range : sampleRanges)
							for (std::uint64_t index = 0; index < range.count; ++index)
							{
								workerStatistics[0].accumulators[range.column].add(values[static_cast<std::size_t>(valueOffset++)]);
								++workerStatistics[0].storedPerColumn[range.column];
							}
					}
				}
				else
				{
					for (std::uint64_t entryOffset = firstEntry; entryOffset < lastEntry; entryOffset += maximumValuesPerRead)
					{
						const auto entriesInBlock = std::min(maximumValuesPerRead, lastEntry - entryOffset);
					const hsize_t entryStart[] = { entryOffset };
					const hsize_t entryCount[] = { entriesInBlock };
					auto valuesFileSpace = valuesData.getSpace();
					valuesFileSpace.selectHyperslab(H5S_SELECT_SET, entryCount, entryStart);
					auto indicesFileSpace = indicesData.getSpace();
					indicesFileSpace.selectHyperslab(H5S_SELECT_SET, entryCount, entryStart);
					H5::DataSpace entryMemorySpace(1, entryCount);
					std::vector<double> values(static_cast<std::size_t>(entriesInBlock));
					std::vector<std::uint64_t> indices(static_cast<std::size_t>(entriesInBlock));
					valuesData.read(values.data(), H5::PredType::NATIVE_DOUBLE, entryMemorySpace, valuesFileSpace);
					indicesData.read(indices.data(), H5::PredType::NATIVE_UINT64, entryMemorySpace, indicesFileSpace);
					// CSC entries are already grouped by column. Find each worker's first
					// column once, then advance through indptr instead of binary-searching
					// the pointer array for every non-zero value.
					const auto activeWorkerCount = getActiveWorkerCount(entriesInBlock);
					#pragma omp parallel num_threads(static_cast<int>(activeWorkerCount))
					{
						const auto workerIndex = static_cast<std::size_t>(omp_get_thread_num());
						const auto actualWorkerCount = static_cast<std::uint64_t>(omp_get_num_threads());
						const auto firstEntry = entriesInBlock * workerIndex / actualWorkerCount;
						const auto lastEntry = entriesInBlock * (workerIndex + 1) / actualWorkerCount;
						auto majorIterator = std::upper_bound(pointers.begin(), pointers.end(), entryOffset + firstEntry);
						if (majorIterator != pointers.begin())
						{
							--majorIterator;
							auto majorIndex = static_cast<std::uint64_t>(std::distance(pointers.begin(), majorIterator));
							for (auto offset = firstEntry; offset < lastEntry; ++offset)
							{
								const auto absoluteEntry = entryOffset + offset;
								while (majorIterator + 1 != pointers.end() && absoluteEntry >= *(majorIterator + 1))
								{
									++majorIterator;
									++majorIndex;
								}
								const auto major = majorOffset + majorIndex;
								const auto row = indices[static_cast<std::size_t>(offset)];
								if (major >= columnCount || row >= rowCount || (!fullScan && row % sampledRowStride != 0))
									continue;
								workerStatistics[workerIndex].accumulators[static_cast<std::size_t>(major)].add(values[static_cast<std::size_t>(offset)]);
								++workerStatistics[workerIndex].storedPerColumn[static_cast<std::size_t>(major)];
							}
						}
					}
					}
				}
				if (progress)
					progress(static_cast<double>(majorOffset + majorsInBlock) / static_cast<double>(columnCount));
			}
		}

		for (const auto& worker : workerStatistics)
			for (std::size_t column = 0; column < accumulators.size(); ++column)
			{
				accumulators[column].merge(worker.accumulators[column]);
				storedPerColumn[column] += worker.storedPerColumn[column];
			}
		if (!weightedColumnSample)
			for (std::size_t column = 0; column < accumulators.size(); ++column)
				accumulators[column].addZeros(sampledRowCount - std::min(sampledRowCount, storedPerColumn[column]));
	}
	else
	{
		return {};
	}

	if (matrixObjectType == H5G_DATASET)
		for (const auto& worker : workerStatistics)
			for (std::size_t column = 0; column < accumulators.size(); ++column)
				accumulators[column].merge(worker.accumulators[column]);

	std::vector<H5ADDimensionStatistics> statistics;
	statistics.reserve(accumulators.size());
	for (std::size_t column = 0; column < accumulators.size(); ++column)
	{
		const auto& accumulator = accumulators[column];
		H5ADDimensionStatistics result;
		result.exact = fullScan;
		if (weightedColumnSample)
		{
			const auto sampleCount = storedPerColumn[column];
			const auto storedCount = totalStoredPerColumn[column];
			const auto implicitZeroCount = rowCount - std::min(rowCount, storedCount);
			if (sampleCount == 0)
			{
				result.finiteValueCount = rowCount;
				result.zeroFraction = 1.0;
				result.minimum = result.maximum = 0.0;
			}
			else
			{
				const auto sampleWeight = static_cast<double>(storedCount) / static_cast<double>(sampleCount);
				const auto estimatedFiniteStoredCount = static_cast<double>(accumulator.finiteCount) * sampleWeight;
				const auto finiteCount = estimatedFiniteStoredCount + static_cast<double>(implicitZeroCount);
				const auto mean = accumulator.finiteCount > 0 ? accumulator.mean * estimatedFiniteStoredCount / std::max(1.0, finiteCount) : 0.0;
				const auto sampleVariance = accumulator.finiteCount > 0 ? accumulator.sumSquaredDelta / static_cast<double>(accumulator.finiteCount) : 0.0;
				const auto secondMoment = sampleVariance + accumulator.mean * accumulator.mean;
				const auto variance = finiteCount > 0 ? std::max(0.0, secondMoment * estimatedFiniteStoredCount / finiteCount - mean * mean) : 0.0;
				result.mean = mean;
				result.standardDeviation = std::sqrt(variance);
				result.finiteValueCount = static_cast<std::uint64_t>(std::llround(finiteCount));
				result.nanCount = static_cast<std::uint64_t>(std::llround(static_cast<double>(accumulator.nanCount) * sampleWeight));
				result.zeroFraction = std::min(1.0, static_cast<double>(implicitZeroCount) / static_cast<double>(rowCount) +
					static_cast<double>(accumulator.zeroCount) * sampleWeight / static_cast<double>(rowCount));
				if (accumulator.finiteCount > 0)
				{
					result.minimum = accumulator.minimum;
					result.maximum = accumulator.maximum;
				}
				if (implicitZeroCount > 0 || accumulator.zeroCount > 0)
				{
					result.minimum = std::min(result.minimum, 0.0);
					result.maximum = std::max(result.maximum, 0.0);
				}
			}
		}
		else
		{
			result.nanCount = accumulator.nanCount;
			result.finiteValueCount = accumulator.finiteCount;
			result.zeroFraction = static_cast<double>(std::min(accumulator.zeroCount, sampledRowCount)) / static_cast<double>(sampledRowCount);
			if (accumulator.finiteCount > 0)
			{
				result.mean = accumulator.mean;
				result.standardDeviation = std::sqrt(accumulator.sumSquaredDelta / static_cast<double>(accumulator.finiteCount));
				result.minimum = accumulator.minimum;
				result.maximum = accumulator.maximum;
			}
		}
		result.available = true;
		result.analyzedRowCount = sampledRowCount;
		statistics.push_back(result);
	}
	return statistics;
}

H5ADSparseMatrixInfo HDF5_AD_Loader::getSparseMatrixInfo() const
{
	H5ADSparseMatrixInfo result;
	if (!_file)
		return result;

	try
	{
		H5G_obj_t objectType = H5G_UNKNOWN;
		for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		{
			if (_file->getObjnameByIdx(index) == "X")
			{
				objectType = _file->getObjTypeByIdx(index);
				break;
			}
		}

		if (objectType != H5G_GROUP)
			return result;

		auto group = _file->openGroup("X");
		if (!group.attrExists("shape") || !H5Utils::contains_name(group, "data") || !H5Utils::contains_name(group, "indices") || !H5Utils::contains_name(group, "indptr"))
			return result;

		if (group.attrExists("encoding-type"))
		{
			std::string encodingType;
			auto encodingAttribute = group.openAttribute("encoding-type");
			encodingAttribute.read(encodingAttribute.getDataType(), encodingType);
			if (encodingType != "csr_matrix" && encodingType != "csc_matrix")
				return result;
			result.columnCompressed = encodingType == "csc_matrix";
		}

		const auto dataSet    = group.openDataSet("data");
		const auto indicesSet = group.openDataSet("indices");
		const auto indptrSet  = group.openDataSet("indptr");
		const auto dataSpace    = dataSet.getSpace();
		const auto indicesSpace = indicesSet.getSpace();
		const auto indptrSpace   = indptrSet.getSpace();
		if (dataSpace.getSimpleExtentNdims() != 1 || indicesSpace.getSimpleExtentNdims() != 1 || indptrSpace.getSimpleExtentNdims() != 1 ||
			dataSpace.getSimpleExtentNpoints() < 0 || dataSpace.getSimpleExtentNpoints() != indicesSpace.getSimpleExtentNpoints())
			return {};
		const auto dataClass = dataSet.getDataType().getClass();
		if (dataClass != H5T_FLOAT && dataClass != H5T_INTEGER && dataClass != H5T_ENUM)
			return {};

		const auto [rows, columns] = getMatrixShape();
		const auto compressedSize = result.columnCompressed ? columns : rows;
		if (compressedSize >= std::numeric_limits<std::uint64_t>::max() || indptrSpace.getSimpleExtentNpoints() < 0 ||
			static_cast<std::uint64_t>(indptrSpace.getSimpleExtentNpoints()) != compressedSize + 1)
			return {};

		result.storedEntryCount = static_cast<std::uint64_t>(dataSpace.getSimpleExtentNpoints());
		result.available = true;
	}
	catch (const H5::Exception&)
	{
		return {};
	}

	return result;
}

H5ADMatrixScanResult HDF5_AD_Loader::estimateTargetElementType() const
{
	return analyzeTargetElementType(false, {});
}

H5ADMatrixScanResult HDF5_AD_Loader::determineTargetElementType(const std::function<void(double)>& progress) const
{
	return analyzeTargetElementType(true, progress);
}

H5ADMatrixScanResult HDF5_AD_Loader::analyzeTargetElementType(const bool fullScan, const std::function<void(double)>& progress) const
{
	H5ADMatrixScanResult result;
	if (!_file)
		return result;

	H5::DataSet dataSet;
	bool sparseData = false;
	for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
	{
		if (_file->getObjnameByIdx(index) != "X")
			continue;

		if (_file->getObjTypeByIdx(index) == H5G_DATASET)
			dataSet = _file->openDataSet("X");
		else if (_file->getObjTypeByIdx(index) == H5G_GROUP)
		{
			auto group = _file->openGroup("X");
			if (H5Utils::contains_name(group, "data") && H5Utils::contains_name(group, "indices") && H5Utils::contains_name(group, "indptr"))
			{
				dataSet = group.openDataSet("data");
				sparseData = true;
			}
		}
		break;
	}

	if (dataSet.getId() < 0)
		return result;

	const auto dataType = dataSet.getDataType();
	const auto dataClass = dataType.getClass();
	if (dataClass != H5T_FLOAT && dataClass != H5T_INTEGER && dataClass != H5T_ENUM)
		return result;

	double minimum = std::numeric_limits<double>::infinity();
	double maximum = -std::numeric_limits<double>::infinity();
	bool allIntegral = true;
	std::uint64_t sampledValues = 0;
	std::uint64_t zeroValues = 0;
	const auto analyzeValues = [&](const std::vector<double>& values) {
		struct BlockSummary
		{
			double minimum = std::numeric_limits<double>::infinity();
			double maximum = -std::numeric_limits<double>::infinity();
			bool allIntegral = true;
			std::uint64_t sampledValues = 0;
			std::uint64_t zeroValues = 0;
		};
		std::vector<BlockSummary> blockSummaries(static_cast<std::size_t>(omp_get_max_threads()));
		#pragma omp parallel if(values.size() >= 65536)
		{
			const auto workerIndex = static_cast<std::size_t>(omp_get_thread_num());
			const auto workerCount = static_cast<std::size_t>(omp_get_num_threads());
			const auto firstIndex = values.size() * workerIndex / workerCount;
			const auto lastIndex = values.size() * (workerIndex + 1) / workerCount;
			auto& summary = blockSummaries[workerIndex];
			for (auto index = firstIndex; index < lastIndex; ++index)
			{
				const auto value = values[index];
				if (value == 0.0)
					++summary.zeroValues;

				if (!std::isfinite(value))
				{
					summary.allIntegral = false;
					++summary.sampledValues;
					continue;
				}

				summary.minimum = std::min(summary.minimum, value);
				summary.maximum = std::max(summary.maximum, value);
				summary.allIntegral = summary.allIntegral && std::trunc(value) == value;
				++summary.sampledValues;
			}
		}
		for (const auto& summary : blockSummaries)
		{
			minimum = std::min(minimum, summary.minimum);
			maximum = std::max(maximum, summary.maximum);
			allIntegral = allIntegral && summary.allIntegral;
			sampledValues += summary.sampledValues;
			zeroValues += summary.zeroValues;
		}
	};

	if (sparseData)
	{
		result.sparseInput = true;
		const auto dataSpace = dataSet.getSpace();
		const auto valueCount = static_cast<std::uint64_t>(dataSpace.getSimpleExtentNpoints());
		result.storedEntryCount = valueCount;
		const auto blockSize = fullScan ? std::uint64_t { 1024 * 1024 } : std::uint64_t { 1024 };
		const auto segmentCount = valueCount == 0 ? 0 : fullScan ? (valueCount - 1) / blockSize + 1 : std::min<std::uint64_t>(64, (valueCount - 1) / blockSize + 1);
		for (std::uint64_t segment = 0; segment < segmentCount; ++segment)
		{
			const auto currentBlockSize = fullScan ? std::min(blockSize, valueCount - segment * blockSize) : blockSize;
			const auto startOffset = fullScan ? segment * blockSize : segment * (valueCount - blockSize) / std::max<std::uint64_t>(1, segmentCount - 1);
			const hsize_t start[] = { startOffset };
			const hsize_t count[] = { currentBlockSize };
			auto fileSpace = dataSet.getSpace();
			fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
			H5::DataSpace memorySpace(1, count);
			std::vector<double> values(static_cast<std::size_t>(currentBlockSize));
			dataSet.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
			analyzeValues(values);
			if (fullScan && progress)
				progress(static_cast<double>(segment + 1) / static_cast<double>(segmentCount));
		}
		if (fullScan)
		{
			const auto shape = getMatrixShape();
			if (shape.first > 0 && shape.second > 0 && shape.first <= std::numeric_limits<std::uint64_t>::max() / shape.second)
			{
				result.elementCount = shape.first * shape.second;
				if (result.elementCount >= valueCount)
				{
					result.zeroCount = std::min(result.elementCount, result.elementCount - valueCount + zeroValues);
					result.zeroFraction = static_cast<double>(result.zeroCount) / static_cast<double>(result.elementCount);
					result.sparsityAvailable = true;
				}
			}
		}
		else
		{
			const auto shape = getMatrixShape();
			if (shape.first > 0 && shape.second > 0 && shape.first <= std::numeric_limits<std::uint64_t>::max() / shape.second)
			{
				result.elementCount = shape.first * shape.second;
				if (result.elementCount >= valueCount)
				{
					const auto sampledZeroFraction = sampledValues > 0 ? static_cast<double>(zeroValues) / static_cast<double>(sampledValues) : 0.0;
					result.zeroFraction = static_cast<double>(result.elementCount - valueCount) / static_cast<double>(result.elementCount) +
						static_cast<double>(valueCount) / static_cast<double>(result.elementCount) * sampledZeroFraction;
					result.sparsityAvailable = true;
				}
			}
		}
	}
	else
	{
		const auto dataSpace = dataSet.getSpace();
		if (dataSpace.getSimpleExtentNdims() != 2)
			return result;

		hsize_t dimensions[2] = {};
		dataSpace.getSimpleExtentDims(dimensions);
		if (dimensions[0] == 0 || dimensions[1] == 0)
			return result;
		if (fullScan && dimensions[0] <= std::numeric_limits<std::uint64_t>::max() / dimensions[1])
			result.elementCount = dimensions[0] * dimensions[1];
		const auto scanRowCount = fullScan ? dimensions[0] : std::min<std::uint64_t>(128, dimensions[0]);
		const auto sampleColumnCount = std::min<std::uint64_t>(fullScan ? 1024 * 1024 : 512, dimensions[1]);
		if (fullScan)
		{
			constexpr std::uint64_t maximumValuesPerRead = 2 * 1024 * 1024;
			const auto columnBlockSize = std::min(dimensions[1], maximumValuesPerRead);
			const auto rowBlockSize = std::max<std::uint64_t>(1, maximumValuesPerRead / columnBlockSize);
			for (std::uint64_t rowOffset = 0; rowOffset < dimensions[0]; rowOffset += rowBlockSize)
			{
				const auto rowsInBlock = std::min(rowBlockSize, dimensions[0] - rowOffset);
				for (std::uint64_t columnOffset = 0; columnOffset < dimensions[1]; columnOffset += columnBlockSize)
				{
					const auto columnsInBlock = std::min(columnBlockSize, dimensions[1] - columnOffset);
					const hsize_t start[] = { rowOffset, columnOffset };
					const hsize_t count[] = { rowsInBlock, columnsInBlock };
					auto fileSpace = dataSet.getSpace();
					fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
					H5::DataSpace memorySpace(2, count);
					std::vector<double> values(static_cast<std::size_t>(rowsInBlock * columnsInBlock));
					dataSet.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
					analyzeValues(values);
				}
				if (fullScan && progress)
					progress(static_cast<double>(rowOffset + rowsInBlock) / static_cast<double>(dimensions[0]));
			}
		}
		else
		{
			for (std::uint64_t sampleRow = 0; sampleRow < scanRowCount; ++sampleRow)
			{
				const auto row = sampleRow * (dimensions[0] - 1) / std::max<std::uint64_t>(1, scanRowCount - 1);
				const auto column = sampleRow * (dimensions[1] - sampleColumnCount) / std::max<std::uint64_t>(1, scanRowCount - 1);
				const hsize_t start[] = { row, column };
				const hsize_t count[] = { 1, sampleColumnCount };
				auto fileSpace = dataSet.getSpace();
				fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
				H5::DataSpace memorySpace(2, count);
				std::vector<double> values(static_cast<std::size_t>(sampleColumnCount));
				dataSet.read(values.data(), H5::PredType::NATIVE_DOUBLE, memorySpace, fileSpace);
				analyzeValues(values);
			}
		}
		if (fullScan && result.elementCount == sampledValues)
		{
			result.zeroCount = zeroValues;
			result.zeroFraction = static_cast<double>(zeroValues) / static_cast<double>(result.elementCount);
			result.sparsityAvailable = true;
		}
		else if (!fullScan && dimensions[0] <= std::numeric_limits<std::uint64_t>::max() / dimensions[1])
		{
			result.elementCount = dimensions[0] * dimensions[1];
			result.zeroFraction = sampledValues > 0 ? static_cast<double>(zeroValues) / static_cast<double>(sampledValues) : 0.0;
			result.sparsityAvailable = sampledValues > 0;
		}
	}
	result.analyzedValueCount = sampledValues;

	if (fullScan)
	{
		if (sparseData && result.sparsityAvailable && result.zeroCount > 0)
		{
			minimum = std::min(minimum, 0.0);
			maximum = std::max(maximum, 0.0);
		}

		result.rangeAvailable = std::isfinite(minimum) && std::isfinite(maximum);
		if (result.rangeAvailable)
		{
			result.minimum = minimum;
			result.maximum = maximum;
		}
	}

	if (sampledValues == 0)
		result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint8) + 3;
	else if (allIntegral && (dataClass == H5T_FLOAT || dataType.getSize() <= 4))
	{
		if (minimum < 0)
		{
			if (minimum >= std::numeric_limits<std::int8_t>::min() && maximum <= std::numeric_limits<std::int8_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::int8) + 3;
			else if (minimum >= std::numeric_limits<std::int16_t>::min() && maximum <= std::numeric_limits<std::int16_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::int16) + 3;
		}
		else
		{
			if (maximum <= std::numeric_limits<std::uint8_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint8) + 3;
			else if (maximum <= std::numeric_limits<std::uint16_t>::max())
				result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::uint16) + 3;
		}
	}

	if (result.recommendedTypeIndex < 0)
		result.recommendedTypeIndex = static_cast<int>(PointData::ElementTypeSpecifier::float32) + 3;

	return result;
}

bool HDF5_AD_Loader::sourceMayLosePrecision() const
{
	if (!_file)
		return false;

	try
	{
		for (hsize_t index = 0; index < _file->getNumObjs(); ++index)
		{
			if (_file->getObjnameByIdx(index) != "X")
				continue;

			H5::DataType dataType;
			if (_file->getObjTypeByIdx(index) == H5G_DATASET)
				dataType = _file->openDataSet("X").getDataType();
			else if (_file->getObjTypeByIdx(index) == H5G_GROUP)
				dataType = _file->openGroup("X").openDataSet("data").getDataType();
			else
				return false;

			return (dataType.getClass() == H5T_FLOAT && dataType.getSize() > sizeof(float)) ||
				(dataType.getClass() == H5T_INTEGER && dataType.getSize() > sizeof(std::uint16_t));
		}
	}
	catch (const H5::Exception&)
	{
		return true;
	}

	return false;
}

 template<typename T>
 void filterValues(T& values, const std::vector<std::ptrdiff_t>& dimensionIndices)
 {
	if(dimensionIndices.empty())
		return;
	assert(values.size() == dimensionIndices.size());
	const auto nrOfSelectedIdices = std::count_if(dimensionIndices.cbegin(), dimensionIndices.cend(), [](std::ptrdiff_t i) { return (i >= 0); });
	T selectedValues(nrOfSelectedIdices);
	#pragma omp parallel for
	 for (std::ptrdiff_t i = 0; i < dimensionIndices.size(); ++i)
	 {
		 auto index = dimensionIndices[i];
		 if (index >= 0)
		 {
			 auto value = values[i];
			 assert(index < nrOfSelectedIdices);
			 selectedValues[index] = value;
		 }
		 
	 }
	 values = std::move(selectedValues);
 }

 
static void LoadProperties(H5::DataSet dataset, H5AD::LoaderInfo &datasetInfo)
 {
	 auto datasetClass = dataset.getDataType().getClass();
	 QVariantMap propertyMap;
	 if (datasetClass == H5T_COMPOUND)
	 {
		 
		 const auto nrOfOriginalDimensions = datasetInfo._originalDimensionNames.size();
		 std::map<std::string, std::vector<QVariant> > compoundMap;
		 if (H5Utils::read_compound(dataset, compoundMap))
		 {
			 for (auto component = compoundMap.begin(); component != compoundMap.end(); ++component)
			 {
				if(component->second.size())
				{
					filterValues(component->second, datasetInfo._selectedDimensionsLUT);
					propertyMap[component->first.c_str()] =  QVariantList(component->second.cbegin(), component->second.cend());
				}
			 }
		 }
	 }
	 datasetInfo._pointsDataset->setProperty(dataset.getObjName().c_str(), propertyMap);
	
 }

static void LoadProperties(H5::Group &group, H5AD::LoaderInfo &datasetInfo)
 {
	 QString name = group.getObjName().c_str();
	 if (name.isEmpty())
		 return;
	 if (name[0] == '/')
		 name.remove(0, 1);
	 QVariantMap propertyMap;

	 std::map<std::string, std::vector<QString>> categories;
	 H5AD::LoadCategories(group, categories);

	 const auto nrOfOriginalDimensions = datasetInfo._originalDimensionNames.size();
	 const auto nrOfObjects = group.getNumObjs();
	 for (auto go = 0; go < nrOfObjects; ++go)
	 {
		 std::string objectName1 = group.getObjnameByIdx(go);
		 H5G_obj_t objectType1 = group.getObjTypeByIdx(go);

		 if (objectType1 == H5G_DATASET)
		 {
			 H5::DataSet dataSet = group.openDataSet(objectName1);
			 auto datasetClass = dataSet.getDataType().getClass();

			 if (datasetClass == H5T_FLOAT)
			 {
				 std::vector<float> values;
				 if (H5Utils::read_vector(group, objectName1, &values))
				 {
					 if (values.size())
					 {
					 	filterValues(values, datasetInfo._selectedDimensionsLUT);
					 	propertyMap[objectName1.c_str()] = QVariantList(values.cbegin(), values.cend());
					 }
				 }
			 }
			 else if ((datasetClass == H5T_INTEGER) || (datasetClass == H5T_ENUM))
			 {
				 std::vector<qlonglong> values;
				 if (H5Utils::read_vector(group, objectName1, &values))
				 {
					 if (values.size())
					 {
						 QString label = objectName1.c_str();
						 QVariantList labels;
						 if (dataSet.attrExists("categories"))
						 {
							 if (categories.count(objectName1))
							 {
								 const std::vector<QString>& category_labels = categories[objectName1];
								 labels.resize(values.size());
								 for (qsizetype i = 0; i < labels.size(); ++i)
								 {
									 qsizetype index = values[i];
									 if (index < category_labels.size())
									 {
										 labels[i] = category_labels[index];
									 }
									 else
									 {
										 labels.clear();
										 break;
									 }
								 }
							 }
						 }
						 if (labels.isEmpty())
						 {
							 if (values.size())
							 {
								 filterValues(values, datasetInfo._selectedDimensionsLUT);
								 propertyMap[label] = QVariantList(values.cbegin(), values.cend());
								 if (datasetClass == H5T_ENUM)
								 {
									 auto enumType = dataSet.getEnumType();
									 labels.reserve(values.size());
									 for (std::size_t i = 0; i < values.size(); ++i)
									 {
										 int v = values[i];
										 labels.push_back(enumType.nameOf(&v, 100).c_str());
									 }
									 QString label = objectName1.c_str();
									 label += " (label)";
									 if (labels.size())
									 {
										 // it shouldn't be needed to filter the labels since they are based on the values which where already filtered.
										 propertyMap[label] = labels;
									 }
								 }
							 }
						 }
						 else
						 {
							 if (values.size())
							 {
								 filterValues(values, datasetInfo._selectedDimensionsLUT);
								 propertyMap[label] = labels;
							 }
						 }

					 }
				 }
			 }
			 else if (datasetClass == H5T_STRING)
			 {
				 // try to read as strings
				 std::vector<QString> values;
				 H5Utils::read_vector_string(dataSet, values);
				 if (values.size())
				 {
					 filterValues(values, datasetInfo._selectedDimensionsLUT);
					 propertyMap[objectName1.c_str()] = QVariantList(values.cbegin(), values.cend());
				 }
			 }
		 }
		 else if (objectType1 == H5G_GROUP)
		 {
			 H5::Group subgroup = group.openGroup(objectName1);
			 std::map<QString, std::vector<unsigned>> codedCategories;
			 
			 if (H5AD::LoadCodedCategories(subgroup, codedCategories))
			 {
				 std::size_t count = 0;
				 for (auto cat_it = codedCategories.cbegin(); cat_it != codedCategories.cend(); ++cat_it)
					 count += cat_it->second.size();
				 if (count != nrOfOriginalDimensions)
					 std::cout << "WARNING: " << "not all dimensions are accounted for" << std::endl;
//				 else
				 {
					 QVariantList values(count);
					 for (auto cat_it = codedCategories.cbegin(); cat_it != codedCategories.cend(); ++cat_it)
					 {
						 for (auto idx_it = cat_it->second.cbegin(); idx_it != cat_it->second.cend(); ++idx_it)
						 {

							 if (values[*idx_it].isNull())
							 {
								 values[*idx_it] = cat_it->first;
							 }
							 else
							 {
								 values.clear();
								 idx_it = cat_it->second.cend();
								 cat_it = codedCategories.cend();
								 break;
							 }
						 }

					 }
					 if (values.size())
					 {
						 filterValues(values, datasetInfo._selectedDimensionsLUT);
						 propertyMap[objectName1.c_str()] = values;
					 }
				 }
			 }
		 }
	 }
	 
	 datasetInfo._pointsDataset->setProperty(name, propertyMap);
	
 }

bool HDF5_AD_Loader::load(const H5ADImportOptions& options)
{
	
	if (_file == nullptr)
		return false;
	try
	{
		const auto [matrixRows, matrixColumns] = getMatrixShape();
		const auto sparseMatrixInfo = getSparseMatrixInfo();
		auto importBenchmark = std::shared_ptr<H5AD::ImportBenchmarkData>();
		if (!options.importBenchmarkFilePath.isEmpty())
		{
			importBenchmark = std::make_shared<H5AD::ImportBenchmarkData>();
			importBenchmark->outputFilePath = options.importBenchmarkFilePath;
			importBenchmark->startedUtc = QDateTime::currentDateTimeUtc();
			importBenchmark->startedAt = std::chrono::steady_clock::now();
		}
		auto nrOfObjects = _file->getNumObjs();
		std::map<std::string, std::size_t> objectNameIdx;
		for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
		{
			std::string objectName1 = _file->getObjnameByIdx(fo);
			objectNameIdx[objectName1] = fo;
		}

		std::set<std::string> objectsToProcess;
		for (const auto& name : options.optionalObjects)
			objectsToProcess.insert(name.toStdString());
		std::set<std::string> optionalMatricesToProcess;
		for (const auto& name : options.optionalMatrixObjects)
			optionalMatricesToProcess.insert(name.toStdString());
		if (!optionalMatricesToProcess.empty())
			objectsToProcess.insert("obsm");

		Dataset<Points> pointsDataset = H5Utils::createPointsDataset(_core, false, options.datasetName);
		pointsDataset->setProjectResourceStagingPolicy(options.projectOpenStagingPolicy);
		pointsDataset->getDataHierarchyItem().setVisible(false);
	
		const auto loaderInfo = std::make_shared<H5AD::LoaderInfo>();
		loaderInfo->_pointsDataset = pointsDataset;
		loaderInfo->_sourceFileName = _fileName;
		loaderInfo->_importBenchmark = importBenchmark;
		loaderInfo->_stagingDirectory = options.stagingDirectory.isEmpty() ? _stagingDirectory : options.stagingDirectory;
		loaderInfo->_storageBackendType = options.storageBackendType;
		loaderInfo->_storageType = options.storageType;
		loaderInfo->_tiledStorageConfiguration = options.tiledStorageConfiguration;
		loaderInfo->_sparseStorageConfiguration = options.sparseStorageConfiguration;
		loaderInfo->_bucketingThreadCount = std::max<std::size_t>(1, options.bucketingThreadCount);
		loaderInfo->_stagingThreadCount = std::max<std::size_t>(1, options.stagingThreadCount);
		loaderInfo->_rowChunkTileFactor = std::clamp<std::size_t>(options.rowChunkTileFactor, 1, 256);
		loaderInfo->_zstdCompressionLevel = std::clamp(options.zstdCompressionLevel, 1, 22);
		loaderInfo->_metadataStagingDirectory = loaderInfo->_stagingDirectory;
		loaderInfo->_metadataStorageType = options.metadataStorageType;
		loaderInfo->_availableClusterSets = options.availableClusterSets;
		loaderInfo->_selectedClusterSets = options.selectedClusterSets;
		loaderInfo->_originalDimensionNames = _dimensionNames;
		loaderInfo->_selectedDimensionIndices = options.selectedDimensionIndices;
		if (!options.selectedDimensionIndices.empty() && options.selectedDimensionIndices.size() < _dimensionNames.size())
		{
			loaderInfo->_selectedDimensionsLUT.assign(_dimensionNames.size(), -1);
			for (std::size_t destination = 0; destination < options.selectedDimensionIndices.size(); ++destination)
			{
				const auto source = options.selectedDimensionIndices[destination];
				if (source < 0 || static_cast<std::size_t>(source) >= _dimensionNames.size())
					throw std::out_of_range("Selected H5AD dimension index is out of range");
				loaderInfo->_selectedDimensionsLUT[static_cast<std::size_t>(source)] = static_cast<std::ptrdiff_t>(destination);
			}
		}
		loaderInfo->_sampleNames = QVariantList(_sampleNames.cbegin(), _sampleNames.cend());
		const auto loader = shared_from_this();
		auto workflow = std::make_unique<mv::workflow::WorkflowPlan>(QObject::tr("Load H5AD point data"));
		const auto hasOptionalMetadata = !objectsToProcess.empty();
		const auto pointDataStageWeight = hasOptionalMetadata ? 80.0 : 99.0;
		const auto metadataStageWeight = hasOptionalMetadata ? 19.0 : 0.0;

		workflow->addSequentialStage(QObject::tr("Stage HDF5 point data"), [loader, loaderInfo, storageType = options.storageType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
			const auto stageStart = std::chrono::steady_clock::now();
			const H5AD::ProgressCallback progress = [executionContext](const float value, const QString&) {
				if (const auto task = executionContext->getTask(); task && (task->isAboutToBeAborted() || task->isAborting() || task->isAborted()))
					throw std::runtime_error("H5AD point-data staging canceled");

				executionContext->setProgress(value);
			};

			try
			{
				if (!H5AD::load_X(loader->_file, *loaderInfo, storageType, progress))
					throw std::runtime_error("Unable to stage H5AD point data");
			}
			catch (...)
			{
				if (loaderInfo->_importBenchmark)
					loaderInfo->_importBenchmark->pointDataStageMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count());
				throw;
			}
			if (loaderInfo->_importBenchmark)
				loaderInfo->_importBenchmark->pointDataStageMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count());
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::CurrentWorkerThread, pointDataStageWeight);

		workflow->addNestedWorkflowStage(QObject::tr("Create point-data metadata"), [loader, loaderInfo, objectsToProcess, optionalMatricesToProcess, objectNameIdx, options](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
			auto metadataWorkflow = std::make_unique<mv::workflow::WorkflowPlan>(QObject::tr("Load optional H5AD metadata"));
			mv::workflow::WorkflowPlan::Jobs metadataJobs;

			const auto addMetadataJob = [&metadataJobs, benchmark = loaderInfo->_importBenchmark](const QString& name, mv::workflow::WorkflowPlan::JobFunction function) {
				metadataJobs.emplace_back(name, [benchmark, name, function = std::move(function)](const mv::workflow::WorkflowPlan::Job& job, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
					const auto stageStart = std::chrono::steady_clock::now();
					try
					{
						function(job, executionContext);
					}
					catch (...)
					{
						if (benchmark)
						{
							const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count());
							benchmark->metadataStageMilliseconds += elapsed;
							benchmark->metadataJobMilliseconds.insert(name, elapsed);
						}
						throw;
					}
					if (benchmark)
					{
						const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count());
						benchmark->metadataStageMilliseconds += elapsed;
						benchmark->metadataJobMilliseconds.insert(name, elapsed);
					}
				}, mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread);
			};

			const auto nrOfObjects = loader->_file->getNumObjs();
			for (hsize_t fo = 0; fo < nrOfObjects; ++fo)
			{
				const auto objectName = loader->_file->getObjnameByIdx(fo);
				if (objectsToProcess.count(objectName) != 1)
					continue;

				const auto objectType = loader->_file->getObjTypeByIdx(fo);
				const auto jobName = QObject::tr("Load sample names and metadata: %1").arg(QString::fromLocal8Bit(objectName.c_str()));
				addMetadataJob(jobName, [loader, loaderInfo, objectName, objectType, optionalMatricesToProcess, options](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext& executionContext) {
					const H5AD::MetadataProgressCallback progress = [executionContext](const float value, const QString&) {
						executionContext->setProgress(value);
					};

					const auto previousClusterSetFilter = loaderInfo->_filterClusterSets;
					if (objectName == "obs")
						loaderInfo->_filterClusterSets = options.filterClusterSets;

					try
					{
						if (objectType == H5G_DATASET)
						{
							auto h5Dataset = loader->_file->openDataSet(objectName);
							H5AD::LoadSampleNamesAndMetaDataFloat(h5Dataset, *loaderInfo, progress);
						}
						else if (objectType == H5G_GROUP)
						{
							auto h5Group = loader->_file->openGroup(objectName);
							if (objectName == "obsm")
							{
								for (const auto& matrixName : optionalMatricesToProcess)
								{
									if (h5Group.nameExists(matrixName.c_str()))
									{
										auto matrix = h5Group.openDataSet(matrixName.c_str());
										const auto matrixKey = QString::fromLocal8Bit(matrixName.c_str());
										loaderInfo->_metadataStorageBackendType = options.optionalMatrixStorageTypes.value(matrixKey, pointdata::storage::StorageBackendType::Dense);
										loaderInfo->_metadataTiledStorageConfiguration = options.optionalMatrixTiledConfigurations.value(matrixKey);
										const auto previousMetadataStorageType = loaderInfo->_metadataStorageType;
										loaderInfo->_metadataStorageType = options.optionalMatrixElementTypes.value(matrixKey, options.metadataStorageType);
										try
										{
											H5AD::LoadSampleNamesAndMetaDataFloat(matrix, *loaderInfo, progress);
										}
										catch (...)
										{
											loaderInfo->_metadataStorageType = previousMetadataStorageType;
											throw;
										}
										loaderInfo->_metadataStorageType = previousMetadataStorageType;
									}
								}
							}
							else
								H5AD::LoadSampleNamesAndMetaDataFloat(h5Group, *loaderInfo, progress);
						}
					}
					catch (const std::exception& exception)
					{
						loaderInfo->_filterClusterSets = previousClusterSetFilter;
						std::cerr << "H5AD Loader: Optional metadata loading failed for " << objectName << ": " << exception.what() << std::endl;
					}
					catch (...)
					{
						loaderInfo->_filterClusterSets = previousClusterSetFilter;
						throw;
					}
					loaderInfo->_filterClusterSets = previousClusterSetFilter;
				});
			}

			const auto varIndex = objectNameIdx.find("var");
			if (varIndex != objectNameIdx.end())
			{
				const auto objectType = loader->_file->getObjTypeByIdx(varIndex->second);
				addMetadataJob(QObject::tr("Load properties: var"), [loader, loaderInfo, objectType](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
					try
					{
						if (objectType == H5G_DATASET)
						{
							auto h5Dataset = loader->_file->openDataSet("var");
							LoadProperties(h5Dataset, *loaderInfo);
						}
						else if (objectType == H5G_GROUP)
						{
							auto h5Group = loader->_file->openGroup("var");
							LoadProperties(h5Group, *loaderInfo);
						}
					}
					catch (const std::exception& exception)
					{
						std::cerr << "H5AD Loader: Optional property loading failed for var: " << exception.what() << std::endl;
					}
				});
			}

			if (metadataJobs.empty())
				addMetadataJob(QObject::tr("No optional metadata selected"), [](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {});

			metadataWorkflow->addSequentialStage(QObject::tr("Load discovered metadata"), std::move(metadataJobs), 1.0);
			return metadataWorkflow;
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread, metadataStageWeight);

		workflow->addSequentialStage(QObject::tr("Publish point-data metadata"), [loaderInfo, stagingPolicy = options.projectOpenStagingPolicy](const mv::workflow::WorkflowPlan::Job&, const mv::workflow::SharedWorkflowExecutionContext&) {
			const auto stageStart = std::chrono::steady_clock::now();
			loaderInfo->_pointsDataset->setProjectResourceStagingPolicy(stagingPolicy);
			events().notifyDatasetDataChanged(loaderInfo->_pointsDataset);
			loaderInfo->_pointsDataset->getDataHierarchyItem().setVisible(true);
			if (loaderInfo->_importBenchmark)
				loaderInfo->_importBenchmark->publishStageMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count());
		}, mv::workflow::WorkflowPlan::JobThreadAffinity::GuiThread, 1);

		mv::workflow::WorkflowOptions workflowOptions;
		workflowOptions.cancellation.enabled = true;
		workflowOptions.reporting.progress = true;
		workflowOptions.reporting.finishedNotification = true;
		workflowOptions.profiling.sinkType = mv::workflow::WorkflowProfilingOptions::SinkType::ChromeTracing;
		auto future = mv::Application::getWorkflowPlanExecutor().execute(std::move(workflow), nullptr, workflowOptions);
		future.onFinished(_workflowReceiver, [pointsDataset, importBenchmark, importOptions = options, sourceFilePath = _fileName, matrixRows, matrixColumns, sparseInput = sparseMatrixInfo.available](const mv::workflow::SharedWorkflowResult& result) {
			const auto succeeded = result && result->getStatus() == mv::workflow::WorkflowResultBase::Status::Success;
			if (!succeeded)
				mv::data().removeDataset(pointsDataset);
			if (importBenchmark)
				writeImportBenchmark(*importBenchmark, importOptions, sourceFilePath, matrixRows, matrixColumns, sparseInput, succeeded);
		});

		return true;
	}
	catch (std::exception &e)
	{
		std::cout << "H5AD Loader: " << e.what() << std::endl;
		return false;
	}

	return false;
}

