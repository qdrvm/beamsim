#pragma once

#include <beamsim/example/roles.hpp>
#include <beamsim/ns3/mpi.hpp>
#include <print>

// CLI Configuration
struct SimulationConfig {
  enum class Backend { DELAY, QUEUE, NS3 };
  enum class Topology {
    DIRECT,
    GOSSIP,
  };

  Backend backend = Backend::DELAY;
  Topology topology = Topology::DIRECT;
  beamsim::example::GroupIndex group_count = 4;
  beamsim::PeerIndex validators_per_group = 3;
  bool help = false;

  void print_usage(const char *program_name) {
    // clang-format off
    std::println("Usage: {} [options]", program_name);
    std::println("Options:");
    std::println("  -b, --backend <delay|queue|ns3>   Simulation backend (default: delay)");
    std::println("  -t, --topology <direct|gossip>    Communication topology (default: direct)");
    std::println("  -g, --groups <number>             Number of validator groups (default: 4)");
    std::println("  -gv, --group-validators <number>  Validators per group (default: 3)");
    std::println("  -h, --help                        Show this help message");
    // clang-format on
  }

  bool parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];

      if (arg == "-h" or arg == "--help") {
        help = true;
        return true;
      } else if (arg == "-b" or arg == "--backend") {
        if (i + 1 >= argc) {
          std::println("Error: --backend requires an argument");
          return false;
        }
        std::string backend_str = argv[++i];
        if (backend_str == "delay") {
          backend = Backend::DELAY;
        } else if (backend_str == "queue") {
          backend = Backend::QUEUE;
        } else if (backend_str == "ns3") {
          backend = Backend::NS3;
        } else {
          std::println("Error: Invalid backend '{}'. Use: delay, queue, ns3",
                       backend_str);
          return false;
        }
      } else if (arg == "-t" or arg == "--topology") {
        if (i + 1 >= argc) {
          std::println("Error: --topology requires an argument");
          return false;
        }
        std::string topology_str = argv[++i];
        if (topology_str == "direct") {
          topology = Topology::DIRECT;
        } else if (topology_str == "gossip") {
          topology = Topology::GOSSIP;
        } else {
          std::println("Error: Invalid topology '{}'. Use: direct, gossip",
                       topology_str);
          return false;
        }
      } else if (arg == "-g" or arg == "--groups") {
        if (i + 1 >= argc) {
          std::println("Error: --groups requires an argument");
          return false;
        }
        try {
          group_count = std::stoi(argv[++i]);
          if (group_count <= 0) {
            std::println("Error: Number of groups must be positive");
            return false;
          }
        } catch (const std::exception &) {
          std::println("Error: Invalid number for groups: {}", argv[i]);
          return false;
        }
      } else if (arg == "-gv" or arg == "--group-validators") {
        if (i + 1 >= argc) {
          std::println("Error: --group-validators requires an argument");
          return false;
        }
        try {
          validators_per_group = std::stoi(argv[++i]);
          if (validators_per_group <= 0) {
            std::println(
                "Error: Number of validators per group must be positive");
            return false;
          }
        } catch (const std::exception &) {
          std::println("Error: Invalid number for validators: {}", argv[i]);
          return false;
        }
      } else {
        std::println("Error: Unknown argument '{}'", arg);
        return false;
      }
    }
    return true;
  }

  void validate() {
#ifndef ns3_FOUND
    if (backend == Backend::NS3) {
      std::println(
          "Warning: ns3 backend requested but ns3 not found, falling back to "
          "delay");
      backend = Backend::DELAY;
    }
#endif
  }

  void print_config() {
    if (not beamsim::mpiIsMain()) {
      return;
    }

    std::string backend_str = (backend == Backend::DELAY) ? "delay"
                            : (backend == Backend::QUEUE) ? "queue"
                                                          : "ns3";
    std::string topology_str =
        (topology == Topology::DIRECT) ? "direct" : "gossip";

    std::println("Configuration:");
    std::println("  Backend: {}", backend_str);
    std::println("  Topology: {}", topology_str);
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
