// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

//! [includes]
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>

#include <ddc/ddc.hpp>
#include <ddc/kernels/splines.hpp>

#include <Kokkos_Core.hpp>

#define PERIODIC_DOMAIN // Comment this to run non-periodic simulation

//! [X-dimension]
/// Our first continuous dimension
struct X
{
#ifdef PERIODIC_DOMAIN
    static constexpr bool PERIODIC = true;
#else
    static constexpr bool PERIODIC = false;
#endif
};
//! [X-dimension]

//! [boundary-condition]
#ifdef PERIODIC_DOMAIN
static constexpr ddc::BoundCond BoundCond = ddc::BoundCond::PERIODIC;
using ExtrapolationRule = ddc::PeriodicExtrapolationRule<X>;
#else
static constexpr ddc::BoundCond BoundCond = ddc::BoundCond::GREVILLE;
using ExtrapolationRule = ddc::NullExtrapolationRule;
#endif
//! [boundary-condition]

//! [X-discretization]
/// A uniform discretization of X
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
        BoundCond,
        BoundCond>;

template <typename NonUniform, std::size_t s_degree_x>
struct DDimX
    : GrevillePoints<NonUniform, s_degree_x>::
              interpolation_discrete_dimension_type
{
};
//! [X-discretization]

//! [Y-space]
// Our second continuous dimension
struct Y;
// Its uniform discretization
struct DDimY : ddc::UniformPointSampling<Y>
{
};
//! [Y-space]

//! [time-space]
// Our simulated time dimension
struct T;
// Its uniform discretization
struct DDimT : ddc::UniformPointSampling<T>
{
};
//! [time-space]

//! [display]
/** A function to pretty print the density
 * @param time the time at which the output is made
 * @param density the density at this time-step
 */
/*
template <class ChunkType>
void display(double time, ChunkType density)
{
    double const mean_density = ddc::transform_reduce(
                                        density.domain(),
                                        0.,
                                        ddc::reducer::sum<double>(),
                                        density)
                                / density.domain().size();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "At t = " << time << ",\n";
    std::cout << "  * mean density  = " << mean_density << "\n";
    // take a slice in the middle of the box
    ddc::ChunkSpan density_slice = density
            [ddc::get_domain<DDimY>(density).front()
             + ddc::get_domain<DDimY>(density).size() / 2];
    std::cout << "  * density[y:"
              << ddc::get_domain<DDimY>(density).size() / 2 << "] = {";
    ddc::for_each(
            ddc::get_domain<DDimX>(density),
            [=](ddc::DiscreteElement<DDimX> const ix) {
                std::cout << std::setw(6) << density_slice(ix) << " ";
            });
    std::cout << " }" << std::endl;
}
*/
//! [display]

template <typename NonUniform, std::size_t s_degree_x>
static void characteristics_advection()
{
    // some parameters that would typically be read from some form of
    // configuration file in a more realistic code

    //! [parameters]
    // Start of the domain of interest in the X dimension
    double const x_start = -1.;
    // End of the domain of interest in the X dimension
    double const x_end = 1.;
    // Number of discretization points in the X dimension
    size_t const nb_x_points = 1000;
    // Velocity along x dimension
    double const vx = .2;
    // Start of the domain of interest in the Y dimension
    double const y_start = -1.;
    // End of the domain of interest in the Y dimension
    double const y_end = 1.;
    // Number of discretization points in the Y dimension
    size_t const nb_y_points = 100000;
    // Simulated time at which to start simulation
    double const start_time = 0.;
    // Simulated time to reach as target of the simulation
    double const end_time = 1.0;
    // Number of time-steps between outputs
    ptrdiff_t const t_output_period = 0.1;
    // Maximum time-step
    ddc::Coordinate<T> const max_dt {0.1};
    //! [parameters]
    using _DDimX = DDimX<NonUniform, s_degree_x>;
    using _BSplinesX = BSplinesX<NonUniform, s_degree_x>;
    using _GrevillePoints = GrevillePoints<NonUniform, s_degree_x>;

    //! [main-start]

    //! [X-global-domain]
    // Initialization of the global domain in X
    if constexpr (!NonUniform::value) {
        ddc::init_discrete_space<_BSplinesX>(
                ddc::Coordinate<X>(x_start),
                ddc::Coordinate<X>(x_end),
                nb_x_points);
        std::cout << "Uniform spline degree " << s_degree_x << std::endl;
    } else {
        std::cout << "Non-uniform spline degree " << s_degree_x
                  << std::endl;
        std::vector<ddc::Coordinate<X>> breaks(nb_x_points + 1);
        for (std::size_t i(0); i < nb_x_points + 1; ++i) {
            breaks[i] = ddc::Coordinate<X>(
                    static_cast<double>(i) / nb_x_points);
        }
        ddc::init_discrete_space<_BSplinesX>(breaks);
    }

    ddc::init_discrete_space<_DDimX>(
            _GrevillePoints::template get_sampling<_DDimX>());

    auto const x_domain = _GrevillePoints::template get_domain<_DDimX>();
    //! [X-global-domain]
    // Initialization of the global domain in Y
    auto const y_domain
            = ddc::init_discrete_space<DDimY>(DDimY::init<DDimY>(
                    ddc::Coordinate<Y>(y_start),
                    ddc::Coordinate<Y>(y_end),
                    ddc::DiscreteVector<DDimY>(nb_y_points)));

    //! [time-domains]

    // number of time intervals required to reach the end time
    ddc::DiscreteVector<DDimT> const nb_time_steps {
            std::ceil((end_time - start_time) / max_dt) + .2};
    // Initialization of the global domain in time:
    // - the number of discrete time-points is equal to the number of
    //   steps + 1
    ddc::DiscreteDomain<DDimT> const time_domain
            = ddc::init_discrete_space<DDimT>(DDimT::init<DDimT>(
                    ddc::Coordinate<T>(start_time),
                    ddc::Coordinate<T>(end_time),
                    nb_time_steps + 1));
    //! [time-domains]

    //! [data allocation]
    // Maps density into the full domain twice:
    // - once for the last fully computed time-step
    ddc::Chunk last_density_alloc(
            ddc::DiscreteDomain<_DDimX, DDimY>(x_domain, y_domain),
            ddc::DeviceAllocator<double>());

    // - once for time-step being computed
    ddc::Chunk next_density_alloc(
            ddc::DiscreteDomain<_DDimX, DDimY>(x_domain, y_domain),
            ddc::DeviceAllocator<double>());
    //! [data allocation]

    //! [initial-conditions]
    ddc::ChunkSpan const initial_density
            = last_density_alloc.span_view();
    // Initialize the density on the main domain
    ddc::DiscreteDomain<_DDimX, DDimY> x_mesh
            = ddc::DiscreteDomain<_DDimX, DDimY>(x_domain, y_domain);
    ddc::parallel_for_each(
            x_mesh,
            KOKKOS_LAMBDA(
                    ddc::DiscreteElement<_DDimX, DDimY> const ixy) {
                double const x
                        = ddc::coordinate(ddc::select<_DDimX>(ixy));
                double const y
                        = ddc::coordinate(ddc::select<DDimY>(ixy));
                initial_density(ixy)
                        = 9.999
                          * Kokkos::exp(-(x * x + y * y) / 0.1 / 2);
                // initial_density(ixy) = 9.999 * ((x * x + y * y) < 0.25);
            });
    //! [initial-conditions]

    ddc::Chunk host_density_alloc(
            ddc::DiscreteDomain<_DDimX, DDimY>(x_domain, y_domain),
            ddc::HostAllocator<double>());


    //! [initial output]
    // display the initial data
    /*
    ddc::parallel_deepcopy(host_density_alloc, last_density_alloc);
    display(ddc::coordinate(time_domain.front()),
            host_density_alloc[x_domain][y_domain]);
    */
    // time of the iteration where the last output happened
    ddc::DiscreteElement<DDimT> last_output = time_domain.front();
    //! [initial output]

    //! [instantiate solver]
    ddc::SplineBuilder<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            _BSplinesX,
            _DDimX,
            BoundCond,
            BoundCond,
#if defined(SOLVER_LAPACK)
            ddc::SplineSolver::LAPACK,
#elif defined(SOLVER_GINKGO)
            ddc::SplineSolver::GINKGO,
#endif
            _DDimX,
            DDimY>
            spline_builder(x_mesh);
    ExtrapolationRule extrapolation_rule;
    ddc::SplineEvaluator<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            _BSplinesX,
            _DDimX,
            ExtrapolationRule,
            ExtrapolationRule,
            _DDimX,
            DDimY>
            spline_evaluator(extrapolation_rule, extrapolation_rule);
    //! [instantiate solver]

    //! [instantiate intermediate chunks]
    // Instantiate chunk of spline coefs to receive output of spline_builder
    ddc::Chunk coef_alloc(
            spline_builder.batched_spline_domain(),
            ddc::DeviceAllocator<double>());
    ddc::ChunkSpan coef = coef_alloc.span_view();

    // Instantiate chunk to receive feet coords
    ddc::Chunk feet_coords_alloc(
            spline_builder.batched_interpolation_domain(),
            ddc::DeviceAllocator<ddc::Coordinate<X>>());
    ddc::ChunkSpan feet_coords = feet_coords_alloc.span_view();
    //! [instantiate intermediate chunks]


    //! [time iteration]
    for (auto const iter :
         time_domain.remove_first(ddc::DiscreteVector<DDimT>(1))) {
        //! [time iteration]

        //! [manipulated views]
        // a span of the density at the time-step we
        // will build
        ddc::ChunkSpan const next_density {
                next_density_alloc.span_view()};
        // a read-only view of the density at the previous time-step
        ddc::ChunkSpan const last_density {
                last_density_alloc.span_view()};
        //! [manipulated views]

        //! [numerical scheme]
        // Stencil computation on the main domain
        // Find the coordinates of the characteristics feet
        ddc::parallel_for_each(
                feet_coords.domain(),
                KOKKOS_LAMBDA(
                        ddc::DiscreteElement<_DDimX, DDimY> const e) {
                    feet_coords(e)
                            = ddc::coordinate(ddc::select<_DDimX>(e))
                              - ddc::Coordinate<X>(
                                      vx * ddc::step<DDimT>());
                });
        // Interpolate the values at feets on the grid
        spline_builder(coef, last_density.span_cview());
        spline_evaluator(
                next_density,
                feet_coords.span_cview(),
                coef.span_cview());
        //! [numerical scheme]

        //! [output]
        /*
        if (iter - last_output >= t_output_period) {
            last_output = iter;
            ddc::parallel_deepcopy(
                    host_density_alloc,
                    last_density_alloc);
            display(ddc::coordinate(iter),
                    host_density_alloc[x_domain][y_domain]);
        }
        */
        //! [output]

        //! [swap]
        // Swap our two buffers
        std::swap(last_density_alloc, next_density_alloc);
        //! [swap]
    }

    //! [final output]
    /*
    if (last_output < time_domain.back()) {
        ddc::parallel_deepcopy(host_density_alloc, last_density_alloc);
        display(ddc::coordinate(time_domain.back()),
                host_density_alloc[x_domain][y_domain]);
    }
    */
    //! [final output]
}

//! [main-start]
int main(int argc, char** argv)
{
    Kokkos::ScopeGuard const kokkos_scope(argc, argv);
    ddc::ScopeGuard const ddc_scope(argc, argv);

    if (argc < 3) {
        std::cout << "Usage ./app <non-uniformity> <spline degree>"
                  << std::endl;
        return 0;
    }

    bool non_uniform = std::stoi(argv[1]);
    int spline_degree = std::stoi(argv[2]);
    if (non_uniform) {
        // Non-uniform mesh degree 3-5
        if (spline_degree == 3) {
            characteristics_advection<std::true_type, 3>();
        } else if (spline_degree == 4) {
            characteristics_advection<std::true_type, 4>();
        } else if (spline_degree == 5) {
            characteristics_advection<std::true_type, 5>();
        }
    } else {
        // Uniform mesh degree 3-5
        if (spline_degree == 3) {
            characteristics_advection<std::false_type, 3>();
        } else if (spline_degree == 4) {
            characteristics_advection<std::false_type, 4>();
        } else if (spline_degree == 5) {
            characteristics_advection<std::false_type, 5>();
        }
    }
}
