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

### Docker Build

The easiest way to build and run BeamSim is using Docker:

```bash
# Build Docker image
make docker_image

# Test the build
make docker_test


```

> It was noticed that the Docker build does not behave consistently when BEAMSIM is using MPI (e.g. in large scale simulations). Therefore it is recommended to run large scale simulations using local build.

For detailed Docker build configuration and options, see [`docs/BUILD.md`](docs/BUILD.md) and [`docs/MAKEFILE.md`](docs/MAKEFILE.md).

### Local Build with NS-3 Support

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
./build/beamsim [options]
```

### Essential Options

Here is the quick reference with the most important options, please see `Parameter reference` section for more detailed parameters description.

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
./build/beamsim

# Show help
./build/beamsim --help
```

#### Backend Selection

```bash
# Use delay-based network simulation (fastest)
./build/beamsim --backend delay

# Use queue-based network simulation
./build/beamsim --backend queue

# Use NS-3 network simulation (most realistic, requires NS-3)
./build/beamsim --backend ns3-direct
```

#### Topology Configuration

```bash
# Direct peer-to-peer communication
./build/beamsim --topology direct

# Gossipsub protocol communication
./build/beamsim --topology gossip
```

#### Network Size Configuration

```bash
# Small network: 2 groups of 5 validators each (10 total)
./build/beamsim --groups 2 --group-validators 5

# Large network: 20 groups of 50 validators each (1000 total)
./build/beamsim --groups 20 --group-validators 50
```

#### Combined Configuration

```bash
# Realistic simulation with gossip protocol
./build/beamsim --backend ns3-direct --topology gossip --groups 10 --group-validators 10

# Performance testing with queue backend
./build/beamsim --backend queue --topology direct --groups 5 --group-validators 20
```

## Parameter Reference (CLI and YAML)

BeamSim can be configured via command-line flags and/or a YAML file. When both are provided, CLI flags override YAML.

- Load YAML: `-c, --config <path>`
- Override any value via the CLI flags below.

### Enumerations and units

- Backend: 
   - `ns3-direct`: recommended, the most realistic simulating TCP stack
   - `queue`: faster backend, without TCP simulation
   - `ns3`: realistic, obsolete
   - `delay`: obsolete, not recommended
- Topology: `direct`, `gossip`, `grid`
- Time (YAML): suffix `ms` or `us` (e.g., `20ms`, `30us`)
- Bitrate: plain number (bits/sec) or `Mbps` (e.g., `100Mbps`)
- Abs-or-ratio inputs: absolute number or percentage with `%` (e.g., `3` or `10%`)

### Core simulation

- backend
   - YAML: `backend: delay|queue|ns3|ns3-direct`
   - CLI: `-b, --backend <value>`
   - Selects the simulation backend. Default: `ns3-direct`.
- topology
   - YAML: `topology: direct|gossip|grid`
   - CLI: `-t, --topology <value>`
   - Communication topology across nodes. Default: `direct`.
- random_seed
   - YAML: `random_seed: <uint>`
   - Seed for reproducibility (YAML only). Default: `0`.

### Roles (network size and aggregators)

- roles.group_count
   - YAML: `roles.group_count: <uint>`
   - CLI: `-g, --groups <number>`
   - Number of validator groups. Default: `4`.
- roles.group_validator_count
   - YAML: `roles.group_validator_count: <uint>`
   - CLI: `-gv, --group-validators <number>`
   - Validators per group (includes local aggregators). Default: `4`.
- roles.group_local_aggregator_count
   - YAML: `roles.group_local_aggregator_count: <number|percent%>`
   - CLI: `-la, --local-aggregators <number|percent%>`
   - Local aggregators per group (absolute or percentage of validators per group). Default: `1`.
- roles.global_aggregator_count
   - YAML: `roles.global_aggregator_count: <uint>`
   - CLI: `-ga, --global-aggregators <number>`
   - Total number of global aggregators. Default: `1`.

Note: Values are validated to ensure local aggregators per group do not exceed the feasible maximum given global aggregators.

### Behavior toggles

- shuffle
   - YAML: `shuffle: true|false`
   - CLI: `--shuffle`
   - Shuffle validators from the same group across different routers (for `ns3` backend). Default: `false`.
- snark1_group_once
   - YAML: `snark1_group_once: true|false`
   - CLI: `--snark1-group-once`
   - Global aggregator accepts only the first SNARK1 per group. Default: `true`.
- snark1_pull
   - YAML: `snark1_pull: true|false`
   - CLI: `--snark1-pull`
   - Broadcast bitfield instead of SNARK1 (pull-based dissemination). Default: `true`.
- snark1_pull_early
   - YAML: `snark1_pull_early: true|false`
   - CLI: `--snark1-pull-early`
   - Start broadcasting the bitfield while SNARK1 is still being generated (early pull). Default: `false`.
   - Note: Enabling `snark1_pull_early` implies `snark1_pull`.
- signature_half_direct
   - YAML: `signature_half_direct: true|false`
   - CLI: `--signature-half-direct`
   - Send signatures only to aggregators. Default: `false`.
- snark1_half_direct
   - YAML: `snark1_half_direct: true|false`
   - CLI: `--snark1-half-direct`
   - Do not send SNARK1 to local aggregators (direct-topology optimization). Default: `false`.
- local_aggregation_only
   - YAML: (CLI only)
   - CLI: `--local-aggregation-only`
   - Stop after local aggregation produces SNARK1. Default: `false`.
- report
   - YAML: (CLI only)
   - CLI: `--report`
   - Print machine-readable report lines for plotting. Default: `false`.

### Gossip settings (topology=gossip)

 - gossip.mesh_n — YAML: `gossip.mesh_n: <uint>` — Target peers in mesh per topic. Default: `4`.
 - gossip.non_mesh_n — YAML: `gossip.non_mesh_n: <uint>` — Maintain extra non-mesh peers. Default: `4`.
 - gossip.idontwant — YAML: `gossip.idontwant: true|false` — Enable IDONTWANT control messages. Default: `false`.

### Network

- network.gml
   - YAML: `network.gml: <path>`
   - CLI: `--gml <path>`
   - Use latencies and bitrate **between pairs of nodes** from a shadow atlas binary. Default: empty (disabled).
- network.max_bitrate
   - YAML: `network.max_bitrate: <number|Mbps>`
   - CLI: `--max-bitrate <number|Mbps>`
   - Maximum incoming bandwidth **per node** (default `100Mbps`).
- network.gml_bitrate
   - YAML: `network.gml_bitrate: <uint>`
   - CLI: (YAML only)
   - Override bitrate for links derived from the GML/shadow atlas. Default: `0` (use `max_bitrate`).

### Cryptographic and compute constants (consts)

All under `consts:` in YAML.

- signature_time: `<time>` — time for a validator to create an initial signature.
- signature_size: `<uint>` — bytes per signature.
- snark_size: `<uint>` — bytes per SNARK proof.
- snark1_threshold: `<double>` — fraction of signatures needed to build SNARK1.
- snark2_threshold: `<double>` — fraction of signatures needed to build final SNARK2.
- aggregation_rate_per_sec: `<double>` — signature aggregation speed (sigs/sec).
- snark_recursion_aggregation_rate_per_sec: `<double>` — SNARK recursion speed (proofs/sec).
- pq_signature_verification_time: `<time>` — time to verify one PQ signature.
- snark_proof_verification_time: `<time>` — time to verify one SNARK proof.

Defaults (when not set in YAML):
- signature_time: `20ms`
- signature_size: `3072`
- snark_size: `131072`
- snark1_threshold: `0.9`
- snark2_threshold: `0.66`
- aggregation_rate_per_sec: `1000`
- snark_recursion_aggregation_rate_per_sec: `100`
- pq_signature_verification_time: `3ms`
- snark_proof_verification_time: `10ms`

### CLI-only quick reference

- `-c, --config <path>` — load YAML config
- `-b, --backend <delay|queue|ns3|ns3-direct>`
- `-t, --topology <direct|gossip|grid>`
- `-g, --groups <number>`
- `-gv, --group-validators <number>`
- `-la, --local-aggregators <number|percent%>`
- `-ga, --global-aggregators <number>`
- `--max-bitrate <number|Mbps>`
- `--gml <path>`
- `--shuffle`
- `--snark1-group-once`
- `--snark1-pull`
- `--snark1-pull-early`
- `--signature-half-direct`
- `--snark1-half-direct`
- `--local-aggregation-only`
- `--report`
- `-h, --help`

### Examples

- YAML (see also `sample.yaml`):

```yaml
backend: ns3-direct
topology: gossip
random_seed: 42

roles:
   group_count: 8
   group_validator_count: 512
   group_local_aggregator_count: 10%
   global_aggregator_count: 102

gossip:
   mesh_n: 8
   non_mesh_n: 4
   idontwant: false

consts:
   signature_time: 20ms
   signature_size: 2530
   snark_size: 131072
   snark1_threshold: 0.75
   snark2_threshold: 0.66
   aggregation_rate_per_sec: 1000
   snark_recursion_aggregation_rate_per_sec: 50
   pq_signature_verification_time: 30us
   snark_proof_verification_time: 5ms

network:
   gml: "shadow-atlas.bin"
   max_bitrate: 100Mbps
```

- CLI overrides:

```bash
# Use 10% local aggregators, 4 global aggregators, and a tighter bandwidth cap
./build/beamsim -c sample.yaml -la 10% -ga 4 --max-bitrate 50Mbps

# Enable pull-based SNARK1 dissemination
./build/beamsim --backend ns3-direct --topology direct -g 4 -gv 64 \
   --snark1-pull
```

## Jupyter Notebook Analysis

BeamSim includes a comprehensive Jupyter notebook (`beamsim.ipynb`) for advanced simulation analysis and visualization. The notebook provides interactive plotting capabilities to analyze network performance across different topologies.

### Prerequisites

Install the required Python dependencies:

```bash
pip install -r requirements.txt
```

This will install the necessary packages including:
- `seaborn` for enhanced plotting
- Other visualization and analysis libraries

### Starting Jupyter Notebook

1. **Navigate to the project directory**:
   ```bash
   cd /path/to/beamsim
   ```

2. **Start Jupyter Notebook**:
   ```bash
   jupyter notebook
   ```

3. **Open the analysis notebook**:
   - In the Jupyter interface, click on `beamsim.ipynb`
   - The notebook will open in a new tab

### Notebook Features

The notebook provides several analysis tools:

- **SNARK Distribution Analysis**: Visualize how SNARK proofs are distributed across the network over time
- **Network Traffic Analysis**: Monitor bandwidth usage by different node roles (validators, aggregators)
- **Topology Comparison**: Compare performance metrics across different network topologies (direct, gossip, grid)
- **Peak Traffic Analysis**: Identify network bottlenecks and peak usage patterns

### Running Simulations from Notebook

The notebook uses a YAML configuration file and supports the same parameters as the CLI:

```python
# Example: Run simulation with custom parameters
run_kwargs = dict(
    c=yaml_config_path,  # YAML configuration
    g=10,                # Number of groups
    gv=64,               # Validators per group
    mpi=10,              # Enable MPI with 10 processes
)

# Generate comparison plots across topologies
topologies = ["direct", "gossip", "grid"]
plot1_topologies(topologies, **run_kwargs)
```

### Customizing Analysis

You can modify the simulation parameters in the notebook to:
- Test different network sizes
- Compare various backend types
- Analyze specific topology configurations
- Generate custom visualizations

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

### NS3-direct Backend
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
- **Method**: Epidemic/gossipsub protocol
- **Structure**: Overlay network with topic-based subscription
- **Use Case**: Scalable consensus protocols
- **Scalability**: Better for large networks

### Grid Topology
- **Method**: 2D mesh network
- **Structure**: Nodes arranged in a grid, with local communication between neighbors
- **Use Case**: Efficient data aggregation and dissemination
- **Scalability**: Good for moderate-sized networks up to 10000 nodes

## Output

The simulator provides detailed timing and status information:

```
Configuration:
  Backend: ns3-direct
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
mpirun -np 4 ./build/beamsim --backend ns3-direct --topology gossip
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
- Use `ns3-direct` backend only when network realism is critical
- Start with small network sizes and scale up gradually

### Verifying Installation

After building, verify the installation works correctly:

```bash
# Test basic functionality
./build/beamsim --help

# Run a quick simulation
./build/beamsim --groups 2 --group-validators 3

# Test different backends (if available)
./build/beamsim --backend delay
./build/beamsim --backend queue
./build/beamsim --backend ns3-direct  # Only if NS-3 is installed
```

Expected output should show configuration details and simulation results with "Status: SUCCESS".

## Documentation

For detailed information about building, configuration, and development:

- **[Build Guide](docs/BUILD.md)**: Complete Docker build instructions, configuration options, and troubleshooting
- **[Makefile Reference](docs/MAKEFILE.md)**: Comprehensive guide to all Makefile targets and variables

## License

See [LICENSE](LICENSE) file for license information.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request
