# BeamSim - Beam Chain Networking Simulator

A high-performance simulation framework for testing beam signature aggregation protocols with support for multiple network backends and communication topologies.

## Overview

BeamSim simulates validator networks with configurable group structures, supporting both direct communication, gossip-based protocols, and grid topology. The simulator can run on different backends including simple delay models, queue-based networks, and full NS-3 network simulation.

## Features

- **Multiple Simulation Backends**: Delay-based, queue-based, and NS-3 network simulation
- **Communication Topologies**: Direct peer-to-peer and gossip protocol communication
- **Configurable Network Structure**: Customizable validator groups and group sizes
- **MPI Support**: Distributed simulation capabilities with NS-3 backend
- **Command Line Interface**: Easy configuration without code changes

## Prerequisites

### Required Dependencies

- **C++23 compatible compiler** (GCC 13+, Clang 15+, or similar)
- **CMake** 3.20 or higher
- **MPI** (for NS-3 backend support)

### Optional Dependencies

- **NS-3** (for advanced network simulation)

## Building the Project

### Quick Build (without NS-3)

```bash
# Configure and build
cmake -G Ninja -B build -D CMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

### Building with NS-3 Support

1. **Setup NS-3** (automated):
   ```bash
   # Run the setup script (downloads and builds NS-3)
   ./setup_ns3.sh
   ```

2. **Build the project**:
   ```bash
   cmake -G Ninja -B build -D CMAKE_BUILD_TYPE=RelWithDebInfo -D ns3_DIR=external/ns-allinone-3.44/install/lib/cmake/ns3
   ninja -C build
   ```

The NS-3 setup script will:
- Download NS-3 version 3.44
- Configure and build NS-3 with optimized settings
- Install NS-3 to `external/ns-allinone-3.44/install/`

## Usage

### Command Line Interface

```bash
./build/main [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-b, --backend <type>` | Simulation backend: `delay`, `queue`, `ns3` | `delay` |
| `-t, --topology <type>` | Communication topology: `direct`, `gossip` | `direct` |
| `-g, --groups <number>` | Number of validator groups | `4` |
| `-gv, --group-validators <number>` | Number of validators per group | `3` |
| `-h, --help` | Show help message | - |

### Examples

#### Basic Usage

```bash
# Run with default settings (4 groups, 3 validators each, delay backend, direct topology)
./build/main

# Show help
./build/main --help
```

#### Backend Selection

```bash
# Use delay-based network simulation (fastest)
./build/main --backend delay

# Use queue-based network simulation
./build/main --backend queue

# Use NS-3 network simulation (most realistic, requires NS-3)
./build/main --backend ns3
```

#### Topology Configuration

```bash
# Direct peer-to-peer communication
./build/main --topology direct

# Gossip protocol communication
./build/main --topology gossip
```

#### Network Size Configuration

```bash
# Small network: 2 groups of 5 validators each (10 total)
./build/main --groups 2 --group-validators 5

# Large network: 20 groups of 50 validators each (1000 total)
./build/main --groups 20 --group-validators 50
```

#### Combined Configuration

```bash
# Realistic simulation with gossip protocol
./build/main --backend ns3 --topology gossip --groups 10 --group-validators 10

# Performance testing with queue backend
./build/main --backend queue --topology direct --groups 5 --group-validators 20
```

## Simulation Backends

### Delay Backend
- **Type**: Simplified network model
- **Use Case**: Algorithm development and testing
- **Performance**: Fastest
- **Features**: Basic message delays

### Queue Backend  
- **Type**: Queue-based network simulation
- **Use Case**: Performance analysis with queuing effects
- **Performance**: Medium
- **Features**: Message queuing, contention modeling

### NS-3 Backend
- **Type**: Full network stack simulation
- **Use Case**: Realistic network behavior analysis
- **Performance**: Slowest (most detailed)
- **Features**: Complete network protocols, routing, realistic delays
- **Requirements**: NS-3 installation, MPI for distributed simulation

## Communication Topologies

### Direct Topology
- **Method**: Point-to-point connections
- **Structure**: Hierarchical (validators → local aggregators → global aggregator)
- **Use Case**: Traditional blockchain consensus
- **Scalability**: Limited by aggregator bottlenecks

### Gossip Topology
- **Method**: Epidemic/gossip protocol
- **Structure**: Overlay network with topic-based subscription
- **Use Case**: Scalable consensus protocols
- **Scalability**: Better for large networks

## Output

The simulator provides detailed timing and status information:

```
Configuration:
  Backend: ns3
  Topology: gossip
  Groups: 4
  Validators per group: 3
  Total validators: 12

routing table rules: 49
Time: 2088ms, Real: 190ms, Status: SUCCESS
```

- **Time**: Simulated time to complete consensus
- **Real**: Wall-clock time for simulation
- **Status**: SUCCESS/FAILURE based on consensus completion

## MPI Support

When using the NS-3 backend, the simulator supports distributed execution:

```bash
# Run with MPI (example with 4 processes)
mpirun -np 4 ./build/main --backend ns3 --topology gossip
```

## Development

### Project Structure

```
beamsim/
├── main.cpp              # Main simulation entry point with CLI
├── src/beamsim/          # Core simulation framework
│   ├── example/          # Example consensus protocol implementation
│   ├── gossip/           # Gossip protocol implementation  
│   └── ns3/              # NS-3 integration
├── external/             # External dependencies (NS-3)
└── build/                # Build artifacts
```

### Adding New Features

1. **New Backend**: Implement `ISimulator` interface in `src/beamsim/`
2. **New Topology**: Extend `PeerBase` class in `beamsim::example` namespace
3. **New Parameters**: Add to `SimulationConfig` struct in `main.cpp`

## Troubleshooting

### Common Issues

1. **NS-3 not found**: Run `./setup_ns3.sh` or set `ns3_FOUND=OFF` in CMake
2. **MPI errors**: Ensure MPI is properly installed and configured
3. **Compilation errors**: Verify C++23 compiler support

### Performance Tips

- Use `delay` backend for algorithm development
- Use `queue` backend for performance analysis
- Use `ns3` backend only when network realism is critical
- Start with small network sizes and scale up gradually

### Verifying Installation

After building, verify the installation works correctly:

```bash
# Test basic functionality
./build/main --help

# Run a quick simulation
./build/main --groups 2 --group-validators 3

# Test different backends (if available)
./build/main --backend delay
./build/main --backend queue
./build/main --backend ns3  # Only if NS-3 is installed
```

Expected output should show configuration details and simulation results with "Status: SUCCESS".

## License

See [LICENSE](LICENSE) file for license information.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request
