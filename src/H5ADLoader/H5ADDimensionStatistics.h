#pragma once

#include <cstdint>

/** @brief Per-column H5AD X statistics. Estimates are marked with exact=false. */
struct H5ADDimensionStatistics
{
	double mean = 0.0;
	double standardDeviation = 0.0;
	double zeroFraction = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	std::uint64_t nanCount = 0;
	std::uint64_t finiteValueCount = 0;
	std::uint64_t analyzedRowCount = 0;
	bool available = false;
	bool exact = false;
};
