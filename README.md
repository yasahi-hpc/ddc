# Development of performance portable spline solver for exa-scale plasma turbulence simulation
This page is the artifact description of the paper entitled *Development of performance portable spline solver for exa-scale plasma turbulence simulation* presented in [P3HPC 2024](https://p3hpc.org/workshop/2024/).
It includes the basic instruction for compilation and experiment workflows. The expected results and the data evaluation method in the paper are also demonstrated.

## How software can be obtained (if available)
The source codes are publicly available here. (branch: kokkos-kernels-profile, SHA: e3ef33b). The source codes for profiling are ``examples/characteristics\_advection.cpp'' and ``benchmarks/splines.cpp''. 

## Hardware dependencies
We have tested on [Intel Xeon Gold 6346 (Icelake)](https://www.intel.com/content/www/us/en/products/sku/212457/intel-xeon-gold-6346-processor-36m-cache-3-10-ghz/specifications.html),
[NVIDIA A100](https://images.nvidia.com/aem-dam/en-zz/Solutions/data-center/nvidia-ampere-architecture-whitepaper.pdf), and [AMD MI250X](https://www.amd.com/system/files/documents/amd-cdna2-white-paper.pdf) GPUs.
For AMD MI250X, we regard each Graphic Compute Die (GCD) as a single GPU.

## Software dependencies
This software relies on external libraries including [`Kokkos`](https://github.com/kokkos/kokkos), [`mdspan`](https://github.com/kokkos/mdspan), [`Kokkos-kernels`](https://github.com/kokkos/kokkos-kernels), [`googletest`](https://github.com/google/googletest), [`benchmark`](https://github.com/google/benchmark), [`LAPACK`](https://github.com/Reference-LAPACK/lapack), [`Kokkos-tools`](https://github.com/kokkos/kokkos-tools) and [`Ginkgo`](https://github.com/ginkgo-project/ginkgo). [`Kokkos`](https://github.com/kokkos/kokkos), [`mdspan`](https://github.com/kokkos/mdspan), [`Kokkos-kernels`](https://github.com/kokkos/kokkos-kernels), [`googletest`](https://github.com/google/googletest), and [`benchmark`](https://github.com/google/benchmark) are included as submodules. [`LAPACK`](https://github.com/Reference-LAPACK/lapack), [`Ginkgo`](https://github.com/ginkgo-project/ginkgo), and [`Kokkos-tools`](https://github.com/kokkos/kokkos-tools) are assumed to be installed to the system. For [`Kokkos-kernels`](https://github.com/kokkos/kokkos-kernels), we rely on [our fork](https://github.com/yasahi-hpc/kokkos-kernels) in the branch `develop-spline-kernels`. 

## Installation
First of all, we have to git clone DDC in the following manner.
```bash
git clone --recursive \
https://github.com/yasahi-hpc/ddc.git
cd ddc
git switch kokkos-kernels-profile
```
Then, we configure and build DDC with the following commands.

```bash
cmake -B build \
-DCMAKE_CXX_COMPILER=<compiler_name> \ 
-DCMAKE_BUILD_TYPE=Release \
-DDDC_BUILD_KERNELS_FFT=OFF \
-DDDC_BUILD_KERNELS_SPLINES=ON \
-DDDC_BUILD_PDI_WRAPPER=OFF \
-DDDC_BUILD_BENCHMARKS=ON \
-DDDC_SPLINES_SOLVER=GINKGO \ # or LAPACK
-DDDC_SPLINES_VERSION=0 # 0, 1, 2 or None
<COMMANDS_FOR_KOKKOS>
cmake --build build -j 16
```

| ARCHITECTURE | `Icelake` | `A100` | `MI250X` |
|:-------------:|:-------------:|:-------------:|:-------------:|
| compiler | g++ | g++ | hipcc |
| `COMMANDS_FOR_KOKKOS` | `-DKokkos_ENABLE_OPENMP=ON` <br> `-DKokkos_ARCH_SKX=ON` | `-DKokkos_ENABLE_CUDA=ON` <br> `-DKokkos_ARCH_AMPERE80=ON` | `-DKokkos_ENABLE_HIP=ON` <br> `-DKokkos_ARCH_AMD_GFX90A=ON` |

## Experimental workflow for *Optimization for Kokkos-kernels implementation* (section VI)
### Compilation
For the compilation of baseline, we set `-DDDC_SPLINES_SOLVER=LAPACK` and
compile without `-DDDC_SPLINES_VERSION` for
baseline. For kernel-fusion and `spmv`, we compile with
`-DDDC_SPLINES_VERSION=1` and `-DDDC_SPLINES_VERSION=2`, respectively.

### Evaluation and expected results
We execute the benchmark app (placed at `build/examples/characteristics_advection`) in the following way. The first and second arguments to the executable are the non-uniformity of mesh and degree of splines.

```bash
export tools_dir=<path-to-kokkos-tools>
#profile with nsys
nsys profile .app 0 3 \
--kokkos-tools-libs=\
${tools_dir}/libkp_nvtx_connector.so
ncu -f --set full -o profile .app 0 3 \
--kokkos-tools-libs=\
${tools_dir}/libkp_nvtx_connector.so

# Just run with Kokkos-tools
export PATH=${PATH}:${tools_dir}
export LD_LIBRARY_PATH=\
${LD_LIBRARY_PATH}:${tools_dir}

./app 0 3
${tools_dir}/../bin/kp_reader *.dat
```

Finally, we get the following performance results in
standard output file in the ascii format. We use the average time for a measurement.

```bash
 (Type)   Total Time, Call Count, Avg. Time per Call, %Total Time in Kernels, %Total Program Time
-------------------------------------------------------------------------
Regions: 
- ddc_splines_solve
 (REGION)   0.011385 1 0.011385 37.002425 0.016346
- KokkosBlas::gemm[noETI]
 (REGION)   0.008380 2 0.004190 27.235744 0.012031
-------------------------------------------------------------------------
```

## Experimental workflow for *Benchmark for spline construction with iterative and direct methods* (section V)
### Compilation
For the compilation of benchmark, we set `-DDDC_SPLINES_SOLVER=GINKGO` for Ginkgo and `-DDDC_SPLINES_SOLVER=LAPACK` with `-DDDC_SPLINES_VERSION=2` for Kokkos-kernels.

### Evaluation and expected results
We execute the benchmark app (placed at `build/benchmarks/ddc_benchmark_splines`) in the following way.

```bash
./ddc_benchmark_splines --benchmark_format=json \
--benchmark_out=<file>.json
```

`<file>` is `splines_bench_<lib>_<backend>.json`, where`<lib>` is `ginkgo` or `lapack` and `<backend>` is `omp`, `cuda` or `hip`.
After we gather all the profiles, we visualize the data with python in the following way, where all the json files must be present under `<dir>`. Figs. 2 (a)-(f) are found under `<dir>` in png format.

```bash
python comparison.py -dirname <dir>
```

## DDC
For the original implementation, please visit [DDC github page](https://github.com/CExA-project/ddc).
