#pragma once

#include <yaml-cpp/yaml.h>

#include <beamsim/example/roles.hpp>
#include <beamsim/from_chars.hpp>
#include <beamsim/gossip/config.hpp>
#include <beamsim/ns3/mpi.hpp>

struct Bitrate {
  uint64_t v;

  static std::optional<Bitrate> parse(std::string_view s) {
    auto r = beamsim::numFromChars<decltype(v)>(s);
    if (not r.has_value()) {
      return std::nullopt;
    }
    auto [num, suffix] = r.value();
    if (suffix.empty()) {
      return Bitrate{num};
    }
    if (suffix == "Mbps") {
      return Bitrate{num * 1'000'000};
    }
    return std::nullopt;
  }
};

struct Ratio {
  double v;
};

template <typename T>
struct AbsOrRatio {
  std::variant<T, Ratio> v;

  AbsOrRatio(T v) : v{v} {}
  AbsOrRatio(Ratio v) : v{v} {}

  T get(T n) const {
    if (auto *abs = std::get_if<T>(&v)) {
      return *abs;
    }
    return std::get<Ratio>(v).v * n;
  }

  static std::optional<AbsOrRatio<T>> parse(std::string_view s) {
    if (s.ends_with("%")) {
      auto r = beamsim::numFromChars<decltype(Ratio::v)>(s);
      if (not r.has_value()) {
        return std::nullopt;
      }
      auto [num, suffix] = r.value();
      if (suffix != "%") {
        return std::nullopt;
      }
      if (std::is_unsigned_v<T> and num < 0) {
        return std::nullopt;
      }
      return Ratio{num / 100};
    }
    auto r = beamsim::numFromChars<T>(s);
    if (not r.has_value()) {
      return std::nullopt;
    }
    auto [num, suffix] = r.value();
    if (not suffix.empty()) {
      return std::nullopt;
    }
    return num;
  }
};

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
      if (not r.has_value()) {
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
      auto r = beamsim::numFromChars<T>(arg.value());
      if (not r.has_value() or not r->second.empty()) {
        std::println("Error: {} expects {}number",
                     flag2,
                     std::is_unsigned_v<T> ? "positive " : "");
        return false;
      }
      Flag<T>::value_ = r->first;
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

  struct FlagBitrate : Flag<Bitrate> {
    void help1(std::string &line) const {
      Flag<Bitrate>::help1(line);
      line += " <bitrate>";
    }
    void help2(std::string &line) const {
      Flag<Bitrate>::help2(line);
      line += std::format(" (default: {})", Flag<Bitrate>::value_.v);
    }
    bool parse(std::string flag2, Args &args) const {
      auto arg = args.next();
      if (not arg) {
        return false;
      }
      auto r = Bitrate::parse(arg.value());
      if (not r.has_value()) {
        std::println("Error: {} expects bitrate", flag2);
        return false;
      }
      value_ = r.value();
      return true;
    }
  };

  template <typename T>
  struct FlagAbsOrRatio : Flag<AbsOrRatio<T>> {
    void help1(std::string &line) const {
      Flag<AbsOrRatio<T>>::help1(line);
      line += " <number|percent%>";
    }
    void help2(std::string &line) const {
      Flag<AbsOrRatio<T>>::help2(line);
      std::string s;
      auto &v = Flag<AbsOrRatio<T>>::value_.v;
      if (auto *abs = std::get_if<T>(&v)) {
        s = std::to_string(*abs);
      } else {
        s = std::format("{:.2f}%", 100 * std::get<Ratio>(v).v);
      }
      line += std::format(" (default: {})", s);
    }
    bool parse(std::string flag2, Args &args) const {
      auto arg = args.next();
      if (not arg) {
        return false;
      }
      auto &value = Flag<AbsOrRatio<T>>::value_;
      auto r = value.parse(arg.value());
      if (not r.has_value()) {
        std::println("Error: {} expects number or percent", flag2);
        return false;
      }
      value = r.value();
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

  struct KnownPaths {
    std::map<std::string, KnownPaths> children;
  };

  struct Value {
    operator bool() const {
      return node.IsDefined();
    }

    Value &required() {
      if (not node.IsDefined()) {
        error();
      }
      return *this;
    }

    template <typename T>
    void get(T &value, const Args::Enum<T> &enum_) const {
      if (not node.IsDefined()) {
        return;
      }
      auto r = enum_.parse(node.as<std::string>());
      if (not r.has_value()) {
        error();
      }
      value = r.value();
    }

    void get(beamsim::Time &value) const {
      if (not node.IsDefined()) {
        return;
      }
      auto str = node.as<std::string>();
      auto r = beamsim::numFromChars<uint64_t>(str);
      if (not r.has_value()) {
        error();
      }
      auto [count, suffix] = r.value();
      if (suffix == "ms") {
        value = std::chrono::milliseconds{count};
      } else if (suffix == "us") {
        value = std::chrono::microseconds{count};
      } else {
        error();
      }
    }

    void get(Bitrate &value) const {
      if (not node.IsDefined()) {
        return;
      }
      auto str = node.as<std::string>();
      auto r = Bitrate::parse(str);
      if (not r.has_value()) {
        error();
      }
      value = r.value();
    }

    template <typename T>
    void get(AbsOrRatio<T> &value) const {
      if (not node.IsDefined()) {
        return;
      }
      auto r = value.parse(node.as<std::string>());
      if (not r.has_value()) {
        error();
      }
      value = r.value();
    }

    template <typename T>
    void get(T &value) const {
      if (not node.IsDefined()) {
        return;
      }
      value = node.as<T>();
    }

    [[noreturn]] void error() const {
      throw YAML::BadConversion{node.Mark()};
    }

    Value at(Path path) const {
      auto new_path = path;
      std::optional<const YAML::Node> node = this->node;
      auto *known = this->known;
      auto value = *this;
      for (auto &key : path) {
        new_path.emplace_back(key);
        if (node.value().IsDefined()) {
          node.emplace(node.value()[key]);
        }
        known = &known->children[key];
      }
      return Value{path, node.value(), known};
    }

    Path path;
    const YAML::Node node;
    KnownPaths *known;
  };

  Value at(Path path) {
    return Value{{}, root, &known_paths}.at(path);
  }

  static void checkUnknown(Path &path,
                           YAML::Node node,
                           const KnownPaths &known) {
    assert2(node.IsDefined());
    if (node.IsMap()) {
      for (auto x : node) {
        auto key = x.first.as<std::string>();
        path.emplace_back(key);
        auto it = known.children.find(key);
        if (it == known.children.end()) {
          std::string s;

          auto first = true;
          for (auto &key : path) {
            if (first) {
              first = false;
            } else {
              s += ".";
            }
            s += key;
          }
          std::println("unknown yaml keys: {}", s);
        }
        checkUnknown(path,
                     x.second,
                     it == known.children.end() ? KnownPaths{} : it->second);
        path.pop_back();
      }
    }
  }
  void checkUnknown() const {
    Path path;
    checkUnknown(path, root, known_paths);
  }

  const YAML::Node root;
  KnownPaths known_paths{};
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

  beamsim::example::RolesConfig roles_config;

  std::string config_path;
  Args::FlagStr flag_config_path{{
      {"-c", "--config"},
      config_path,
      "yaml config path",
  }};
  Backend backend = Backend::NS3_DIRECT;
  Args::FlagEnum<decltype(backend)> flag_backend{
      {"-b", "--backend"},
      backend,
      "Simulation backend",
      enum_backend_,
  };
  Topology topology = Topology::GOSSIP;
  Args::FlagEnum<decltype(topology)> flag_topology{
      {"-t", "--topology"},
      topology,
      "Communication topology",
      enum_topology_,
  };
  Args::FlagInt<beamsim::example::GroupIndex> flag_group_count{{
      {"-g", "--groups"},
      roles_config.group_count,
      "Number of validator groups",
  }};
  Args::FlagInt<beamsim::PeerIndex> flag_validators_per_group{{
      {"-gv", "--group-validators"},
      roles_config.group_validator_count,
      "Validators per group",
  }};
  bool shuffle = false;
  Args::FlagBool flag_shuffle{{
      {"--shuffle"},
      shuffle,
      "shuffle validators from same group to different routers",
  }};
  bool snark1_group_once = true;
  Args::FlagBool flag_snark1_group_once{{
      {"--snark1-group-once"},
      snark1_group_once,
      "global aggregator accepts only accepts first snark1 from group",
  }};
  bool snark1_pull = true;
  Args::FlagBool flag_snark1_pull{{
      {"--snark1-pull"},
      snark1_pull,
      "broadcast bitfield instead of snark1",
  }};
  bool snark1_pull_early = false;
  Args::FlagBool flag_snark1_pull_early{{
      {"--snark1-pull-early"},
      snark1_pull_early,
      "broadcast bitfield while generating snark1",
  }};
  beamsim::PeerIndex signature_half_direct = 0;
  Args::FlagInt<beamsim::PeerIndex> flag_signature_half_direct{{
      {"--signature-half-direct"},
      signature_half_direct,
      "don't send signature to validators, send to N local aggregators",
  }};
  bool snark1_half_direct = false;
  Args::FlagBool flag_snark1_half_direct{{
      {"--snark1-half-direct"},
      snark1_half_direct,
      "don't send snark1 to local aggregators",
  }};
  bool local_aggregation_only = false;
  Args::FlagBool flag_local_aggregation_only{{
      {"--local-aggregation-only"},
      local_aggregation_only,
      "stop simulation after local aggregator generates snark1",
  }};
  bool snark1_global_smart_push = false;
  Args::FlagBool flag_snark1_global_smart_push{{
      {"--snark1-global-smart-push"},
      snark1_global_smart_push,
      "global aggregators push the first snark1 per group and ignore further ones",
  }};
  std::optional<beamsim::DirectRouterConfig> direct_router;
  std::string gml_path;
  Args::FlagStr flag_gml_path{{
      {"--gml"},
      gml_path,
      "bin gml path",
  }};
  uint64_t gml_bitrate = 0;
  uint32_t random_seed = 0;
  Bitrate max_bitrate = {100 * 1'000'000};
  Args::FlagBitrate flag_max_bitrate{{
      {"--max-bitrate"},
      max_bitrate,
      "Maximum bitrate per node",
  }};
  bool report = false;
  Args::FlagBool flag_report{
      {{"--report"}, report, "Print report data for plots"}};
  bool help = false;
  Args::FlagBool flag_help{{{"-h", "--help"}, help, "Show this help message"}};
  AbsOrRatio<beamsim::PeerIndex> local_aggregators = {1};
  Args::FlagAbsOrRatio<beamsim::PeerIndex> flag_local_aggregators{{
      {"-la", "--local-aggregators"},
      local_aggregators,
      "Number of local aggregators per group",
  }};
  Args::FlagInt<beamsim::example::GroupIndex> flag_global_aggregators{{
      {"-ga", "--global-aggregators"},
      roles_config.global_aggregator_count,
      "Number of global aggregators",
  }};

  auto flags(auto &&f) {
    return f(flag_config_path,
             flag_backend,
             flag_topology,
             flag_group_count,
             flag_validators_per_group,
             flag_shuffle,
             flag_snark1_group_once,
             flag_snark1_pull,
             flag_snark1_pull_early,
             flag_signature_half_direct,
             flag_snark1_half_direct,
             flag_local_aggregation_only,
             flag_snark1_global_smart_push,
             flag_gml_path,
             flag_max_bitrate,
             flag_report,
             flag_help,
             flag_local_aggregators,
             flag_global_aggregators);
  }

  beamsim::gossip::Config gossip_config;

  static void print_usage(const char *program_name) {
    SimulationConfig config;
    std::println("Usage: {} [options]", program_name);
    std::println("Options:");
    config.flags([&](auto &&...a) { Args::help(a...); });
  }

  bool _parse(Args args) {
    return flags([&](auto &&...a) { return args.parse(a...); });
  }

  bool parse_args(int argc, char **argv) {
    SimulationConfig tmp;
    if (not tmp._parse({argc, argv})) {
      return false;
    }
    if (not tmp.config_path.empty()) {
      yaml(tmp.config_path);
    }
    if (not _parse({argc, argv})) {
      return false;
    }
    if (local_aggregation_only) {
      roles_config.global_aggregator_count = 1;
      roles_config.group_count = 1;
    }
    auto max_group_local_aggregators =
        (roles_config.group_count * roles_config.group_validator_count
         - roles_config.global_aggregator_count)
        / roles_config.group_count;
    roles_config.group_local_aggregator_count =
        local_aggregators.get(roles_config.group_validator_count);
    if (roles_config.group_local_aggregator_count
        > max_group_local_aggregators) {
      std::println("Warning: group local aggregator count {} exceeds {}",
                   roles_config.group_local_aggregator_count,
                   max_group_local_aggregators);
      roles_config.group_local_aggregator_count = max_group_local_aggregators;
    }
    return true;
  }

  void yaml(std::string path) {
    Yaml yaml{YAML::LoadFile(path)};
    yaml.at({"backend"}).get(backend, enum_backend_);
    yaml.at({"topology"}).get(topology, enum_topology_);
    yaml.at({"shuffle"}).get(shuffle);
    yaml.at({"snark1_group_once"}).get(snark1_group_once);
    yaml.at({"snark1_pull"}).get(snark1_pull);
    yaml.at({"snark1_pull_early"}).get(snark1_pull_early);
    yaml.at({"signature_half_direct"}).get(signature_half_direct);
    yaml.at({"snark1_half_direct"}).get(snark1_half_direct);
    yaml.at({"snark1_global_smart_push"}).get(snark1_global_smart_push);

    yaml.at({"random_seed"}).get(random_seed);

    yaml.at({"roles", "group_count"}).get(roles_config.group_count);
    yaml.at({"roles", "group_validator_count"})
        .get(roles_config.group_validator_count);
    yaml.at({"roles", "global_aggregator_count"})
        .get(roles_config.global_aggregator_count);
    yaml.at({"roles", "group_local_aggregator_count"}).get(local_aggregators);

    yaml.at({"gossip", "mesh_n"}).get(gossip_config.mesh_n);
    yaml.at({"gossip", "non_mesh_n"}).get(gossip_config.non_mesh_n);
    yaml.at({"gossip", "idontwant"}).get(gossip_config.idontwant);

    auto &consts = beamsim::consts();
    yaml.at({"consts", "signature_time"}).get(consts.signature_time);
    yaml.at({"consts", "signature_size"}).get(consts.signature_size);
    yaml.at({"consts", "snark_size"}).get(consts.snark_size);
    yaml.at({"consts", "snark1_threshold"}).get(consts.snark1_threshold);
    yaml.at({"consts", "snark2_threshold"}).get(consts.snark2_threshold);
    yaml.at({"consts", "aggregation_rate_per_sec"})
        .get(consts.aggregation_rate_per_sec);
    yaml.at({"consts", "snark_recursion_aggregation_rate_per_sec"})
        .get(consts.snark_recursion_aggregation_rate_per_sec);
    yaml.at({"consts", "pq_signature_verification_time"})
        .get(consts.pq_signature_verification_time);
    yaml.at({"consts", "snark_proof_verification_time"})
        .get(consts.snark_proof_verification_time);

    if (auto direct = yaml.at({"network", "direct"})) {
      auto range = [&](std::string name, auto &range) {
        direct.at({name, "min"}).required().get(range.first);
        direct.at({name, "max"}).required().get(range.second);
      };
      auto &config = direct_router.emplace();
      range("bitrate", config.bitrate);
      range("delay", config.delay);
    }
    yaml.at({"network", "max_bitrate"}).get(max_bitrate);
    yaml.at({"network", "gml"}).get(gml_path);
    yaml.at({"network", "gml_bitrate"}).get(gml_bitrate);

    yaml.checkUnknown();
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
    auto &consts = beamsim::consts();
    if (consts.snark1_threshold < consts.snark2_threshold) {
      // TODO: repeat snark1 aggregation with more signatures
      std::println(
          "Warning: snark1 threshold shouldn't be less than snark2 threshold");
    }
    if (snark1_pull_early and not snark1_pull) {
      snark1_pull = true;
      std::println("Warning: snark1_pull_early implies snark1_pull");
    }
  }

  void print_config() {
    if (not beamsim::mpiIsMain()) {
      return;
    }
    std::println("Configuration:");
    std::println("  Backend: {}", enum_backend_.str(backend));
    std::println("  Topology: {}", enum_topology_.str(topology));
    std::println("  Groups: {}", roles_config.group_count);
    std::println("  Validators per group: {}",
                 roles_config.group_validator_count);
    std::println("  Local aggregators per group: {}",
                 roles_config.group_local_aggregator_count);
    std::println("  Global aggregators: {}",
                 roles_config.global_aggregator_count);
    std::println("  Total validators: {}",
                 roles_config.group_count * roles_config.group_validator_count);
    if (beamsim::mpiSize() > 1) {
      std::println("  MPI: {}", beamsim::mpiSize());
    } else {
      std::println("  MPI: no");
    }
    std::println("  Random seed: {}", random_seed);
    std::println();
  }
};
