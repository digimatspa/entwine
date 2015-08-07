/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/types/structure.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

#include <entwine/tree/climber.hpp>

namespace entwine
{

namespace
{
    std::size_t log2(std::size_t val)
    {
        return std::log2(val);
    }
}

ChunkInfo::ChunkInfo(const Structure& structure, const Id& index)
    : m_structure(structure)
    , m_index(index)
    , m_chunkId(0)
    , m_depth(calcDepth(m_structure.factor(), index))
    , m_chunkOffset(0)
    , m_chunkPoints(0)
    , m_chunkNum(0)
{
    const Id levelIndex(calcLevelIndex(m_structure.dimensions(), m_depth));
    const std::size_t baseChunkPoints(m_structure.baseChunkPoints());

    const Id& sparseIndexBegin(m_structure.sparseIndexBegin());
    const Id& coldIndexBegin(m_structure.coldIndexBegin());

    if (!m_structure.dynamicChunks() || levelIndex <= sparseIndexBegin)
    {
        m_chunkPoints = baseChunkPoints;
        const auto divMod((m_index - coldIndexBegin).divMod(m_chunkPoints));
        m_chunkNum = divMod.first.getSimple();
        m_chunkOffset = divMod.second.getSimple();
        m_chunkId = coldIndexBegin + m_chunkNum * m_chunkPoints;
    }
    else
    {
        const std::size_t sparseFirstSpan(
                pointsAtDepth(
                    m_structure.dimensions(),
                    m_structure.sparseDepthBegin()).getSimple());

        const std::size_t chunksPerSparseDepth(
            sparseFirstSpan / baseChunkPoints);

        const std::size_t sparseDepthCount(
                m_depth - m_structure.sparseDepthBegin());

        m_chunkPoints =
            (Id(baseChunkPoints) *
            binaryPow(m_structure.dimensions(), sparseDepthCount)).getSimple();

        const Id coldIndexSpan(sparseIndexBegin - coldIndexBegin);
        const Id numColdChunks(coldIndexSpan / baseChunkPoints);

        const Id prevLevelsChunkCount(
                numColdChunks +
                chunksPerSparseDepth * sparseDepthCount);

        const std::size_t levelOffset((index - levelIndex).getSimple());

        m_chunkNum =
            (prevLevelsChunkCount + levelOffset / m_chunkPoints).getSimple();
        m_chunkOffset = levelOffset % m_chunkPoints;
        m_chunkId = levelIndex + (levelOffset / m_chunkPoints) * m_chunkPoints;
    }
}

std::size_t ChunkInfo::calcDepth(
        const std::size_t factor,
        const Id& index)
{
    return log2(index * (factor - 1) + 1) / log2(factor);
}

Id ChunkInfo::calcLevelIndex(
        const std::size_t dimensions,
        const std::size_t depth)
{
    return (binaryPow(dimensions, depth) - 1) / ((1ULL << dimensions) - 1);
}

Id ChunkInfo::pointsAtDepth(
        const std::size_t dimensions,
        const std::size_t depth)
{
    return binaryPow(dimensions, depth);
}

Id ChunkInfo::binaryPow(
        const std::size_t baseLog2,
        const std::size_t exp)
{
    return Id(1) << (exp * baseLog2);
}

std::size_t ChunkInfo::logN(std::size_t val, std::size_t n)
{
    if (n != 4 && n != 8)
    {
        throw std::runtime_error("Invalid logN arg: " + std::to_string(n));
    }

    return log2(val) / log2(n);
}

std::size_t ChunkInfo::isPerfectLogN(std::size_t val, std::size_t n)
{
    return (1ULL << logN(val, n) * log2(n)) == val;
}

Structure::Structure(
        const std::size_t nullDepth,
        const std::size_t baseDepth,
        const std::size_t coldDepth,
        const std::size_t chunkPoints,
        const std::size_t dimensions,
        const std::size_t numPointsHint,
        const bool dynamicChunks,
        const std::pair<std::size_t, std::size_t> subset)
    : m_nullDepthBegin(0)
    , m_nullDepthEnd(nullDepth)
    , m_baseDepthBegin(m_nullDepthEnd)
    , m_baseDepthEnd(std::max(m_baseDepthBegin, baseDepth))
    , m_coldDepthBegin(m_baseDepthEnd)
    , m_coldDepthEnd(std::max(m_coldDepthBegin, coldDepth))
    , m_sparseDepthBegin(0)
    , m_sparseIndexBegin(0)
    , m_chunkPoints(chunkPoints)
    , m_dynamicChunks(dynamicChunks)
    , m_dimensions(dimensions)
    , m_factor(1ULL << m_dimensions)
    , m_numPointsHint(numPointsHint)
    , m_subset(subset)
{
    loadIndexValues();
}

Structure::Structure(
        const std::size_t nullDepth,
        const std::size_t baseDepth,
        const std::size_t chunkPoints,
        const std::size_t dimensions,
        const std::size_t numPointsHint,
        const bool dynamicChunks,
        const std::pair<std::size_t, std::size_t> subset)
    : m_nullDepthBegin(0)
    , m_nullDepthEnd(nullDepth)
    , m_baseDepthBegin(m_nullDepthEnd)
    , m_baseDepthEnd(std::max(m_baseDepthBegin, baseDepth))
    , m_coldDepthBegin(m_baseDepthEnd)
    , m_coldDepthEnd(0)
    , m_sparseDepthBegin(0)
    , m_sparseIndexBegin(0)
    , m_chunkPoints(chunkPoints)
    , m_dynamicChunks(dynamicChunks)
    , m_dimensions(dimensions)
    , m_factor(1ULL << m_dimensions)
    , m_numPointsHint(numPointsHint)
    , m_subset(subset)
{
    loadIndexValues();
}

Structure::Structure(const Json::Value& json)
    : m_nullDepthBegin(0)
    , m_nullDepthEnd(json["nullDepth"].asUInt64())
    , m_baseDepthBegin(m_nullDepthEnd)
    , m_baseDepthEnd(json["baseDepth"].asUInt64())
    , m_coldDepthBegin(m_baseDepthEnd)
    , m_coldDepthEnd(json["coldDepth"].asUInt64())
    , m_sparseDepthBegin(0)
    , m_sparseIndexBegin(0)
    , m_chunkPoints(json["chunkPoints"].asUInt64())
    , m_dynamicChunks(json["dynamicChunks"].asBool())
    , m_dimensions(json["dimensions"].asUInt64())
    , m_factor(1ULL << m_dimensions)
    , m_numPointsHint(json["numPointsHint"].asUInt64())
    , m_subset({ json["subset"][0].asUInt64(), json["subset"][1].asUInt64() })
{
    loadIndexValues();
}

void Structure::loadIndexValues()
{
    const std::size_t coldFirstSpan(
            ChunkInfo::pointsAtDepth(
                m_dimensions,
                m_coldDepthBegin).getSimple());

    if (m_baseDepthEnd < 4)
    {
        throw std::runtime_error("Base depth too small");
    }

    if (!m_chunkPoints && hasCold())
    {
        // TODO Assign a default?
        throw std::runtime_error(
                "Points per chunk not specified, but a cold depth was given.");
    }

    if (hasCold() && !ChunkInfo::isPerfectLogN(m_chunkPoints, m_dimensions))
    {
        throw std::runtime_error(
                "Invalid chunk specification - "
                "must be of the form 4^n for quadtree, or 8^n for octree");
    }

    m_nominalChunkDepth = ChunkInfo::logN(m_chunkPoints, m_factor);
    m_nominalChunkIndex =
        ChunkInfo::calcLevelIndex(
                m_dimensions,
                m_nominalChunkDepth).getSimple();

    m_nullIndexBegin = 0;
    m_nullIndexEnd = ChunkInfo::calcLevelIndex(m_dimensions, m_nullDepthEnd);
    m_baseIndexBegin = m_nullIndexEnd;
    m_baseIndexEnd = ChunkInfo::calcLevelIndex(m_dimensions, m_baseDepthEnd);
    m_coldIndexBegin = m_baseIndexEnd;
    m_coldIndexEnd =
        m_coldDepthEnd ?
            ChunkInfo::calcLevelIndex(m_dimensions, m_coldDepthEnd) : 0;

    if (m_numPointsHint)
    {
        m_sparseDepthBegin =
            std::max(
                    log2(m_numPointsHint) / log2(m_factor) + 1,
                    m_coldDepthBegin);

        m_sparseIndexBegin =
            ChunkInfo::calcLevelIndex(
                    m_dimensions,
                    m_sparseDepthBegin);
    }
    else
    {
        std::cout <<
            "No numPointsHint provided.  " <<
            "For more than a few billion points, " <<
            "there may be a large performance hit." << std::endl;
    }

    const std::size_t splits(m_subset.second);
    if (splits)
    {
        if (!m_nullDepthEnd || std::pow(4, m_nullDepthEnd) < splits)
        {
            throw std::runtime_error("Invalid null depth for requested subset");
        }

        if (!(splits == 4 || splits == 16 || splits == 64))
        {
            throw std::runtime_error("Invalid subset split");
        }

        if (m_subset.first >= m_subset.second)
        {
            throw std::runtime_error("Invalid subset identifier");
        }

        if (hasCold())
        {
            if (
                    (coldFirstSpan / m_chunkPoints) < splits ||
                    (coldFirstSpan / m_chunkPoints) % splits)
            {
                throw std::runtime_error("Invalid chunk size for this subset");
            }
        }
    }
}

Json::Value Structure::toJson() const
{
    Json::Value json;

    json["nullDepth"] = static_cast<Json::UInt64>(nullDepthEnd());
    json["baseDepth"] = static_cast<Json::UInt64>(baseDepthEnd());
    json["coldDepth"] = static_cast<Json::UInt64>(coldDepthEnd());
    json["chunkPoints"] = static_cast<Json::UInt64>(baseChunkPoints());
    json["dimensions"] = static_cast<Json::UInt64>(dimensions());
    json["numPointsHint"] = static_cast<Json::UInt64>(numPointsHint());
    json["dynamicChunks"] = m_dynamicChunks;
    json["subset"].append(static_cast<Json::UInt64>(m_subset.first));
    json["subset"].append(static_cast<Json::UInt64>(m_subset.second));

    return json;
}

ChunkInfo Structure::getInfoFromNum(const std::size_t chunkNum) const
{
    Id chunkId(0);

    if (hasCold())
    {
        if (hasSparse() && dynamicChunks())
        {
            const Id endFixed(
                    ChunkInfo::calcLevelIndex(
                        m_dimensions,
                        m_sparseDepthBegin + 1));

            const Id fixedSpan(endFixed - m_coldIndexBegin);
            const Id fixedNum(fixedSpan / m_chunkPoints);

            if (chunkNum < fixedNum)
            {
                chunkId = m_coldIndexBegin + chunkNum * m_chunkPoints;
            }
            else
            {
                const Id leftover(chunkNum - fixedNum);

                const std::size_t chunksPerSparseDepth(
                        numChunksAtDepth(m_sparseDepthBegin));

                const std::size_t depth(
                        (m_sparseDepthBegin + 1 +
                            leftover / chunksPerSparseDepth).getSimple());

                const std::size_t chunkNumInDepth(
                        (leftover % chunksPerSparseDepth).getSimple());

                const std::size_t depthIndexBegin(
                        ChunkInfo::calcLevelIndex(
                            m_dimensions,
                            depth).getSimple());

                const Id depthChunkSize(
                        ChunkInfo::pointsAtDepth(m_dimensions, depth) /
                        chunksPerSparseDepth);

                chunkId = depthIndexBegin + chunkNumInDepth * depthChunkSize;
            }
        }
        else
        {
            chunkId = m_coldIndexBegin + chunkNum * m_chunkPoints;
        }
    }

    return ChunkInfo(*this, chunkId);
}

std::size_t Structure::numChunksAtDepth(const std::size_t depth) const
{
    std::size_t num(0);

    if (!hasSparse() || !dynamicChunks() || depth <= m_sparseDepthBegin)
    {
        const Id depthSpan(
                ChunkInfo::calcLevelIndex(m_dimensions, depth + 1) -
                ChunkInfo::calcLevelIndex(m_dimensions, depth));

        num = (depthSpan / m_chunkPoints).getSimple();
    }
    else
    {
        const Id sparseFirstSpan(
                ChunkInfo::pointsAtDepth(m_dimensions, m_sparseDepthBegin));

        num = (sparseFirstSpan / m_chunkPoints).getSimple();
    }

    return num;
}

bool Structure::isSubset() const
{
    return m_subset.second != 0;
}

std::pair<std::size_t, std::size_t> Structure::subset() const
{
    return m_subset;
}

void Structure::makeWhole()
{
    m_subset = { 0, 0 };
}

std::unique_ptr<BBox> Structure::subsetBBox(const BBox& full) const
{
    std::unique_ptr<BBox> result;

    Climber climber(full, *this);
    std::size_t times(0);

    // TODO
    if (is3d()) throw std::runtime_error("Can't currently split octree");

    // TODO Very temporary.
    if (m_subset.second == 4) times = 1;
    else if (m_subset.second == 16) times = 2;
    else if (m_subset.second == 64) times = 3;
    else throw std::runtime_error("Invalid subset split");

    if (times)
    {
        for (std::size_t i(0); i < times; ++i)
        {
            Climber::Dir dir(
                    static_cast<Climber::Dir>(
                        m_subset.first >> (i * 2) & 0x03));

            if (dir == Climber::Dir::nwd) climber.goNwd();
            else if (dir == Climber::Dir::ned) climber.goNed();
            else if (dir == Climber::Dir::swd) climber.goSwd();
            else climber.goSed();
        }

        result.reset(new BBox(climber.bbox()));
    }
    else
    {
        throw std::runtime_error("Invalid magnification subset");
    }

    return result;
}

std::string Structure::subsetPostfix() const
{
    std::string postfix("");

    if (isSubset()) postfix += "-" + std::to_string(m_subset.first);

    return postfix;
}

} // namespace entwine

