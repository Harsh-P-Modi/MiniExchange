# Phase 2 Baseline Results

Environment: Windows laptop, no CPU pinning, no turbo-boost control
Build: RelWithDebInfo

## Single-operation latency (ns)

| Operation | Avg | Median | P99 | Max |
|---|---|---|---|---|
| ADD (no match) | 967.6 | 900 | 1300 | 30200 |
| ADD (1 fill) | 621.0 | 600 | 700 | 79800 |
| ADD (10 fills) | 2531.0 | 2300 | 3400 | 109400 |
| ADD (100 fills) | 20641.2 | 19500 | 42100 | 231200 |
| CANCEL (front) | 397.4 | 300 | 1100 | 235400 |
| CANCEL (back) | 272.7 | 200 | 900 | 74300 |

## Sustained throughput

| Workload | Orders/sec |
|---|---|
| Mixed (60% limit, 10% market, 30% cancel) | 2.92M |
