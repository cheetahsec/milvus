// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include "knowhere/index/vector_index/ConfAdapter.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "knowhere/common/Log.h"
#include "knowhere/index/vector_index/helpers/IndexParameter.h"

#ifdef MILVUS_GPU_VERSION
#include "faiss/gpu/utils/DeviceUtils.h"
#endif

namespace milvus {
namespace knowhere {

#define DEFAULT_MAX_DIM 32768
#define DEFAULT_MIN_DIM 1
#define DEFAULT_MAX_K 16384
#define DEFAULT_MIN_K 1
#define DEFAULT_MIN_ROWS 1  // minimum size for build index
#ifdef MILVUS_FPGA_VERSION
#define DEFAULT_MAX_ROWS 300000000
#else
#define DEFAULT_MAX_ROWS 50000000
#endif
#define CheckIntByRange(key, min, max)                                                                   \
    if (!oricfg.contains(key) || !oricfg[key].is_number_integer() || oricfg[key].get<int64_t>() > max || \
        oricfg[key].get<int64_t>() < min) {                                                              \
        return false;                                                                                    \
    }

#define CheckIntByRangeIfExist(key, min, max)                                       \
    if (oricfg.contains(key)) {                                                     \
        if (!oricfg[key].is_number_integer() || oricfg[key].get<int64_t>() > max || \
            oricfg[key].get<int64_t>() < min) {                                     \
            return false;                                                           \
        }                                                                           \
    }

#define CheckIntByValues(key, container)                                                                 \
    if (!oricfg.contains(key) || !oricfg[key].is_number_integer()) {                                     \
        return false;                                                                                    \
    } else {                                                                                             \
        auto finder = std::find(std::begin(container), std::end(container), oricfg[key].get<int64_t>()); \
        if (finder == std::end(container)) {                                                             \
            return false;                                                                                \
        }                                                                                                \
    }

#define CheckStrByValues(key, container)                                                                     \
    if (!oricfg.contains(key) || !oricfg[key].is_string()) {                                                 \
        return false;                                                                                        \
    } else {                                                                                                 \
        auto finder = std::find(std::begin(container), std::end(container), oricfg[key].get<std::string>()); \
        if (finder == std::end(container)) {                                                                 \
            return false;                                                                                    \
        }                                                                                                    \
    }

bool
ConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static std::vector<std::string> METRICS{Metric::L2, Metric::IP};
    CheckIntByRange(meta::DIM, DEFAULT_MIN_DIM, DEFAULT_MAX_DIM);
    CheckStrByValues(Metric::TYPE, METRICS);
    return true;
}

bool
ConfAdapter::CheckSearch(Config& oricfg, const IndexType type, const IndexMode mode) {
    CheckIntByRange(meta::TOPK, DEFAULT_MIN_K - 1, DEFAULT_MAX_K);
    return true;
}

int64_t
MatchNlist(int64_t size, int64_t nlist) {
    const int64_t MIN_POINTS_PER_CENTROID = 40;

    if (nlist * MIN_POINTS_PER_CENTROID > size) {
        // nlist is too large, adjust to a proper value
        nlist = std::max(1L, size / MIN_POINTS_PER_CENTROID);
        LOG_KNOWHERE_WARNING_ << "Row num " << size << " match nlist " << nlist;
    }
    return nlist;
}

int64_t
MatchNbits(int64_t size, int64_t nbits) {
    if (size < (1 << nbits)) {
        // nbits is too large, adjust to a proper value
        if (size >= (1 << 8)) {
            nbits = 8;
        } else if (size >= (1 << 4)) {
            nbits = 4;
        } else if (size >= (1 << 2)) {
            nbits = 2;
        } else {
            nbits = 1;
        }
        LOG_KNOWHERE_WARNING_ << "Row num " << size << " match nbits " << nbits;
    }
    return nbits;
}

bool
IVFConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t MAX_NLIST = 65536;
    static int64_t MIN_NLIST = 1;

    CheckIntByRange(IndexParams::nlist, MIN_NLIST, MAX_NLIST);
    CheckIntByRange(meta::ROWS, DEFAULT_MIN_ROWS, DEFAULT_MAX_ROWS);

    // int64_t nlist = oricfg[IndexParams::nlist];
    // CheckIntByRange(meta::ROWS, nlist, DEFAULT_MAX_ROWS);

    // auto tune params
    int64_t nq = oricfg[meta::ROWS].get<int64_t>();
    int64_t nlist = oricfg[IndexParams::nlist].get<int64_t>();
    oricfg[IndexParams::nlist] = MatchNlist(nq, nlist);

    // Best Practice
    // static int64_t MIN_POINTS_PER_CENTROID = 40;
    // static int64_t MAX_POINTS_PER_CENTROID = 256;
    // CheckIntByRange(meta::ROWS, MIN_POINTS_PER_CENTROID * nlist, MAX_POINTS_PER_CENTROID * nlist);

    return ConfAdapter::CheckTrain(oricfg, mode);
}

bool
IVFConfAdapter::CheckSearch(Config& oricfg, const IndexType type, const IndexMode mode) {
    static int64_t MIN_NPROBE = 1;
    static int64_t MAX_NPROBE = 65536;  // todo(linxj): [1, nlist]

    if (mode == IndexMode::MODE_GPU) {
#ifdef MILVUS_GPU_VERSION
        CheckIntByRange(IndexParams::nprobe, MIN_NPROBE, faiss::gpu::getMaxKSelection());
#endif
    } else {
        CheckIntByRange(IndexParams::nprobe, MIN_NPROBE, MAX_NPROBE);
    }

    return ConfAdapter::CheckSearch(oricfg, type, mode);
}

bool
IVFSQConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t DEFAULT_NBITS = 8;
    oricfg[IndexParams::nbits] = DEFAULT_NBITS;

    return IVFConfAdapter::CheckTrain(oricfg, mode);
}

bool
IVFPQConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t DEFAULT_NBITS = 8;
    static int64_t MAX_NLIST = 65536;
    static int64_t MIN_NLIST = 1;
    static int64_t MAX_NBITS = 16;
    static int64_t MIN_NBITS = 1;
    static std::vector<std::string> METRICS{Metric::L2, Metric::IP};

    CheckStrByValues(Metric::TYPE, METRICS);
    CheckIntByRange(meta::DIM, DEFAULT_MIN_DIM, DEFAULT_MAX_DIM);
    CheckIntByRange(meta::ROWS, DEFAULT_MIN_ROWS, DEFAULT_MAX_ROWS);
    CheckIntByRange(IndexParams::nlist, MIN_NLIST, MAX_NLIST);
    CheckIntByRangeIfExist(IndexParams::nbits, MIN_NBITS, MAX_NBITS);

    auto rows = oricfg[meta::ROWS].get<int64_t>();
    auto nlist = oricfg[IndexParams::nlist].get<int64_t>();
    auto dimension = oricfg[meta::DIM].get<int64_t>();
    auto m = oricfg[IndexParams::m].get<int64_t>();
    auto nbits = oricfg.count(IndexParams::nbits) ? oricfg[IndexParams::nbits].get<int64_t>() : DEFAULT_NBITS;

    // auto tune params
    oricfg[IndexParams::nlist] = MatchNlist(rows, nlist);
    oricfg[IndexParams::nbits] = nbits = MatchNbits(rows, nbits);

#ifdef MILVUS_GPU_VERSION
    if (mode == IndexMode::MODE_GPU) {
        if (IsValidForGPU(dimension, m, nbits)) {
            return true;
        }
        // else try CPU Mode
        mode == IndexMode::MODE_CPU;
    }
#endif
    return IsValidForCPU(dimension, m);
}

bool
IVFPQConfAdapter::IsValidForGPU(int64_t dimension, int64_t m, int64_t nbits) {
    /*
     * Faiss 1.6
     * Only 1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 28, 32 dims per sub-quantizer are currently supported with
     * no precomputed codes. Precomputed codes supports any number of dimensions, but will involve memory overheads.
     */
    static std::vector<int64_t> support_dim_per_subquantizer{32, 28, 24, 20, 16, 12, 10, 8, 6, 4, 3, 2, 1};
    static std::vector<int64_t> support_subquantizer{96, 64, 56, 48, 40, 32, 28, 24, 20, 16, 12, 8, 4, 3, 2, 1};

    if (!IsValidForCPU(dimension, m)) {
        return false;
    }

    int64_t sub_dim = dimension / m;
    return (std::find(std::begin(support_subquantizer), std::end(support_subquantizer), m) !=
            support_subquantizer.end()) &&
           (std::find(std::begin(support_dim_per_subquantizer), std::end(support_dim_per_subquantizer), sub_dim) !=
            support_dim_per_subquantizer.end());
}

bool
IVFPQConfAdapter::IsValidForCPU(int64_t dimension, int64_t m) {
    return (dimension % m == 0);
}

bool
NSGConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t MIN_KNNG = 5;
    static int64_t MAX_KNNG = 300;
    static int64_t MIN_SEARCH_LENGTH = 10;
    static int64_t MAX_SEARCH_LENGTH = 300;
    static int64_t MIN_OUT_DEGREE = 5;
    static int64_t MAX_OUT_DEGREE = 300;
    static int64_t MIN_CANDIDATE_POOL_SIZE = 50;
    static int64_t MAX_CANDIDATE_POOL_SIZE = 1000;
    static std::vector<std::string> METRICS{Metric::L2, Metric::IP};

    CheckStrByValues(Metric::TYPE, METRICS);
    CheckIntByRange(meta::ROWS, DEFAULT_MIN_ROWS, DEFAULT_MAX_ROWS);
    CheckIntByRange(IndexParams::knng, MIN_KNNG, MAX_KNNG);
    CheckIntByRange(IndexParams::search_length, MIN_SEARCH_LENGTH, MAX_SEARCH_LENGTH);
    CheckIntByRange(IndexParams::out_degree, MIN_OUT_DEGREE, MAX_OUT_DEGREE);
    CheckIntByRange(IndexParams::candidate, MIN_CANDIDATE_POOL_SIZE, MAX_CANDIDATE_POOL_SIZE);

    // auto tune params
    oricfg[IndexParams::nlist] = MatchNlist(oricfg[meta::ROWS].get<int64_t>(), 8192);

    int64_t nprobe = int(oricfg[IndexParams::nlist].get<int64_t>() * 0.1);
    oricfg[IndexParams::nprobe] = nprobe < 1 ? 1 : nprobe;

    return true;
}

bool
NSGConfAdapter::CheckSearch(Config& oricfg, const IndexType type, const IndexMode mode) {
    static int64_t MIN_SEARCH_LENGTH = 1;
    static int64_t MAX_SEARCH_LENGTH = 300;

    CheckIntByRange(IndexParams::search_length, MIN_SEARCH_LENGTH, MAX_SEARCH_LENGTH);

    return ConfAdapter::CheckSearch(oricfg, type, mode);
}

bool
HNSWConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t MIN_EFCONSTRUCTION = 8;
    static int64_t MAX_EFCONSTRUCTION = 512;
    static int64_t MIN_M = 4;
    static int64_t MAX_M = 64;

    CheckIntByRange(meta::ROWS, DEFAULT_MIN_ROWS, DEFAULT_MAX_ROWS);
    CheckIntByRange(IndexParams::efConstruction, MIN_EFCONSTRUCTION, MAX_EFCONSTRUCTION);
    CheckIntByRange(IndexParams::M, MIN_M, MAX_M);

    return ConfAdapter::CheckTrain(oricfg, mode);
}

bool
HNSWConfAdapter::CheckSearch(Config& oricfg, const IndexType type, const IndexMode mode) {
    static int64_t MAX_EF = 32768;

    CheckIntByRange(IndexParams::ef, oricfg[meta::TOPK], MAX_EF);

    return ConfAdapter::CheckSearch(oricfg, type, mode);
}

bool
BinIDMAPConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static std::vector<std::string> METRICS{Metric::HAMMING, Metric::JACCARD, Metric::TANIMOTO, Metric::SUBSTRUCTURE,
                                            Metric::SUPERSTRUCTURE};

    CheckIntByRange(meta::DIM, DEFAULT_MIN_DIM, DEFAULT_MAX_DIM);
    CheckStrByValues(Metric::TYPE, METRICS);

    return true;
}

bool
BinIVFConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static std::vector<std::string> METRICS{Metric::HAMMING, Metric::JACCARD, Metric::TANIMOTO};
    static int64_t MAX_NLIST = 65536;
    static int64_t MIN_NLIST = 1;

    CheckIntByRange(meta::ROWS, DEFAULT_MIN_ROWS, DEFAULT_MAX_ROWS);
    CheckIntByRange(meta::DIM, DEFAULT_MIN_DIM, DEFAULT_MAX_DIM);
    CheckIntByRange(IndexParams::nlist, MIN_NLIST, MAX_NLIST);
    CheckStrByValues(Metric::TYPE, METRICS);

    int64_t nlist = oricfg[IndexParams::nlist];
    CheckIntByRange(meta::ROWS, nlist, DEFAULT_MAX_ROWS);

    // Best Practice
    // static int64_t MIN_POINTS_PER_CENTROID = 40;
    // static int64_t MAX_POINTS_PER_CENTROID = 256;
    // CheckIntByRange(meta::ROWS, MIN_POINTS_PER_CENTROID * nlist, MAX_POINTS_PER_CENTROID * nlist);

    return true;
}

bool
ANNOYConfAdapter::CheckTrain(Config& oricfg, IndexMode& mode) {
    static int64_t MIN_NTREES = 1;
    // too large of n_trees takes much time, if there is real requirement, change this threshold.
    static int64_t MAX_NTREES = 1024;

    CheckIntByRange(IndexParams::n_trees, MIN_NTREES, MAX_NTREES);

    return ConfAdapter::CheckTrain(oricfg, mode);
}

bool
ANNOYConfAdapter::CheckSearch(Config& oricfg, const IndexType type, const IndexMode mode) {
    return ConfAdapter::CheckSearch(oricfg, type, mode);
}

}  // namespace knowhere
}  // namespace milvus
