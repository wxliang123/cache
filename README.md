## Run Program

`mkdir third_party`

Install googletest & spdlog in `third_party`

`mkdir build && cd build`
`cmake .. && make`

Generate the synthetic workloads
`python ./scripts/generate_trace.py`

Test caches with synthetic workloads
`python ./scripts/run_trace.py`