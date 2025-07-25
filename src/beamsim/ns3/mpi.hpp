#pragma once

#ifdef ns3_FOUND
#include <mpi.h>
#endif

#include <cstdint>
#include <string>

namespace beamsim {
  using MpiIndex = uint32_t;

  inline MpiIndex mpiIndex() {
    static MpiIndex r = [] {
      int r = 0;
#ifdef ns3_FOUND
      MPI_Comm_rank(MPI_COMM_WORLD, &r);
#endif
      return r;
    }();
    return r;
  }

  inline MpiIndex mpiSize() {
    static MpiIndex r = [] {
      int r = 1;
#ifdef ns3_FOUND
      MPI_Comm_size(MPI_COMM_WORLD, &r);
#endif
      return r;
    }();
    return r;
  }

  inline bool mpiIsMain() {
    return mpiIndex() == 0;
  }

  inline int mpiAny(bool x) {
    bool r = x;
#ifdef ns3_FOUND
    MPI_Allreduce(&x, &r, 1, MPI_CXX_BOOL, MPI_LOR, MPI_COMM_WORLD);
#endif
    return r;
  }

  inline int64_t mpiMin(int64_t x) {
    int64_t r = x;
#ifdef ns3_FOUND
    MPI_Allreduce(&x, &r, 1, MPI_INT64_T, MPI_MIN, MPI_COMM_WORLD);
#endif
    return r;
  }

  inline std::string mpiRecvStr(MpiIndex from, int tag = 0) {
#ifdef ns3_FOUND
    std::string s;
    uint32_t size;
    MPI_Recv(
        &size, 1, MPI_UINT32_T, from, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    s.resize(size);
    MPI_Recv(s.data(),
             size,
             MPI_UINT8_T,
             from,
             tag,
             MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    return s;
#else
    abort();
#endif
  }

  inline void mpiSendStr(MpiIndex to, std::string_view s, int tag = 0) {
#ifdef ns3_FOUND
    uint32_t size = s.size();
    MPI_Send(&size, 1, MPI_UINT32_T, to, tag, MPI_COMM_WORLD);
    MPI_Send(s.data(), size, MPI_UINT8_T, to, tag, MPI_COMM_WORLD);
#endif
  }
}  // namespace beamsim
