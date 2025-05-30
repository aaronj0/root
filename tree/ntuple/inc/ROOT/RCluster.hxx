/// \file ROOT/RCluster.hxx
/// \ingroup NTuple
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2020-03-11
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2020, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RCluster
#define ROOT_RCluster

#include <ROOT/RNTupleUtil.hxx>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ROOT {
namespace Experimental {
namespace Internal {

// clang-format off
/**
\class ROnDiskPage
\ingroup NTuple
\brief A page as being stored on disk, that is packed and compressed

Used by the cluster pool to cache pages from the physical storage. Such pages generally need to be
uncompressed and unpacked before they can be used by RNTuple upper layers.
*/
// clang-format on
class ROnDiskPage {
private:
   /// The memory location of the bytes
   const void *fAddress = nullptr;
   /// The compressed and packed size of the page. This includes both payload and checksum (if present)
   std::uint32_t fSize = 0;

public:
   /// On-disk pages within a page source are identified by the column and page number. The key is used for
   /// associative collections of on-disk pages.
   struct Key {
      ROOT::DescriptorId_t fPhysicalColumnId;
      std::uint64_t fPageNo;
      Key(ROOT::DescriptorId_t columnId, std::uint64_t pageNo) : fPhysicalColumnId(columnId), fPageNo(pageNo) {}
      friend bool operator ==(const Key &lhs, const Key &rhs) {
         return lhs.fPhysicalColumnId == rhs.fPhysicalColumnId && lhs.fPageNo == rhs.fPageNo;
      }
   };

   ROnDiskPage() = default;
   ROnDiskPage(void *address, std::uint32_t size) : fAddress(address), fSize(size) {}

   const void *GetAddress() const { return fAddress; }
   std::uint32_t GetSize() const { return fSize; }

   bool IsNull() const { return fAddress == nullptr; }
}; // class ROnDiskPage

} // namespace Internal
} // namespace Experimental
} // namespace ROOT

// For hash maps ROnDiskPage::Key --> ROnDiskPage
namespace std
{
template <>
struct hash<ROOT::Experimental::Internal::ROnDiskPage::Key> {
   // TODO(jblomer): quick and dirty hash, likely very sub-optimal, to be revised later.
   size_t operator()(const ROOT::Experimental::Internal::ROnDiskPage::Key &key) const
   {
      return (
         (std::hash<ROOT::DescriptorId_t>()(key.fPhysicalColumnId) ^ (hash<ROOT::NTupleSize_t>()(key.fPageNo) << 1)) >>
         1);
   }
};
}


namespace ROOT {
namespace Experimental {
namespace Internal {

// clang-format off
/**
\class ROOT::Experimental::Internal::ROnDiskPageMap
\ingroup NTuple
\brief A memory region that contains packed and compressed pages

Derived classes implement how the on-disk pages are stored in memory, e.g. mmap'd or in a special area.
*/
// clang-format on
class ROnDiskPageMap {
   friend class RCluster;

private:
   std::unordered_map<ROnDiskPage::Key, ROnDiskPage> fOnDiskPages;

public:
   ROnDiskPageMap() = default;
   ROnDiskPageMap(const ROnDiskPageMap &other) = delete;
   ROnDiskPageMap(ROnDiskPageMap &&other) = default;
   ROnDiskPageMap &operator =(const ROnDiskPageMap &other) = delete;
   ROnDiskPageMap &operator =(ROnDiskPageMap &&other) = default;
   virtual ~ROnDiskPageMap();

   /// Inserts information about a page stored in fMemory.  Therefore, the address referenced by onDiskPage
   /// needs to be owned by the page map (see derived classes).  If a page map contains a page of a given column,
   /// it is expected that _all_ the pages of that column in that cluster are part of the page map.
   void Register(const ROnDiskPage::Key &key, const ROnDiskPage &onDiskPage) { fOnDiskPages.emplace(key, onDiskPage); }
}; // class ROnDiskPageMap

// clang-format off
/**
\class ROOT::Experimental::Internal::ROnDiskPageMapHeap
\ingroup NTuple
\brief An ROnDiskPageMap that is used for an fMemory allocated as an array of unsigned char.
*/
// clang-format on
class ROnDiskPageMapHeap : public ROnDiskPageMap {
private:
   /// The memory region containing the on-disk pages.
   std::unique_ptr<unsigned char []> fMemory;
public:
   explicit ROnDiskPageMapHeap(std::unique_ptr<unsigned char []> memory) : fMemory(std::move(memory)) {}
   ROnDiskPageMapHeap(const ROnDiskPageMapHeap &other) = delete;
   ROnDiskPageMapHeap(ROnDiskPageMapHeap &&other) = default;
   ROnDiskPageMapHeap &operator =(const ROnDiskPageMapHeap &other) = delete;
   ROnDiskPageMapHeap &operator =(ROnDiskPageMapHeap &&other) = default;
   ~ROnDiskPageMapHeap() override;
}; // class ROnDiskPageMapHeap

// clang-format off
/**
\class ROOT::Experimental::Internal::RCluster
\ingroup NTuple
\brief An in-memory subset of the packed and compressed pages of a cluster

Binds together several page maps that represent all the pages of certain columns of a cluster
*/
// clang-format on
class RCluster {
public:
   using ColumnSet_t = std::unordered_set<ROOT::DescriptorId_t>;
   /// The identifiers that specifies the content of a (partial) cluster
   struct RKey {
      ROOT::DescriptorId_t fClusterId = ROOT::kInvalidDescriptorId;
      ColumnSet_t fPhysicalColumnSet;
   };

protected:
   /// References the cluster identifier in the page source that created the cluster
   ROOT::DescriptorId_t fClusterId;
   /// Multiple page maps can be combined in a single RCluster
   std::vector<std::unique_ptr<ROnDiskPageMap>> fPageMaps;
   /// Set of the (complete) columns represented by the RCluster
   ColumnSet_t fAvailPhysicalColumns;
   /// Lookup table for the on-disk pages
   std::unordered_map<ROnDiskPage::Key, ROnDiskPage> fOnDiskPages;

public:
   explicit RCluster(ROOT::DescriptorId_t clusterId) : fClusterId(clusterId) {}
   RCluster(const RCluster &other) = delete;
   RCluster(RCluster &&other) = default;
   RCluster &operator =(const RCluster &other) = delete;
   RCluster &operator =(RCluster &&other) = default;
   ~RCluster() = default;

   /// Move the given page map into this cluster; for on-disk pages that are present in both the cluster at hand and
   /// pageMap, GetOnDiskPage() may return the page from either of the memory regions (left to the implementation).
   /// Their content is supposed to be the same.
   /// Page maps cannot be physically merged them because they have potentially used different allocation mechanisms
   /// (e.g. mmap vs. malloc).
   void Adopt(std::unique_ptr<ROnDiskPageMap> pageMap);
   /// Move the contents of other into this cluster; for on-disk pages that are present in both the cluster at hand and
   /// the "other" cluster, GetOnDiskPage() may return the page from either of the memory regions
   /// (left to the implementation).
   void Adopt(RCluster &&other);
   /// Marks the column as complete; must be done for all columns, even empty ones without associated pages,
   /// before the cluster is given from the page storage to the cluster pool.  Marking the available columns is
   /// typically the last step of RPageSouce::LoadCluster().
   void SetColumnAvailable(ROOT::DescriptorId_t physicalColumnId);
   const ROnDiskPage *GetOnDiskPage(const ROnDiskPage::Key &key) const;

   ROOT::DescriptorId_t GetId() const { return fClusterId; }
   const ColumnSet_t &GetAvailPhysicalColumns() const { return fAvailPhysicalColumns; }
   bool ContainsColumn(ROOT::DescriptorId_t colId) const { return fAvailPhysicalColumns.count(colId) > 0; }
   size_t GetNOnDiskPages() const { return fOnDiskPages.size(); }
}; // class RCluster

} // namespace Internal
} // namespace Experimental
} // namespace ROOT

#endif
