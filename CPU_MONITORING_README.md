# CPU Usage Monitoring for PIOCxTest

## Overview
This branch adds comprehensive CPU usage monitoring for both cores of the RP2040 in the PIOCxTest project.

## What's Monitored

### Core 0 (TX - Transmit)
- **Function**: `loop()` on Core 0
- **Work measured**: Message creation and DMA configuration
- **Metrics tracked**:
  - Time spent in actual work (message creation, DMA setup)
  - Total loop time (including sleep)
  - Loop iteration frequency
  - CPU utilization percentage

### Core 1 (RX - Receive)
- **Function**: `loop1()` on Core 1
- **Work measured**: DMA polling, message validation, buffer management
- **Metrics tracked**:
  - Time spent processing received messages
  - Total loop time
  - Loop iteration frequency
  - CPU utilization percentage

## How It Works

### Timing Infrastructure
- Uses `time_us_64()` for microsecond precision timing
- Minimal overhead (~few CPU cycles per measurement)
- Non-intrusive to real-time PIO/DMA operations

### Statistics Structure
```cpp
struct CPUStats {
    uint64_t total_time_us;   // Total time in measurement period
    uint64_t busy_time_us;    // Time spent doing work
    uint64_t loop_count;      // Number of loop iterations
    uint64_t last_report_time;// Last time stats were reported
    const char* core_name;    // "Core0" or "Core1"
};
```

### Reporting
- Reports every 1 second (configurable via `REPORT_INTERVAL_US`)
- Output format:
```
[Core0] CPU: 2.50% | Loops: 30000 | Freq: 30.00 kHz | Avg work: 0.83 us
[Core1] CPU: 5.12% | Loops: 2450000 | Freq: 2450.00 kHz | Avg work: 0.21 us
```

## Output Metrics Explained

1. **CPU %**: Percentage of time the core spent doing actual work
   - Formula: `(busy_time / total_time) × 100`
   - Low % means lots of idle time available

2. **Loops**: Number of loop iterations in the reporting period

3. **Freq (kHz)**: Loop iteration frequency
   - Formula: `(loop_count / total_time_ms)`
   - For Core 0: Should be ~30 kHz (matches TX rate)
   - For Core 1: Will be much higher (tight polling loop)

4. **Avg work (us)**: Average time spent on work per loop iteration
   - Formula: `busy_time / loop_count`
   - Shows how long each message/operation takes

## Testing Different Scenarios

### Modify TX Rate (Core 0)
In `PIOCxTest.ino:138`, change the sleep duration:
```cpp
sleep_us(1000000/30000);  // Current: 30 kHz
sleep_us(1000000/60000);  // Test: 60 kHz (higher load)
sleep_us(1000000/10000);  // Test: 10 kHz (lower load)
```

### Add Delays for Load Testing
Add `busy_wait_us()` to simulate heavier processing:
```cpp
// In loop() after message creation:
busy_wait_us(10);  // Simulate 10us of extra work

// In loop1() after message validation:
busy_wait_us(5);   // Simulate 5us of extra work
```

## Expected Results

### Baseline (30 kHz TX rate)
- **Core 0**: 1-5% CPU (mostly sleeping)
  - Work time: <1-2 us per message
  - Loop freq: 30 kHz
- **Core 1**: 5-20% CPU (depends on message rate)
  - Work time: <1 us per message processed
  - Loop freq: Very high (MHz range) due to polling

### Performance Indicators
- **Good**: CPU % well below 50% on both cores = headroom available
- **Warning**: CPU % approaching 80% = may struggle with bursts
- **Critical**: CPU % above 90% = at capacity, may drop messages

## Troubleshooting

### High Core 0 CPU
- DMA setup taking too long
- `rand()` or message creation overhead
- Serial print overhead (rare, only at 15k message intervals)

### High Core 1 CPU
- Too many messages to process
- Checksum validation overhead
- Buffer switching overhead
- Serial print overhead

### Measurement Overhead
The timing code itself adds minimal overhead:
- `time_us_64()` call: ~10-20 cycles (~0.1 us @ 125 MHz)
- Per-loop overhead: <1 us typically
- Reporting overhead: Only once per second

## Code Locations

- **Infrastructure**: PIOCxTest.ino:10-44
- **Core 0 monitoring**: PIOCxTest.ino:95, 120-152
- **Core 1 monitoring**: PIOCxTest.ino:180, 232-289

## Modifying Report Interval

Change the reporting frequency by modifying:
```cpp
#define REPORT_INTERVAL_US 1000000  // 1 second
#define REPORT_INTERVAL_US 500000   // 0.5 seconds (more frequent)
#define REPORT_INTERVAL_US 5000000  // 5 seconds (less frequent)
```

## Building and Flashing

Build and upload as normal using Arduino IDE or arduino-cli with RP2040 board support.
