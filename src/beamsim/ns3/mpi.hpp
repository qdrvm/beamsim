#pragma once

#include <mpi.h>

#include <cstdint>

namespace beamsim {
  using MpiIndex = uint32_t;

  inline MpiIndex mpiIndex() {
    static MpiIndex r = [] {
      int r;
      MPI_Comm_rank(MPI_COMM_WORLD, &r);
      return r;
    }();
    return r;
  }

  inline MpiIndex mpiSize() {
    static MpiIndex r = [] {
      int r;
      MPI_Comm_size(MPI_COMM_WORLD, &r);
      return r;
    }();
    return r;
  }

  inline bool mpiIsMain() {
    return mpiIndex() == 0;
  }

  inline int mpiAny(bool x) {
    bool r = false;
    MPI_Allreduce(&x, &r, 1, MPI_CXX_BOOL, MPI_LOR, MPI_COMM_WORLD);
    return r;
  }
}  // namespace beamsim
