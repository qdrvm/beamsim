#include <beamsim/from_chars.hpp>
#include <beamsim/gml.hpp>
#include <beamsim/mmap.hpp>
#include <beamsim/write_file.hpp>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <print>
#include <unordered_map>

using std::literals::operator""sv;

struct Tokens {
  std::string_view input;

  void error() const {
    throw std::logic_error("gml parse error");
  }
  void skipWhitespace() {
    auto i = input.find_first_not_of(" \n"sv);
    if (i == std::string_view::npos) {
      return;
    }
    input.remove_prefix(i);
  }
  std::string_view take(size_t n) {
    auto r = input.substr(0, n);
    input.remove_prefix(n);
    return r;
  }
  std::string_view token() {
    skipWhitespace();
    if (input.empty()) {
      return input;
    }
    if (input[0] == '[' or input[0] == ']') {
      return take(1);
    }
    if (input[0] == '"') {
      auto i = input.find('"', 1);
      if (i == std::string_view::npos) {
        error();
      }
      return take(i + 1);
    }
    auto i = input.find_first_of(" \n[]\""sv);
    return take(i == std::string_view::npos ? input.size() : i);
  }
  void token(std::string_view expected) {
    auto t = token();
    if (t != expected) {
      error();
    }
  }
  std::optional<std::string_view> bracket() {
    auto t = token();
    if (t == "]"sv) {
      return std::nullopt;
    }
    return t;
  }
  std::string_view str() {
    auto t = token();
    if (t[0] != '"') {
      error();
    }
    if (t.back() != '"') {
      error();
    }
    t.remove_prefix(1);
    t.remove_suffix(1);
    return t;
  }
  template <typename T>
  T num() {
    auto r = beamsim::numFromChars<T>(token());
    if (not r or r->second != ""sv) {
      error();
    }
    return r->first;
  }
  template <typename T>
  std::pair<T, std::string_view> numSuffix() {
    auto r = beamsim::numFromChars<T>(str());
    if (not r) {
      error();
    }
    return *r;
  }
  template <typename T>
  T numSuffix(std::string_view expected) {
    auto [num, suffix] = numSuffix<T>();
    if (suffix != expected) {
      error();
    }
    return num;
  }
  uint32_t kibit() {
    return numSuffix<uint32_t>(" Kibit"sv);
  }
  uint32_t us() {
    return numSuffix<uint32_t>(" us"sv);
  }
};

beamsim::Gml parse(std::string_view input) {
  using _Node = uint16_t;
  using NodeIndex = uint16_t;
  using City = uint16_t;
  using _City = uint64_t;
  Tokens tokens{input};
  beamsim::Gml gml;
  std::unordered_map<_Node, NodeIndex> node_id_index;
  std::unordered_map<_City, City> city_index;
  auto nodes = true;
  auto bad_nodes = 0;
  tokens.token("graph"sv);
  tokens.token("["sv);
  while (auto t = tokens.bracket()) {
    if (t == "node"sv) {
      auto remove = false;
      if (not nodes) {
        tokens.error();
      }
      tokens.token("["sv);
      tokens.token("id"sv);
      auto node_id = tokens.num<_Node>();
      tokens.token("label"sv), tokens.str();
      tokens.token("ip_address"sv), tokens.str();
      auto t = tokens.token();
      std::optional<_City> _city;
      if (t == "city_code"sv) {
        _city = tokens.numSuffix<_City>(""sv);
        t = tokens.token();
      } else {
        remove = true;
      }
      if (t == "country_code"sv) {
        tokens.str();
        t = tokens.token();
      } else {
        remove = true;
      }
      if (t != "bandwidth_down"sv) {
        tokens.error();
      }
      auto down = tokens.kibit();
      tokens.token("bandwidth_up"sv);
      auto up = tokens.kibit();
      tokens.token("]"sv);
      if (not remove) {
        auto city_it = city_index.find(_city.value());
        if (city_it == city_index.end()) {
          city_it = city_index.emplace(_city.value(), city_index.size()).first;
        }
        node_id_index.emplace(node_id, gml.nodes.size());
        gml.nodes.emplace_back(beamsim::Gml::Node{
            .down_kibit = down,
            .up_kibit = up,
            .city = city_it->second,
        });
      } else {
        ++bad_nodes;
      }
      continue;
    }
    if (t == "edge"sv) {
      if (nodes) {
        nodes = false;
        gml.latency_us.resize(gml.nodes.size());
      }
      tokens.token("["sv);
      tokens.token("source"sv);
      auto i1 = tokens.num<_Node>();
      tokens.token("target"sv);
      auto i2 = tokens.num<_Node>();
      tokens.token("latency"sv);
      auto latency = tokens.us();
      tokens.token("packet_loss"sv);
      tokens.num<double>();
      tokens.token("label"sv);
      tokens.str();
      tokens.token("]"sv);
      auto it1 = node_id_index.find(i1);
      auto it2 = node_id_index.find(i2);
      if (i1 != i2 and it1 != node_id_index.end()
          and it2 != node_id_index.end()) {
        gml.latency_us.at(it1->second, it2->second) = latency;
      }
      continue;
    }
    tokens.error();
  }
  if (bad_nodes != 0) {
    std::println("gml: {} bad nodes", bad_nodes);
  }
  return gml;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::println("usage: gml2bin [graph.gml] [graph.bin]");
    return EXIT_FAILURE;
  }
  std::string input_path = argv[1];
  std::string output_path = argv[2];

  beamsim::Mmap input{input_path};
  input.sequential();
  if (not input.good()) {
    std::println("can't read {}", input_path);
    return EXIT_FAILURE;
  }
  auto gml = parse(input.data);

  auto bin = gml.encode();
  std::println("gml {}mb -> bin {}mb, {}%",
               input.data.size() >> 20,
               bin.size() >> 20,
               100 * bin.size() / input.data.size());
  beamsim::writeFile(output_path, bin);
}
