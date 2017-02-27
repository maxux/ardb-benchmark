# ardb-benchmark
## Quick Setup
- `ardb-prepare.sh`: compile a ardb-server for each storage engine and put them on `/opt/ardb-bin`
- `ardb-benchmark.c`: benchmark software used for stress-test ardb, multithreaded
- `ardb-spawn.sh`: run multiple ardb instance for each storage engine
- `ardb-bench.sh`: run benchmark on all ardb-engine spawned by `ardb-spawn.sh`
