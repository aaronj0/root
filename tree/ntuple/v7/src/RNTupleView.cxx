/// \file RNTupleView.cxx
/// \ingroup NTuple ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2024-10-28
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2024, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RError.hxx>
#include <ROOT/RFieldBase.hxx>
#include <ROOT/RNTupleDescriptor.hxx>
#include <ROOT/RNTupleView.hxx>
#include <ROOT/RPageStorage.hxx>

ROOT::RNTupleGlobalRange ROOT::Internal::GetFieldRange(const ROOT::RFieldBase &field,
                                                       const ROOT::Experimental::Internal::RPageSource &pageSource)
{
   const auto &desc = pageSource.GetSharedDescriptorGuard().GetRef();

   auto fnGetPrincipalColumnId = [&desc](ROOT::DescriptorId_t fieldId) -> ROOT::DescriptorId_t {
      auto columnIterable = desc.GetColumnIterable(fieldId);
      return (columnIterable.size() > 0) ? columnIterable.begin()->GetPhysicalId() : ROOT::kInvalidDescriptorId;
   };

   auto columnId = fnGetPrincipalColumnId(field.GetOnDiskId());
   if (columnId == ROOT::kInvalidDescriptorId) {
      for (const auto &f : field) {
         columnId = fnGetPrincipalColumnId(f.GetOnDiskId());
         if (columnId != ROOT::kInvalidDescriptorId)
            break;
      }
   }

   if (columnId == ROOT::kInvalidDescriptorId) {
      return ROOT::RNTupleGlobalRange(ROOT::kInvalidNTupleIndex, ROOT::kInvalidNTupleIndex);
   }

   auto arraySize = std::max(std::uint64_t(1), desc.GetFieldDescriptor(field.GetOnDiskId()).GetNRepetitions());
   return ROOT::RNTupleGlobalRange(0, desc.GetNElements(columnId) / arraySize);
}
