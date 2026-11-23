#pragma once

#include <QString>

#include <cstdint>

/** Describes one importable categorical observation field. */
struct H5ADClusterSetInfo
{
	bool clusterCountAvailable = false; /**< Whether the source encoding exposes its category count without reading observations. */
	std::uint64_t clusterCount = 0; /**< Number of encoded categories, when available. */
	QString description; /**< Brief source-format detail shown as a tooltip. */
};
