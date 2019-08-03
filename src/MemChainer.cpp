#include "MemChainer.hpp"
#include "chobo/small_vector.hpp"

// from "fastapprox"
// https://github.com/romeric/fastapprox/blob/master/fastapprox/src/fastlog.h
static inline float fastlog2(float x) {
  union {
      float f;
      uint32_t i;
  } vx = {x};
  union {
      uint32_t i;
      float f;
  } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  float y = vx.i;
  y *= 1.1920928955078125e-7f;

  return y - 124.22551499f
         - 1.498030302f * mx.f
         - 1.72587999f / (0.3520887068f + mx.f);
}

// from "fastapprox"
// https://github.com/romeric/fastapprox/blob/master/fastapprox/src/fastlog.h
static inline float fasterlog2(float x) {
  union {
      float f;
      uint32_t i;
  } vx = {x};
  float y = vx.i;
  y *= 1.1920928955078125e-7f;
  return y - 126.94269504f;
}

void MemClusterer::setMaxAllowedRefsPerHit(uint32_t max){
  maxAllowedRefsPerHit = max;
}

uint32_t MemClusterer::getMaxAllowedRefsPerHit() {
  return maxAllowedRefsPerHit;
}

bool MemClusterer::fillMemCollection(std::vector<std::pair<int, pufferfish::util::ProjectedHits>> &hits,
                                     //pufferfish::common_types::RefMemMapT &trMemMap,
                                     RefMemMap& trMemMap,
                                     std::vector<pufferfish::util::UniMemInfo> &memCollection, pufferfish::util::ReadEnd re,
                                     phmap::flat_hash_map<pufferfish::common_types::ReferenceID, bool> & other_end_refs) {
  using namespace pufferfish::common_types;
  if (hits.empty()) {
    return false;
  }

  size_t totSize{0};
  for (auto &hit : core::range<decltype(hits.begin())>(hits.begin(), hits.end())) {
    auto &refs = hit.second.refRange;
    auto rs = refs.size();
    totSize +=  (static_cast<uint64_t>(rs) < maxAllowedRefsPerHit) ? rs : 0;
  }

  // here we guarantee that even if later we fill up
  // every gap between the hits and before the first and after the last hit
  // still the memCollection size doesn't change and hence the pointers are valid
  //memCollection.reserve(maxAllowedRefsPerHit * 2 * hits.size() + 1);
  memCollection.reserve(totSize);

  for (auto &hit : core::range<decltype(hits.begin())>(hits.begin(), hits.end())) {
    auto &readPos = hit.first;
    auto &projHits = hit.second;
    // NOTE: here we rely on internal members of the ProjectedHit (i.e., member variables ending in "_").
    // Maybe we want to change the interface (make these members public or provide accessors)?
    auto &refs = projHits.refRange;
    if (static_cast<uint64_t>(refs.size()) < maxAllowedRefsPerHit) {
      uint32_t mappings{0};
      memCollection.emplace_back(projHits.contigIdx_, projHits.contigOrientation_,
                                 readPos, projHits.k_, projHits.contigPos_,
                                 projHits.globalPos_ - projHits.contigPos_, projHits.contigLen_, re);
      auto memItr = std::prev(memCollection.end());
      for (auto &posIt : refs) {
      //If we want to let the the hits to the references also found by the other end to be accepted
      //if (static_cast<uint64_t>(refs.size()) < maxAllowedRefsPerHit or other_end_refs.find(posIt.transcript_id()) != other_end_refs.end() ) {
        const auto& refPosOri = projHits.decodeHit(posIt);
        trMemMap[std::make_pair(posIt.transcript_id(), refPosOri.isFW)].
        emplace_back(memItr, refPosOri.pos, refPosOri.isFW);
        mappings++;
      //}
      }
    }
  }
  return true;
}


bool MemClusterer::findOptChain(std::vector<std::pair<int, pufferfish::util::ProjectedHits>> &hits,
                                pufferfish::util::CachedVectorMap<size_t, std::vector<pufferfish::util::MemCluster>, std::hash<size_t>>& memClusters,
                                //phmap::flat_hash_map<pufferfish::common_types::ReferenceID, std::vector<pufferfish::util::MemCluster>> &memClusters,
                                uint32_t maxSpliceGap, std::vector<pufferfish::util::UniMemInfo> &memCollection,
                                uint32_t readLen,
                                phmap::flat_hash_map<pufferfish::common_types::ReferenceID, bool>& other_end_refs,
                                bool hChain,
                                RefMemMap& trMemMap,
                                //pufferfish::common_types::RefMemMapT& trMemMap,
                                bool verbose) {
  using namespace pufferfish::common_types;
  //(void)verbose;

  // Map from (reference id, orientation) pair to a cluster of MEMs.
  if (!fillMemCollection(hits, trMemMap, memCollection, pufferfish::util::ReadEnd::LEFT, other_end_refs))
    return false;

  size_t maxHits{0};
//  for (auto hitIt = trMemMap.key_begin(); hitIt != trMemMap.key_end(); ++hitIt) {
  for (auto hitIt = trMemMap.begin(); hitIt != trMemMap.end(); ++hitIt) {

    auto& trOri = hitIt->first;
    //    auto& 
    //for (auto &trMem : core::range<decltype(trMemMap.begin())>(trMemMap.begin(), trMemMap.end())) {
    //auto &trOri = trMem.first;
    auto &tid = trOri.first;
    auto &isFw = trOri.second;
    auto &memList = *hitIt->second;
//    auto& memList = trMemMap.cache_index(hitIt->second);
    size_t hits = memList.size();
    if (hits < 0.65 * maxHits) { continue; }
    if (hits > maxHits) { maxHits = hits; }

    // sort memList according to mem reference positions
    std::sort(memList.begin(), memList.end(),
              [isFw](pufferfish::util::MemInfo &q1, pufferfish::util::MemInfo &q2) -> bool {
                  auto q1ref = q1.tpos + q1.extendedlen;
                  auto q2ref = q2.tpos + q2.extendedlen;
                  auto q1read = q1.rpos + q1.extendedlen;
                  auto q2read = q2.rpos + q2.extendedlen;
                  return q1ref != q2ref ? q1ref < q2ref :
                         (isFw ? q1read < q2read : q1read > q2read);// sort based on tpos
              });
    /*if (verbose) {
      std::cerr << "\ntid" << tid << " , isFw:" << isFw << "\n";
      for (auto &m : memList) {
        std::cerr << "\ttpos:" << m.tpos << " rpos:" << m.memInfo->rpos << " len:" << m.memInfo->memlen
                  << "\n";
      }
    }*/

    //auto minPosIt = memList.begin();
    // find the valid chains
    // Use variant of minimap2 scoring (Li 2018)
    // https://academic.oup.com/bioinformatics/advance-article/doi/10.1093/bioinformatics/bty191/4994778
    auto alpha = [](int32_t qdiff, int32_t rdiff, int32_t ilen) -> double {
        double score = ilen;
        double mindiff = (qdiff < rdiff) ? qdiff : rdiff;
        return (score < mindiff) ? score : mindiff;
    };

    auto beta = [maxSpliceGap](int32_t qdiff, int32_t rdiff, double avgseed) -> double {
        if (qdiff < 0 or ((uint32_t) std::max(qdiff, rdiff) > maxSpliceGap)) {
          return std::numeric_limits<double>::infinity();
        }
        double l = qdiff - rdiff;
        int32_t al = std::abs(l);
        // To penalize cases with organized gaps for reads such as
        // CTCCTCATCCTCCTCATCCTCCTCCTCCTCCTCCTCCTCCGCTGCCGCCGCCGACCGACTGAACCGCACCCGCCGCGCCGCACCGCCTCCAAGTCCCGGC
        // polyester simulated on human transcriptome. 0.01 -> 0.05
        return (l == 0) ? 0.0 : (0.05 * avgseed * al + 0.5 * fastlog2(static_cast<float>(al)));
    };
    constexpr const double bottomScore = std::numeric_limits<double>::lowest();
    double bestScore = bottomScore;
    int32_t bestChainEnd = -1;
    double avgseed = 31.0;
    f.clear();
    p.clear();
    keepMem.clear();
    bestChainEndList.clear();
    //auto lastHitId = static_cast<int32_t>(memList.size() - 1);

    // Compact mems before chaining.
    // UniMEMs can terminate because of the end of a contig, even if
    // there is still an exact match between the read and one or more references.
    // Here, we compact UniMEMs that would have constituted a larger (contiguous)
    // MEM with respect to the current reference.
    int32_t prev_qposi_end = 0;
    int32_t prev_rposi_end = 0;
    size_t currentMemIdx = 0;

    int32_t prev_qposi_start = -1;
    int32_t prev_rposi_start = -1;

    auto& m = memList.front();
    //bool didOinkWithoutPizza = false;
    //bool chainOfInterest{false};
    for (int32_t i = 0; i < static_cast<int32_t>(memList.size()); ++i) {
      auto &hi = memList[i];
      //chainOfInterest = /*chainOfInterest or */(hi.rpos == 1 and hi.tpos == 163 and tid == 151214);
      int32_t qposi_start = hi.isFw ? hi.rpos : readLen - (hi.rpos + hi.extendedlen);
      int32_t rposi_start = hi.tpos;

      int32_t qposi_end = hi.isFw ? (hi.rpos + hi.extendedlen) : (readLen - hi.rpos);
      int32_t rposi_end = hi.tpos + hi.extendedlen;

      /*if (chainOfInterest) {
        std::stringstream ss;
        ss << "##\n";
        ss << "oink oink!\n";
        ss << "i = " << i << "\n";
        ss << "qposi_start = " << qposi_start << ", qposi_end = " << qposi_end << "\n";
        ss << "prev_qposi_start = " << prev_qposi_start << ", prev_qposi_end =" << prev_qposi_end << "\n";
        ss << "rposi_start = " << rposi_start << ", rposi_end = " << rposi_end << "\n";
        ss << "prev_rposi_start = " << prev_rposi_start << ", prev_rposi_end = " << prev_rposi_end << "\n";
        ss << "hi.tpos = " << hi.tpos << "\n##\n";
        std::cerr << ss.str();
        didOinkWithoutPizza = true;
      }*/

      int32_t overlap_read = (prev_qposi_end - qposi_start);
      int32_t overlap_ref = (prev_rposi_end - rposi_start);
      if (i > 0 and overlap_ref >= 0 and (overlap_ref == overlap_read)) {
        //if (i > 0 and (qposi - prev_qposi) == (rposi - prev_rposi) and static_cast<int32_t>(hi.tpos) < prev_rposi) {
        /*if (chainOfInterest){
          std::cerr << "pizza pizza!\n";
          didOinkWithoutPizza = false;
        }
*/
        auto &lastMem = memList[currentMemIdx];
        uint32_t extension = rposi_end - prev_rposi_end;
        lastMem.extendedlen += extension;
        if (!isFw) {
          lastMem.rpos = hi.rpos;
        }
        hi.extendedlen = std::numeric_limits<decltype(hi.extendedlen)>::max();
        //keepMem.push_back(0);
      } else {
        currentMemIdx=i;
        //keepMem.push_back(1);
      }
      //if (didOinkWithoutPizza) { std::cerr << "\n\nNOOOOOOOOOO!!!!!!!!\n\n"; }
      prev_qposi_start = qposi_start;
      prev_rposi_start = rposi_start;
      prev_qposi_end = qposi_end;
      prev_rposi_end = rposi_end;
    }

    memList.erase(std::remove_if(memList.begin(), memList.end(),
                                 [](pufferfish::util::MemInfo& m) {
                                   bool r = m.extendedlen == std::numeric_limits<decltype(m.extendedlen)>::max(); return r;
                                 }), memList.end());
    /*
    if (verbose) {
      std::cerr << "\ntid" << tid << " , isFw:" << isFw << "\n";
      for (auto &m : memList) {
        std::cerr << "\ttpos:" << m.tpos << " rpos:" << m.rpos << " len:" << m.extendedlen
                  << "\n";
      }
    }
    */

    p.reserve(memList.size());
    f.reserve(memList.size());
    for (int32_t i = 0; i < static_cast<int32_t>(memList.size()); ++i) {
      auto &hi = memList[i];
      //if (hi.extendedlen != hi.memInfo->memlen)
      //  std::cerr<< hi.extendedlen << "  " <<  hi.memInfo->memlen << "\n";

      int32_t qposi = hi.rpos + hi.extendedlen;
      int32_t rposi = hi.tpos + hi.extendedlen;

      double baseScore = static_cast<double>(hi.extendedlen);
      p.push_back(i);
      f.push_back(baseScore);

      // possible predecessors in the chain
      int32_t numRounds{2};
      (void) numRounds;
      for (int32_t j = i - 1; j >= 0; --j) {
        auto &hj = memList[j];

        int32_t qposj = hj.rpos + hj.extendedlen;
        int32_t rposj = hj.tpos + hj.extendedlen;

        int32_t qdiff = isFw ? qposi - qposj :
                        (qposj - hj.extendedlen) - (qposi - hi.extendedlen);
        int32_t rdiff = rposi - rposj;

        auto extensionScore = f[j] + alpha(qdiff, rdiff, hi.extendedlen) - beta(qdiff, rdiff, avgseed);

        //To fix cases where there are repetting sequences in the read or reference
          /*int32_t rdiff_mem = hi.tpos - (hj.tpos + hj.extendedlen);
          int32_t qdiff_mem = isFw ? hi.rpos - (hj.rpos + hj.extendedlen) : hj.rpos - (hi.rpos + hi.extendedlen);
          if (rdiff == 0 or qdiff == 0 or rdiff * qdiff < 0 or rdiff_mem * qdiff_mem < 0 or hi.rpos == hj.rpos or
              hi.tpos == hj.tpos) {
            extensionScore = -std::numeric_limits<double>::infinity();
          }
          */
        /*
        if (verbose) {
          std::cerr << i << " " << j <<
                    " extendedleni:" << hi.extendedlen << " extendedlenj:" << hj.extendedlen <<
                    " f[i]:" << f[i] << " f[j]:" << f[j] <<
                    " readDiff:" << qdiff << " refDiff:" << rdiff <<
                    " alpha:" << alpha(qdiff, rdiff, hi.extendedlen) <<
                    " beta:" << beta(qdiff, rdiff, avgseed) <<
                    " extensionScore: " << extensionScore << "\n";
        }
        */
        bool extendWithJ = (extensionScore > f[i]);
        p[i] = extendWithJ ? j : p[i];
        f[i] = extendWithJ ? extensionScore : f[i];
        /*if ((hi.rpos == 0 and hi.tpos == 162 and tid == 151214) and hj.rpos == 7 and hj.tpos == 161) {
          std::cerr << "qdiff = " << qdiff << "\n";
          std::cerr << "rdiff = " << rdiff << "\n";
          std::cerr << "hi.len = " << hi.extendedlen << "\n";
          std::cerr << "hj.len = " << hj.extendedlen << "\n";
          std::cerr << "hi.rpos = " << hi.rpos<< "\n";
          std::cerr << "hi.tpos = " << hi.tpos<< "\n";
          std::cerr << "hj.rpos = " << hj.rpos<< "\n";
          std::cerr << "hj.tpos = " << hj.tpos<< "\n";
          std::cerr << "extensionScore = " << extensionScore << "\n";
          std::cerr << "p[" << i << "] = " << p[i] << "\n";
          std::cerr << "f[" << i << "] = " << f[i] << "\n";
        }*/

        // HEURISTIC : if we connected this match to an earlier one
        // i.e. if we extended the chain.
        // This implements Heng Li's heuristic ---
        // "
        // We note that if anchor i is chained to j, chaining i to a predecessor of j
        // is likely to yield a lower score.
        // "
        // here we take this to the extreme, and stop at the first j to which we chain.
        // we can add a parameter "h" as in the minimap paper.  But here we expect the
        // chains of matches in short reads to be short enough that this may not be worth it.
        if (hChain and p[i] < i) {
          numRounds--;
          if (numRounds <= 0) { break; }
        }
        // If the last two hits are too far from each other, we are sure that 
        // every other hit will be even further since the mems are sorted
        if (rdiff > readLen * 2) {
          break;
        }
        // Mohsen: This heuristic hurts the accuracy of the chain in the case of this read:
        // TGAACGCTCTATGATGTCAGCCTACGAGCGCTCTATGATGTTAGCCTACGAGCGCTCTATGATGTCCCCTATGGCTGAGCGCTCTATGATGTCAGCTTAT
        // from Polyester simalted sample aligning to the human transcriptome
      }
      if (f[i] > bestScore) {
        bestScore = f[i];
        bestChainEnd = i;
        bestChainEndList.clear();
        bestChainEndList.push_back(bestChainEnd);
      } else if (f[i] == bestScore) {
        bestChainEndList.push_back(i);
      }
    }
    //if (chainOfInterest) { std::cerr << "bestScore = " << bestScore << "\n"; }
    // Do backtracking
    chobo::small_vector<uint8_t> seen(f.size(), 0);
    for (auto bestChainEnd : bestChainEndList) {
      if (bestChainEnd >= 0) {
        bool shouldBeAdded = true;
        memIndicesInReverse.clear();
        auto lastPtr = p[bestChainEnd];
        while (lastPtr < bestChainEnd) {
          if (seen[bestChainEnd] > 0) {
            shouldBeAdded = false;
            //break;
          }
          memIndicesInReverse.push_back(bestChainEnd);
          seen[bestChainEnd] = 1;
          bestChainEnd = lastPtr;
          lastPtr = p[bestChainEnd];
          //lastPtr = bestChainEnd;
          //bestChainEnd = p[bestChainEnd];
        }
        if (seen[bestChainEnd] > 0) {
          shouldBeAdded = false;
        }
        memIndicesInReverse.push_back(bestChainEnd);
        if (shouldBeAdded) {
          // @fataltes --- is there a reason we were inserting here before rather than
          // pushing back?
          memClusters[tid].push_back(pufferfish::util::MemCluster(isFw, readLen));
          auto& justAddedCluster = memClusters[tid].back();
          for (auto it = memIndicesInReverse.rbegin(); it != memIndicesInReverse.rend(); it++) {
            justAddedCluster.addMem(memList[*it].memInfo, memList[*it].tpos,
                                       memList[*it].extendedlen, memList[*it].rpos, isFw);
          }
          justAddedCluster.coverage = bestScore;
          if (justAddedCluster.coverage == readLen)
            justAddedCluster.perfectChain = true;
          /*
          if (verbose)
            std::cerr<<"Added position: " << memClusters[tid][0].coverage << " " << memClusters[tid][0].mems[0].tpos << "\n";
          */
        }
        //minPosIt += lastPtr;
      } else {
        // should not happen
        std::cerr << "[FATAL] : Cannot find any valid chain for quasi-mapping\n";
        std::cerr << "num hits = " << memList.size() << "\n";
        std::cerr << "bestChainEnd = " << bestChainEnd << "\n";
        std::cerr << "bestChainScore = " << bestScore << "\n";
        std::exit(1);
      }
    }






    for (auto & memClust : memClusters[tid]) {
      auto &memList = memClust.mems;
      for (int32_t i = 0; i < static_cast<int32_t>(memList.size()); ++i) {
        auto &hi = memList[i];
        //chainOfInterest = /*chainOfInterest or */(hi.rpos == 1 and hi.tpos == 163 and tid == 151214);
        int32_t qposi_start = hi.isFw ? hi.rpos : readLen - (hi.rpos + hi.extendedlen);
        int32_t rposi_start = hi.tpos;

        int32_t qposi_end = hi.isFw ? (hi.rpos + hi.extendedlen) : (readLen - hi.rpos);
        int32_t rposi_end = hi.tpos + hi.extendedlen;

        /*if (chainOfInterest) {
          std::stringstream ss;
          ss << "##\n";
          ss << "oink oink!\n";
          ss << "i = " << i << "\n";
          ss << "qposi_start = " << qposi_start << ", qposi_end = " << qposi_end << "\n";
          ss << "prev_qposi_start = " << prev_qposi_start << ", prev_qposi_end =" << prev_qposi_end << "\n";
          ss << "rposi_start = " << rposi_start << ", rposi_end = " << rposi_end << "\n";
          ss << "prev_rposi_start = " << prev_rposi_start << ", prev_rposi_end = " << prev_rposi_end << "\n";
          ss << "hi.tpos = " << hi.tpos << "\n##\n";
          std::cerr << ss.str();
          didOinkWithoutPizza = true;
        }*/

        int32_t overlap_read = (prev_qposi_end - qposi_start);
        int32_t overlap_ref = (prev_rposi_end - rposi_start);
        if (i > 0 and overlap_ref >= 0 and (overlap_ref == overlap_read)) {
          //if (i > 0 and (qposi - prev_qposi) == (rposi - prev_rposi) and static_cast<int32_t>(hi.tpos) < prev_rposi) {
          /*if (chainOfInterest){
            std::cerr << "pizza pizza!\n";
            didOinkWithoutPizza = false;
          }*/

          auto &lastMem = memList[currentMemIdx];
          uint32_t extension = rposi_end - prev_rposi_end;
          lastMem.extendedlen += extension;
          if (!isFw) {
            lastMem.rpos = hi.rpos;
          }
          hi.extendedlen = std::numeric_limits<decltype(hi.extendedlen)>::max();
          //keepMem.push_back(0);
        } else {
          currentMemIdx=i;
          //keepMem.push_back(1);
        }
        //if (didOinkWithoutPizza) { std::cerr << "\n\nNOOOOOOOOOO!!!!!!!!\n\n"; }
        prev_qposi_start = qposi_start;
        prev_rposi_start = rposi_start;
        prev_qposi_end = qposi_end;
        prev_rposi_end = rposi_end;
      }

      memList.erase(std::remove_if(memList.begin(), memList.end(),
                                   [](pufferfish::util::MemInfo& m) {
                                       bool r = m.extendedlen == std::numeric_limits<decltype(m.extendedlen)>::max(); return r;
                                   }), memList.end());

    }

  }
  /*
  if (verbose)
    std::cerr << "\n[END OF FIND_OPT_CHAIN]\n";
  */
  return true;
}
