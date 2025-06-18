#pragma once

#include <yaml-cpp/yaml.h>

#include <beamsim/example/roles.hpp>
#include <beamsim/ns3/mpi.hpp>
#include <charconv>
#include <print>

struct Args {
  Args(int argc, char **argv)
      : args_{argv + 1, static_cast<size_t>(argc - 1)} {}

  using Flags = std::vector<std::string>;
  template <typename T>
  struct Flag {
    void help1(std::string &line) const {
      auto sep = false;
      for (auto &flag : flags_) {
        if (sep) {
          line += ", ";
        } else {
          sep = true;
        }
        line += flag;
      }
    }

    void help2(std::string &line) const {
      line += help_;
    }

    Flags flags_;
    T &value_;
    std::string help_;
  };

  template <typename T>
  struct Enum {
    Enum(std::map<T, std::string> map) : map_{map} {}

    std::optional<T> parse(std::string_view s) const {
      for (auto &p : map_) {
        if (p.second == s) {
          return p.first;
        }
      }
      return std::nullopt;
    }

    std::string str(T v) const {
      return map_.at(v);
    }

    void join(std::string &out, std::string sep) const {
      auto first = true;
      for (auto &p : map_) {
        if (first) {
          first = false;
        } else {
          out += sep;
        }
        out += p.second;
      }
    }

    std::map<T, std::string> map_;
  };

  template <typename T>
  struct FlagEnum : Flag<T> {
    FlagEnum(Flags flags, T &value, std::string help, const Enum<T> &enum_)
        : Flag<T>{flags, value, help}, enum_{enum_} {}

    void help1(std::string &line) const {
      Flag<T>::help1(line);
      line += " <";
      enum_.join(line, "|");
      line += ">";
    }
    void help2(std::string &line) const {
      Flag<T>::help2(line);
      line += std::format(" (default: {})", enum_.str(Flag<T>::value_));
    }
    bool parse(std::string flag2, Args &args) const {
      auto arg = args.next();
      if (not arg) {
        return false;
      }
      auto r = enum_.parse(arg.value());
      if (not r) {
        std::string s;
        enum_.join(s, ", ");
        std::println("Error: {} expects one of: {}", flag2, s);
        return false;
      }
      Flag<T>::value_ = r.value();
      return true;
    }

    const Enum<T> &enum_;
  };

  template <std::integral T>
  struct FlagInt : Flag<T> {
    void help1(std::string &line) const {
      Flag<T>::help1(line);
      line += " <number>";
    }
    void help2(std::string &line) const {
      Flag<T>::help2(line);
      line += std::format(" (default: {})", Flag<T>::value_);
    }
    bool parse(std::string flag2, Args &args) const {
      auto arg = args.next();
      if (not arg) {
        return false;
      }
      auto end = arg->data() + arg->size();
      auto r = std::from_chars(arg->data(), end, Flag<T>::value_);
      if (r.ec != std::errc{} or r.ptr != end) {
        std::println("Error: {} expects {}number",
                     flag2,
                     std::is_unsigned_v<T> ? "positive " : "");
        return false;
      }
      return true;
    }
  };

  struct FlagBool : Flag<bool> {
    bool parse(std::string, Args &) const {
      value_ = true;
      return true;
    }
  };

  struct FlagStr : Flag<std::string> {
    bool parse(std::string, Args &args) const {
      auto arg = args.next();
      if (not arg) {
        return false;
      }
      value_ = arg.value();
      return true;
    }
  };

  template <typename... A>
  static void help(const A &...a) {
    std::vector<std::string> lines;
    lines.resize(sizeof...(a));
    size_t align = 0;
    auto help1 = [&, i = 0](const auto &flag) mutable {
      auto &line = lines.at(i);
      line += "  ";
      flag.help1(line);
      beamsim::setMax(align, line.size());
      ++i;
    };
    (help1(a), ...);
    for (auto &line : lines) {
      line.resize(align, ' ');
    }
    auto help2 = [&, i = 0](const auto &flag) mutable {
      auto &line = lines.at(i);
      line += "  ";
      flag.help2(line);
      ++i;
    };
    (help2(a), ...);
    for (auto &line : lines) {
      std::println("{}", line);
    }
    /*
    "  -b, --backend <delay|queue|ns3>   Simulation backend (default: delay)"
    "  -t, --topology <direct|gossip>    Communication topology (default: direct)"
    "  -g, --groups <number>             Number of validator groups (default: 4)"
    "  -gv, --group-validators <number>  Validators per group (default: 3)"
    */
  }

  template <typename... A>
  bool parse(const A &...a) {
    std::map<std::string, std::function<bool(std::string)>> flags;
    auto add = [&, this](const auto &flag) {
      auto parse = [&, this](std::string flag2) {
        return flag.parse(flag2, *this);
      };
      for (auto &flag2 : flag.flags_) {
        flags.emplace(flag2, parse);
      }
    };
    (add(a), ...);
    while (auto flag2 = next()) {
      auto it = flags.find(flag2.value());
      if (it == flags.end()) {
        std::println("Error: Unknown argument '{}'", flag2.value());
        return false;
      }
      if (not it->second(flag2.value())) {
        return false;
      }
    }
    return true;
  }

  std::optional<std::string> next() {
    if (args_.empty()) {
      return std::nullopt;
    }
    auto r = args_[0];
    args_ = args_.subspan(1);
    return r;
  }

  std::span<char *> args_;
};

struct Yaml {
  using Path = std::vector<std::string>;
  struct Value {
    template <typename T>
    void get(T &value, const Args::Enum<T> &enum_) const {
      if (not node.IsDefined()) {
        return;
      }
      value = enum_.parse(node.as<std::string>()).value();
    }

    template <typename T>
    void get(T &value) const {
      if (not node.IsDefined()) {
        return;
      }
      value = node.as<T>();
    }

    Path path;
    YAML::Node node;
  };

  Value at(Path path) const {
    std::optional<const YAML::Node> node = root;
    for (auto &key : path) {
      node.emplace(node.value()[key]);
    }
    return Value{path, node.value()};
  }

  YAML::Node root;
};

// CLI Configuration
struct SimulationConfig {
  enum class Backend {
    DELAY,
    QUEUE,
    NS3,
    NS3_DIRECT,
  };
  enum class Topology {
    DIRECT,
    GOSSIP,
    GRID,
  };

  const Args::Enum<Backend> enum_backend_{{
      {Backend::DELAY, "delay"},
      {Backend::QUEUE, "queue"},
      {Backend::NS3, "ns3"},
      {Backend::NS3_DIRECT, "ns3-direct"},
  }};
  const Args::Enum<Topology> enum_topology_{{
      {Topology::DIRECT, "direct"},
      {Topology::GOSSIP, "gossip"},
      {Topology::GRID, "grid"},
  }};

  std::string config_path;
  Args::FlagStr flag_config_path{{
      {"-c", "--config"},
      config_path,
      "yaml config path",
  }};
  Backend backend = Backend::DELAY;
  Args::FlagEnum<decltype(backend)> flag_backend{
      {"-b", "--backend"},
      backend,
      "Simulation backend",
      enum_backend_,
  };
  Topology topology = Topology::DIRECT;
  Args::FlagEnum<decltype(topology)> flag_topology{
      {"-t", "--topology"},
      topology,
      "Communication topology",
      enum_topology_,
  };
  beamsim::example::GroupIndex group_count = 4;
  Args::FlagInt<decltype(group_count)> flag_group_count{{
      {"-g", "--groups"},
      group_count,
      "Number of validator groups",
  }};
  beamsim::PeerIndex validators_per_group = 3;
  Args::FlagInt<decltype(validators_per_group)> flag_validators_per_group{{
      {"-gv", "--group-validators"},
      validators_per_group,
      "Validators per group",
  }};
  bool shuffle = false;
  Args::FlagBool flag_shuffle{{{"--shuffle"}, shuffle, ""}};
  bool help = false;
  Args::FlagBool flag_help{{{"-h", "--help"}, help, "Show this help message"}};

  static void print_usage(const char *program_name) {
    SimulationConfig config;
    std::println("Usage: {} [options]", program_name);
    std::println("Options:");
    Args::help(config.flag_config_path,
               config.flag_backend,
               config.flag_topology,
               config.flag_group_count,
               config.flag_validators_per_group,
               config.flag_shuffle,
               config.flag_help);
  }

  bool parse_args(int argc, char **argv) {
    if (not Args{argc, argv}.parse(flag_config_path,
                                   flag_backend,
                                   flag_topology,
                                   flag_group_count,
                                   flag_validators_per_group,
                                   flag_shuffle,
                                   flag_help)) {
      return false;
    }
    if (not config_path.empty()) {
      yaml();
    }
    return true;
  }

  void yaml() {
    Yaml yaml{YAML::LoadFile(config_path)};
    yaml.at({"backend"}).get(backend, enum_backend_);
    yaml.at({"topology"}).get(topology, enum_topology_);
    yaml.at({"groups"}).get(group_count);
    yaml.at({"group-validators"}).get(validators_per_group);
    yaml.at({"shuffle"}).get(shuffle);
  }

  void validate() {
#ifndef ns3_FOUND
    if (backend == Backend::NS3 or backend == Backend::NS3_DIRECT) {
      if (beamsim::mpiIsMain()) {
        std::println(
            "Warning: ns3 backend requested but simulator is build without ns3 "
            "support, install ns3 and rebuild simulator with ns3 support");
      }
      exit(EXIT_FAILURE);
    }
#endif
  }

  void print_config() {
    if (not beamsim::mpiIsMain()) {
      return;
    }
    std::println("Configuration:");
    std::println("  Backend: {}", enum_backend_.str(backend));
    std::println("  Topology: {}", enum_topology_.str(topology));
    std::println("  Groups: {}", group_count);
    std::println("  Validators per group: {}", validators_per_group);
    std::println("  Total validators: {}",
                 group_count * validators_per_group + 1);
    if (beamsim::mpiSize() > 1) {
      std::println("  MPI: {}", beamsim::mpiSize());
    } else {
      std::println("  MPI: no");
    }
    std::println();
  }
};
