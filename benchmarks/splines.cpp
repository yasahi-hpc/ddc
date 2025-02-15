// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <thread>
#include <vector>

#include <ddc/ddc.hpp>
#include <ddc/kernels/splines.hpp>

#include <benchmark/benchmark.h>

namespace DDC_HIP_5_7_ANONYMOUS_NAMESPACE_WORKAROUND(SPLINES_CPP)
{
    struct X
    {
        static constexpr bool PERIODIC = true;
    };

    template <typename NonUniform, std::size_t s_degree_x>
    struct BSplinesX
        : std::conditional_t<
                  NonUniform::value,
                  ddc::NonUniformBSplines<X, s_degree_x>,
                  ddc::UniformBSplines<X, s_degree_x>>
    {
    };

    template <typename NonUniform, std::size_t s_degree_x>
    using GrevillePoints = ddc::GrevilleInterpolationPoints<
            BSplinesX<NonUniform, s_degree_x>,
            ddc::BoundCond::PERIODIC,
            ddc::BoundCond::PERIODIC>;

    template <typename NonUniform, std::size_t s_degree_x>
    struct DDimX : GrevillePoints<NonUniform, s_degree_x>::interpolation_discrete_dimension_type
    {
    };

    struct Y;

    struct DDimY : ddc::UniformPointSampling<Y>
    {
    };

} // namespace DDC_HIP_5_7_ANONYMOUS_NAMESPACE_WORKAROUND(SPLINES_CPP)

// Function to monitor GPU memory asynchronously
void monitorMemoryAsync(std::mutex& mutex, bool& monitorFlag, size_t& maxUsedMem)
{
    size_t freeMem = 0;
    size_t totalMem = 0;
    while (monitorFlag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Adjust the interval as needed

        // Acquire a lock to ensure thread safety when accessing CUDA functions
        std::lock_guard<std::mutex> lock(mutex);

#if defined(__CUDACC__)
        cudaMemGetInfo(&freeMem, &totalMem);
#endif
        maxUsedMem = std::max(maxUsedMem, totalMem - freeMem);
    }
}

template <typename NonUniform, std::size_t s_degree_x>
static void characteristics_advection(benchmark::State& state)
{
    std::size_t nx = state.range(0);
    std::size_t ny = state.range(1);
    int cols_per_chunk = state.range(2);
    int preconditioner_max_block_size = state.range(3);

    size_t freeMem = 0;
    size_t totalMem = 0;
#if defined(__CUDACC__)
    cudaMemGetInfo(&freeMem, &totalMem);
#endif
    size_t initUsedMem
            = totalMem
              - freeMem; // cudaMemGetInfo gives GPU total memory occupation, we consider that other users of the GPU have constant occupancy and substract it.
    size_t maxUsedMem = initUsedMem;

    bool monitorFlag = true;
    std::mutex mutex;
    // Create a thread to monitor GPU memory asynchronously
    std::thread monitorThread(
            monitorMemoryAsync,
            std::ref(mutex),
            std::ref(monitorFlag),
            std::ref(maxUsedMem));

    if constexpr (!NonUniform::value) {
        ddc::init_discrete_space<BSplinesX<
                NonUniform,
                s_degree_x>>(ddc::Coordinate<X>(0.), ddc::Coordinate<X>(1.), nx);
    } else {
        std::vector<ddc::Coordinate<X>> breaks(nx + 1);
        for (std::size_t i(0); i < nx + 1; ++i) {
            breaks[i] = ddc::Coordinate<X>(static_cast<double>(i) / nx);
        }
        ddc::init_discrete_space<BSplinesX<NonUniform, s_degree_x>>(breaks);
    }

    ddc::init_discrete_space<DDimX<NonUniform, s_degree_x>>(
            ddc::GrevilleInterpolationPoints<
                    BSplinesX<NonUniform, s_degree_x>,
                    ddc::BoundCond::PERIODIC,
                    ddc::BoundCond::PERIODIC>::
                    template get_sampling<DDimX<NonUniform, s_degree_x>>());
    ddc::DiscreteDomain<DDimY> y_domain = ddc::init_discrete_space<DDimY>(DDimY::init<DDimY>(
            ddc::Coordinate<Y>(-1.),
            ddc::Coordinate<Y>(1.),
            ddc::DiscreteVector<DDimY>(ny)));

    auto const x_domain = ddc::GrevilleInterpolationPoints<
            BSplinesX<NonUniform, s_degree_x>,
            ddc::BoundCond::PERIODIC,
            ddc::BoundCond::PERIODIC>::template get_domain<DDimX<NonUniform, s_degree_x>>();
    ddc::Chunk density_alloc(
            ddc::DiscreteDomain<DDimX<NonUniform, s_degree_x>, DDimY>(x_domain, y_domain),
            ddc::DeviceAllocator<double>());
    ddc::ChunkSpan const density = density_alloc.span_view();
    // Initialize the density on the main domain
    ddc::DiscreteDomain<DDimX<NonUniform, s_degree_x>, DDimY> x_mesh
            = ddc::DiscreteDomain<DDimX<NonUniform, s_degree_x>, DDimY>(x_domain, y_domain);
    ddc::parallel_for_each(
            Kokkos::DefaultExecutionSpace(),
            x_mesh,
            KOKKOS_LAMBDA(ddc::DiscreteElement<DDimX<NonUniform, s_degree_x>, DDimY> const ixy) {
                double const x = ddc::coordinate(ddc::select<DDimX<NonUniform, s_degree_x>>(ixy));
                double const y = ddc::coordinate(ddc::select<DDimY>(ixy));
                density(ixy) = 9.999 * Kokkos::exp(-(x * x + y * y) / 0.1 / 2);
                // initial_density(ixy) = 9.999 * ((x * x + y * y) < 0.25);
            });
    ddc::SplineBuilder<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            BSplinesX<NonUniform, s_degree_x>,
            DDimX<NonUniform, s_degree_x>,
            ddc::BoundCond::PERIODIC,
            ddc::BoundCond::PERIODIC,
#if defined(SOLVER_LAPACK)
            ddc::SplineSolver::LAPACK,
#elif defined(SOLVER_GINKGO)
            ddc::SplineSolver::GINKGO,
#endif
            DDimX<NonUniform, s_degree_x>,
            DDimY>
            spline_builder(x_mesh, cols_per_chunk, preconditioner_max_block_size);
    ddc::PeriodicExtrapolationRule<X> periodic_extrapolation;
    ddc::SplineEvaluator<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            BSplinesX<NonUniform, s_degree_x>,
            DDimX<NonUniform, s_degree_x>,
            ddc::PeriodicExtrapolationRule<X>,
            ddc::PeriodicExtrapolationRule<X>,
            DDimX<NonUniform, s_degree_x>,
            DDimY>
            spline_evaluator(periodic_extrapolation, periodic_extrapolation);
    ddc::Chunk coef_alloc(
            spline_builder.batched_spline_domain(),
            ddc::KokkosAllocator<double, Kokkos::DefaultExecutionSpace::memory_space>());
    ddc::ChunkSpan coef = coef_alloc.span_view();
    ddc::Chunk feet_coords_alloc(
            spline_builder.batched_interpolation_domain(),
            ddc::KokkosAllocator<
                    ddc::Coordinate<X>,
                    Kokkos::DefaultExecutionSpace::memory_space>());
    ddc::ChunkSpan feet_coords = feet_coords_alloc.span_view();

    for (auto _ : state) {
        Kokkos::Profiling::pushRegion("FeetCharacteristics");
        ddc::parallel_for_each(
                Kokkos::DefaultExecutionSpace(),
                feet_coords.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<DDimX<NonUniform, s_degree_x>, DDimY> const e) {
                    feet_coords(e) = ddc::coordinate(ddc::select<DDimX<NonUniform, s_degree_x>>(e))
                                     - ddc::Coordinate<X>(0.0176429863);
                });
        Kokkos::Profiling::popRegion();
        Kokkos::Profiling::pushRegion("SplineBuilder");
        spline_builder(coef, density.span_cview());
        Kokkos::Profiling::popRegion();
        Kokkos::Profiling::pushRegion("SplineEvaluator");
        spline_evaluator(density, feet_coords.span_cview(), coef.span_cview());
        Kokkos::Profiling::popRegion();
        Kokkos::fence("End of advection step");
    }
    monitorFlag = false;
    monitorThread.join();
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(nx * ny * sizeof(double)));
    state.counters["gpu_mem_occupancy"] = maxUsedMem - initUsedMem;
    ////////////////////////////////////////////////////
    /// --------------- HUGE WARNING --------------- ///
    /// The following lines are forbidden in a prod- ///
    /// uction code. It is a necessary workaround    ///
    /// which must be used ONLY for Google Benchmark.///
    /// The reason is it acts on underlying global   ///
    /// variables, which is always a bad idea.       ///
    ////////////////////////////////////////////////////
    ddc::detail::g_discrete_space_dual<BSplinesX<NonUniform, s_degree_x>>.reset();
    if constexpr (!NonUniform::value) {
        ddc::detail::g_discrete_space_dual<ddc::UniformBsplinesKnots<BSplinesX<NonUniform, s_degree_x>>>.reset();
    } else {
        ddc::detail::g_discrete_space_dual<ddc::NonUniformBsplinesKnots<BSplinesX<NonUniform, s_degree_x>>>.reset();
    }
    ddc::detail::g_discrete_space_dual<DDimX<NonUniform, s_degree_x>>.reset();
    ddc::detail::g_discrete_space_dual<DDimY>.reset();
    ////////////////////////////////////////////////////
}

// Tuning : 512 cols and 8 precond on CPU, 16384 cols and 1 precond on GPU

#if defined(KOKKOS_ENABLE_CUDA) || defined(KOKKOS_ENABLE_HIP)
std::string chip = "gpu";
std::size_t cols_per_chunk_ref = 65535;
unsigned int preconditioner_max_block_size_ref = 1u;
#elif defined(KOKKOS_ENABLE_OPENMP)
std::string chip = "cpu";
std::size_t cols_per_chunk_ref = 8192;
unsigned int preconditioner_max_block_size_ref = 32u;
#elif defined(KOKKOS_ENABLE_SERIAL)
std::string chip = "cpu";
std::size_t cols_per_chunk_ref = 8192;
unsigned int preconditioner_max_block_size_ref = 32u;
#endif

// Uniform mesh degree 3-5
BENCHMARK(characteristics_advection<std::false_type, 3>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

BENCHMARK(characteristics_advection<std::false_type, 4>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

BENCHMARK(characteristics_advection<std::false_type, 5>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

// Non-uniform mesh degree 3-5
BENCHMARK(characteristics_advection<std::true_type, 3>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

BENCHMARK(characteristics_advection<std::true_type, 4>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

BENCHMARK(characteristics_advection<std::true_type, 5>)
        ->RangeMultiplier(2)
        ->Ranges({{64, 1024},
                  {100, 200000},
                  {cols_per_chunk_ref, cols_per_chunk_ref},
                  {preconditioner_max_block_size_ref, preconditioner_max_block_size_ref}})
        ->MinTime(3)
        ->UseRealTime();

int main(int argc, char** argv)
{
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::AddCustomContext("chip", chip);
#if defined(SOLVER_LAPACK)
    std::string backend = "LAPACK";
#elif defined(SOLVER_GINKGO)
    std::string backend = "GINKGO";
#endif
    ::benchmark::AddCustomContext("backend", backend);
    ::benchmark::AddCustomContext("cols_per_chunk_ref", std::to_string(cols_per_chunk_ref));
    ::benchmark::AddCustomContext(
            "preconditioner_max_block_size_ref",
            std::to_string(preconditioner_max_block_size_ref));
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    {
        Kokkos::ScopeGuard const kokkos_scope(argc, argv);
        ddc::ScopeGuard const ddc_scope(argc, argv);
        ::benchmark::RunSpecifiedBenchmarks();
    }
    ::benchmark::Shutdown();
    return 0;
}
