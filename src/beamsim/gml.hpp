#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beamsim {
  static_assert(std::endian::native == std::endian::little);

  template <typename T>
  struct LowerTriangleMatrix {
    static size_t _size(size_t n) {
      return n * (n - 1) / 2;
    }
    void resize(size_t n) {
      array.resize(_size(n));
    }
    T &at(size_t i1, size_t i2) {
      if (i1 < i2) {
        std::swap(i1, i2);
      }
      if (i1 == i2) {
        throw std::logic_error("diagonal");
      }
      return array.at(_size(i1) + i2);
    }
    const T &at(size_t i1, size_t i2) const {
      return const_cast<LowerTriangleMatrix *>(this)->at(i1, i2);
    }

    std::vector<T> array;
  };

  struct Gml {
    struct Node {
      uint64_t bitrate() const {
        return std::min<uint64_t>(down_kibit, up_kibit) << 10;
      }

      uint32_t down_kibit;
      uint32_t up_kibit;
      uint16_t city;
    };
    static constexpr auto kNodeSize =
        sizeof(Node::down_kibit) + sizeof(Node::up_kibit) + sizeof(Node::city);

    std::string encode() const {
      std::string data;
      auto latency_size = latency_us.array.size() * sizeof(uint32_t);
      data.resize(sizeof(uint32_t) + nodes.size() * kNodeSize + latency_size);
      auto ptr = data.data();
      auto range = [&](const void *p, size_t n) {
        memcpy(ptr, p, n);
        ptr += n;
      };
      auto one = [&](const auto &v) { range(&v, sizeof(v)); };
      one(static_cast<uint32_t>(nodes.size()));
      for (auto &node : nodes) {
        range(&node, kNodeSize);
      }
      range(latency_us.array.data(), latency_size);
      return data;
    }

    static Gml decode(std::string_view input) {
      Gml gml;
      auto range = [&](void *p, size_t n) {
        if (input.size() < n) {
          abort();
        }
        memcpy(p, input.data(), n);
        input.remove_prefix(n);
      };
      auto one = [&](auto &v) { range(&v, sizeof(v)); };
      uint32_t n;
      one(n);
      gml.nodes.resize(n);
      for (auto &node : gml.nodes) {
        range(&node, kNodeSize);
      }
      gml.latency_us.resize(n);
      auto latency_size = gml.latency_us.array.size() * sizeof(uint32_t);
      range(gml.latency_us.array.data(), latency_size);
      return gml;
    }

    std::vector<Node> nodes;
    LowerTriangleMatrix<uint32_t> latency_us;
  };
}  // namespace beamsim
