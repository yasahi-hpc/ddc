// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#pragma once
#include <ddc/ddc.hpp>

#include "ddc/chunk_span.hpp"
#include "ddc/discrete_domain.hpp"
#include "ddc/kokkos_allocator.hpp"

#include "deriv.hpp"
#include "math_tools.hpp"
#include "spline_boundary_conditions.hpp"
#include "splines_linear_problem_maker.hpp"

namespace ddc {

/**
 * @brief An enum determining the backend solver of a SplineBuilder or SplineBuilder2d.
 *
 * An enum determining the backend solver of a SplineBuilder or SplineBuilder2d.
 */
enum class SplineSolver {
    GINKGO, ///< Enum member to identify the Ginkgo-based solver (iterative method)
    LAPACK ///< Enum member to identify the LAPACK-based solver (direct method)
};

/**
 * @brief A class for creating a spline approximation of a function.
 *
 * A class which contains an operator () which can be used to build a spline approximation
 * of a function. A spline approximation is represented by coefficients stored in a Chunk
 * of B-splines. The spline is constructed such that it respects the boundary conditions
 * BcLower and BcUpper, and it interpolates the function at the points on the interpolation_discrete_dimension
 * associated with interpolation_discrete_dimension_type.
 * @tparam ExecSpace The Kokkos execution space on which the spline approximation is performed.
 * @tparam MemorySpace The Kokkos memory space on which the data (interpolation function and splines coefficients) is stored.
 * @tparam BSplines The discrete dimension representing the B-splines.
 * @tparam InterpolationDDim The discrete dimension on which interpolation points are defined.
 * @tparam BcLower The lower boundary condition.
 * @tparam BcUpper The upper boundary condition.
 * @tparam Solver The SplineSolver giving the backend used to perform the spline approximation.
 * @tparam IDimX A variadic template of all the discrete dimensions forming the full space (InterpolationDDim + batched dimensions).
 */
template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
class SplineBuilder
{
    static_assert(
            (BSplines::is_periodic() && (BcLower == ddc::BoundCond::PERIODIC)
             && (BcUpper == ddc::BoundCond::PERIODIC))
            || (!BSplines::is_periodic() && (BcLower != ddc::BoundCond::PERIODIC)
                && (BcUpper != ddc::BoundCond::PERIODIC)));

public:
    /// @brief The type of the Kokkos execution space used by this class.
    using exec_space = ExecSpace;

    /// @brief The type of the Kokkos memory space used by this class.
    using memory_space = MemorySpace;

    /// @brief The type of the interpolation continuous dimension (continuous dimension of interest) used by this class.
    using continuous_dimension_type = typename InterpolationDDim::continuous_dimension_type;

    /// @brief The type of the interpolation discrete dimension (discrete dimension of interest) used by this class.
    using interpolation_discrete_dimension_type = InterpolationDDim;

    /// @brief The discrete dimension representing the B-splines.
    using bsplines_type = BSplines;

    /// @brief The type of the Deriv dimension at the boundaries.
    using deriv_type = ddc::Deriv<continuous_dimension_type>;

    /// @brief The type of the domain for the 1D interpolation mesh used by this class.
    using interpolation_domain_type = ddc::DiscreteDomain<interpolation_discrete_dimension_type>;

    /// @brief The type of the whole domain representing interpolation points.
    using batched_interpolation_domain_type = ddc::DiscreteDomain<IDimX...>;

    /**
     * @brief The type of the batch domain (obtained by removing the dimension of interest
     * from the whole domain).
     *
     * Example: For batched_interpolation_domain_type = DiscreteDomain<X,Y,Z> and a dimension of interest Y,
     * this is DiscreteDomain<X,Z>
     */
    using batch_domain_type =
            typename ddc::detail::convert_type_seq_to_discrete_domain<ddc::type_seq_remove_t<
                    ddc::detail::TypeSeq<IDimX...>,
                    ddc::detail::TypeSeq<interpolation_discrete_dimension_type>>>;

    /**
     * @brief The type of the whole spline domain (cartesian product of 1D spline domain
     * and batch domain) preserving the underlying memory layout (order of dimensions).
     *
     * Example: For batched_interpolation_domain_type = DiscreteDomain<X,Y,Z> and a dimension of interest Y
     * (associated to a B-splines tag BSplinesY), this is DiscreteDomain<X,BSplinesY,Z>.
     */
    using batched_spline_domain_type =
            typename ddc::detail::convert_type_seq_to_discrete_domain<ddc::type_seq_replace_t<
                    ddc::detail::TypeSeq<IDimX...>,
                    ddc::detail::TypeSeq<interpolation_discrete_dimension_type>,
                    ddc::detail::TypeSeq<bsplines_type>>>;

private:
    /**
     * @brief The type of the whole spline domain (cartesian product of the 1D spline domain
     * and the batch domain) with 1D spline dimension being the leading dimension.
     *
     * Example: For batched_interpolation_domain_type = DiscreteDomain<X,Y,Z> and a dimension of interest Y
     * (associated to a B-splines tag BSplinesY), this is DiscreteDomain<BSplinesY,X,Z>.
     */
    using batched_spline_tr_domain_type =
            typename ddc::detail::convert_type_seq_to_discrete_domain<ddc::type_seq_merge_t<
                    ddc::detail::TypeSeq<bsplines_type>,
                    ddc::type_seq_remove_t<
                            ddc::detail::TypeSeq<IDimX...>,
                            ddc::detail::TypeSeq<interpolation_discrete_dimension_type>>>>;

public:
    /**
     * @brief The type of the whole Deriv domain (cartesian product of 1D Deriv domain 
     * and batch domain) preserving the underlying memory layout (order of dimensions).
     *
     * Example: For batched_interpolation_domain_type = DiscreteDomain<X,Y,Z> and a dimension of interest Y,
     * this is DiscreteDomain<X,Deriv<Y>,Z>
     */
    using batched_derivs_domain_type =
            typename ddc::detail::convert_type_seq_to_discrete_domain<ddc::type_seq_replace_t<
                    ddc::detail::TypeSeq<IDimX...>,
                    ddc::detail::TypeSeq<interpolation_discrete_dimension_type>,
                    ddc::detail::TypeSeq<deriv_type>>>;

    /// @brief Indicates if the degree of the splines is odd or even.
    static constexpr bool s_odd = BSplines::degree() % 2;

    /// @brief The number of equations defining the boundary condition at the lower bound.
    static constexpr int s_nbc_xmin = n_boundary_equations(BcLower, BSplines::degree());

    /// @brief The number of equations defining the boundary condition at the upper bound.
    static constexpr int s_nbc_xmax = n_boundary_equations(BcUpper, BSplines::degree());

    /// @brief The boundary condition implemented at the lower bound.
    static constexpr ddc::BoundCond s_bc_xmin = BcLower;

    /// @brief The boundary condition implemented at the upper bound.
    static constexpr ddc::BoundCond s_bc_xmax = BcUpper;

private:
    batched_interpolation_domain_type m_batched_interpolation_domain;

    int m_offset;

    double m_dx; // average cell size for normalization of derivatives

    // interpolator specific
    std::unique_ptr<ddc::detail::SplinesLinearProblem<exec_space>> matrix;

    /// Calculate offset so that the matrix is diagonally dominant
    int compute_offset(interpolation_domain_type const& interpolation_domain);

public:
    /**
     * @brief Build a SplineBuilder acting on batched_interpolation_domain.
     * 
     * @param batched_interpolation_domain The domain on which the interpolation points are defined.
     *
     * @param cols_per_chunk A parameter used by the slicer (internal to the solver) to define the size
     * of a chunk of right-hand sides of the linear problem to be computed in parallel (chunks are treated
     * by the linear solver one-after-the-other).
     * This value is optional. If no value is provided then the default value is chosen by the requested solver.
     *
     * @param preconditioner_max_block_size A parameter used by the slicer (internal to the solver) to
     * define the size of a block used by the Block-Jacobi preconditioner.
     * This value is optional. If no value is provided then the default value is chosen by the requested solver.
     *
     * @see MatrixSparse
     */
    explicit SplineBuilder(
            batched_interpolation_domain_type const& batched_interpolation_domain,
            std::optional<std::size_t> cols_per_chunk = std::nullopt,
            std::optional<unsigned int> preconditioner_max_block_size = std::nullopt)
        : m_batched_interpolation_domain(batched_interpolation_domain)
        , m_offset(compute_offset(interpolation_domain()))
        , m_dx((ddc::discrete_space<BSplines>().rmax() - ddc::discrete_space<BSplines>().rmin())
               / ddc::discrete_space<BSplines>().ncells())
    {
        static_assert(
                ((BcLower == BoundCond::PERIODIC) == (BcUpper == BoundCond::PERIODIC)),
                "Incompatible boundary conditions");

        // Calculate block sizes
        int lower_block_size, upper_block_size;
        if constexpr (bsplines_type::is_uniform()) {
            compute_block_sizes_uniform(lower_block_size, upper_block_size);
        } else {
            compute_block_sizes_non_uniform(lower_block_size, upper_block_size);
        }
        allocate_matrix(
                lower_block_size,
                upper_block_size,
                cols_per_chunk,
                preconditioner_max_block_size);
    }

    /// @brief Copy-constructor is deleted.
    SplineBuilder(SplineBuilder const& x) = delete;

    /** @brief Move-constructs.
     *
     * @param x An rvalue to another SplineBuilder.
     */
    SplineBuilder(SplineBuilder&& x) = default;

    /// @brief Destructs.
    ~SplineBuilder() = default;

    /// @brief Copy-assignment is deleted.
    SplineBuilder& operator=(SplineBuilder const& x) = delete;

    /** @brief Move-assigns.
     *
     * @param x An rvalue to another SplineBuilder.
     * @return A reference to this object.
     */
    SplineBuilder& operator=(SplineBuilder&& x) = default;

    /**
     * @brief Get the domain for the 1D interpolation mesh used by this class.
     *
     * This is 1D because it is defined along the dimension of interest.
     *
     * @return The 1D domain for the interpolation mesh.
     */
    interpolation_domain_type interpolation_domain() const noexcept
    {
        return interpolation_domain_type(m_batched_interpolation_domain);
    }

    /**
     * @brief Get the whole domain representing interpolation points.
     *
     * Values of the function must be provided on this domain in order
     * to build a spline representation of the function (cartesian product of 1D interpolation_domain and batch_domain).
     *
     * @return The domain for the interpolation mesh.
     */
    batched_interpolation_domain_type batched_interpolation_domain() const noexcept
    {
        return m_batched_interpolation_domain;
    }

    /**
     * @brief Get the batch domain.
     *
     * Obtained by removing the dimension of interest from the whole interpolation domain.
     *
     * @return The batch domain.
     */
    batch_domain_type batch_domain() const noexcept
    {
        return ddc::remove_dims_of(batched_interpolation_domain(), interpolation_domain());
    }

    /**
     * @brief Get the 1D domain on which spline coefficients are defined.
     *
     * The 1D spline domain corresponding to the dimension of interest.
     *
     * @return The 1D domain for the spline coefficients.
     */
    ddc::DiscreteDomain<bsplines_type> spline_domain() const noexcept
    {
        return ddc::discrete_space<bsplines_type>().full_domain();
    }

    /**
     * @brief Get the whole domain on which spline coefficients are defined.
     *
     * Spline approximations (spline-transformed functions) are computed on this domain.
     *
     * @return The domain for the spline coefficients.
     */
    batched_spline_domain_type batched_spline_domain() const noexcept
    {
        return ddc::replace_dim_of<
                interpolation_discrete_dimension_type,
                bsplines_type>(batched_interpolation_domain(), spline_domain());
    }

private:
    /**
     * @brief Get the whole domain on which spline coefficients are defined, with the dimension of interest being the leading dimension.
     *
     * This is used internally due to solver limitation and because it may be beneficial to computation performance. For LAPACK backend and non-periodic boundary condition, we are using SplinesLinearSolver3x3Blocks which requires upper_block_size additional rows for internal operations.
     *
     * @return The (transposed) domain for the spline coefficients.
     */
    batched_spline_tr_domain_type batched_spline_tr_domain() const noexcept
    {
        return batched_spline_tr_domain_type(ddc::replace_dim_of<bsplines_type, bsplines_type>(
                batched_spline_domain(),
                ddc::DiscreteDomain<bsplines_type>(
                        ddc::DiscreteElement<bsplines_type>(0),
                        ddc::DiscreteVector<bsplines_type>(
                                matrix->required_number_of_rhs_rows()))));
    }

public:
    /**
     * @brief Get the whole domain on which derivatives on lower boundary are defined.
     *
     * This is only used with BoundCond::HERMITE boundary conditions.
     *
     * @return The domain for the Derivs values.
     */
    batched_derivs_domain_type batched_derivs_xmin_domain() const noexcept
    {
        return ddc::replace_dim_of<interpolation_discrete_dimension_type, deriv_type>(
                batched_interpolation_domain(),
                ddc::DiscreteDomain<deriv_type>(
                        ddc::DiscreteElement<deriv_type>(1),
                        ddc::DiscreteVector<deriv_type>(s_nbc_xmin)));
    }

    /**
     * @brief Get the whole domain on which derivatives on upper boundary are defined.
     *
     * This is only used with BoundCond::HERMITE boundary conditions.
     *
     * @return The domain for the Derivs values.
     */
    batched_derivs_domain_type batched_derivs_xmax_domain() const noexcept
    {
        return ddc::replace_dim_of<interpolation_discrete_dimension_type, deriv_type>(
                batched_interpolation_domain(),
                ddc::DiscreteDomain<deriv_type>(
                        ddc::DiscreteElement<deriv_type>(1),
                        ddc::DiscreteVector<deriv_type>(s_nbc_xmax)));
    }

    /**
     * @brief Get the interpolation matrix.
     *
     * This can be useful for debugging (as it allows
     * one to print the matrix) or for more complex quadrature schemes.
     *
     * Warning: the returned detail::Matrix class is not supposed to be exposed
     * to user, which means its usage is not supported out of the scope of current class.
     * Use at your own risk.
     *
     * @return A reference to the interpolation matrix.
     */
    const ddc::detail::SplinesLinearProblem<exec_space>& get_interpolation_matrix() const noexcept
    {
        return *matrix;
    }

    /**
     * @brief Compute a spline approximation of a function.
     *
     * Use the values of a function (defined on
     * SplineBuilder::batched_interpolation_domain) and the derivatives of the
     * function at the boundaries (in the case of BoundCond::HERMITE only, defined 
     * on SplineBuilder::batched_derivs_xmin_domain and SplineBuilder::batched_derivs_xmax_domain) 
     * to calculate a spline approximation of this function.
     *
     * The spline approximation is stored as a ChunkSpan of coefficients
     * associated with B-splines.
     *
     * @param[out] spline The coefficients of the spline computed by this SplineBuilder.
     * @param[in] vals The values of the function on the interpolation mesh.
     * @param[in] derivs_xmin The values of the derivatives at the lower boundary
     * (used only with BoundCond::HERMITE lower boundary condition).
     * @param[in] derivs_xmax The values of the derivatives at the upper boundary
     * (used only with BoundCond::HERMITE upper boundary condition).
     */
    template <class Layout>
    void operator()(
            ddc::ChunkSpan<double, batched_spline_domain_type, Layout, memory_space> spline,
            ddc::ChunkSpan<double const, batched_interpolation_domain_type, Layout, memory_space>
                    vals,
            std::optional<
                    ddc::ChunkSpan<double const, batched_derivs_domain_type, Layout, memory_space>>
                    derivs_xmin
            = std::nullopt,
            std::optional<
                    ddc::ChunkSpan<double const, batched_derivs_domain_type, Layout, memory_space>>
                    derivs_xmax
            = std::nullopt) const;

private:
    void compute_block_sizes_uniform(int& lower_block_size, int& upper_block_size) const;

    void compute_block_sizes_non_uniform(int& lower_block_size, int& upper_block_size) const;

    void allocate_matrix(
            int lower_block_size,
            int upper_block_size,
            std::optional<std::size_t> cols_per_chunk = std::nullopt,
            std::optional<unsigned int> preconditioner_max_block_size = std::nullopt);

    void build_matrix_system();
};

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
int SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::compute_offset(interpolation_domain_type const& interpolation_domain)
{
    int offset;
    if constexpr (bsplines_type::is_periodic()) {
        // Calculate offset so that the matrix is diagonally dominant
        std::array<double, bsplines_type::degree() + 1> values_ptr;
        std::experimental::mdspan<
                double,
                std::experimental::extents<std::size_t, bsplines_type::degree() + 1>> const
                values(values_ptr.data());
        ddc::DiscreteElement<interpolation_discrete_dimension_type> start(
                interpolation_domain.front());
        auto jmin = ddc::discrete_space<BSplines>()
                            .eval_basis(values, ddc::coordinate(start + BSplines::degree()));
        if constexpr (bsplines_type::degree() % 2 == 0) {
            offset = jmin.uid() - start.uid() + bsplines_type::degree() / 2 - BSplines::degree();
        } else {
            int const mid = bsplines_type::degree() / 2;
            offset = jmin.uid() - start.uid() + (values(mid) > values(mid + 1) ? mid : mid + 1)
                     - BSplines::degree();
        }
    } else {
        offset = 0;
    }
    return offset;
}

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
void SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::compute_block_sizes_uniform(int& lower_block_size, int& upper_block_size) const
{
    switch (BcLower) {
    case ddc::BoundCond::PERIODIC:
        upper_block_size = (bsplines_type::degree()) / 2;
        break;
    case ddc::BoundCond::HERMITE:
        upper_block_size = s_nbc_xmin;
        break;
    case ddc::BoundCond::GREVILLE:
        upper_block_size = bsplines_type::degree() - 1;
        break;
    default:
        throw std::runtime_error("ddc::BoundCond not handled");
    }
    switch (BcUpper) {
    case ddc::BoundCond::PERIODIC:
        lower_block_size = (bsplines_type::degree()) / 2;
        break;
    case ddc::BoundCond::HERMITE:
        lower_block_size = s_nbc_xmax;
        break;
    case ddc::BoundCond::GREVILLE:
        lower_block_size = bsplines_type::degree() - 1;
        break;
    default:
        throw std::runtime_error("ddc::BoundCond not handled");
    }
}

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
void SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::compute_block_sizes_non_uniform(int& lower_block_size, int& upper_block_size)
        const
{
    switch (BcLower) {
    case ddc::BoundCond::PERIODIC:
        upper_block_size = bsplines_type::degree() - 1;
        break;
    case ddc::BoundCond::HERMITE:
        upper_block_size = s_nbc_xmin + 1;
        break;
    case ddc::BoundCond::GREVILLE:
        upper_block_size = bsplines_type::degree() - 1;
        break;
    default:
        throw std::runtime_error("ddc::BoundCond not handled");
    }
    switch (BcUpper) {
    case ddc::BoundCond::PERIODIC:
        lower_block_size = bsplines_type::degree() - 1;
        break;
    case ddc::BoundCond::HERMITE:
        lower_block_size = s_nbc_xmax + 1;
        break;
    case ddc::BoundCond::GREVILLE:
        lower_block_size = bsplines_type::degree() - 1;
        break;
    default:
        throw std::runtime_error("ddc::BoundCond not handled");
    }
}

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
void SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::
        allocate_matrix(
                [[maybe_unused]] int lower_block_size,
                [[maybe_unused]] int upper_block_size,
                std::optional<std::size_t> cols_per_chunk,
                std::optional<unsigned int> preconditioner_max_block_size)
{
    // Special case: linear spline
    // No need for matrix assembly
    // (desactivated)
    /*
    if constexpr (bsplines_type::degree() == 1)
        return;
	*/

    if constexpr (Solver == ddc::SplineSolver::LAPACK) {
        int upper_band_width;
        if (bsplines_type::is_uniform()) {
            upper_band_width = bsplines_type::degree() / 2;
        } else {
            upper_band_width = bsplines_type::degree() - 1;
        }
        if constexpr (bsplines_type::is_periodic()) {
            matrix = ddc::detail::SplinesLinearProblemMaker::make_new_periodic_band_matrix<
                    ExecSpace>(
                    ddc::discrete_space<BSplines>().nbasis(),
                    upper_band_width,
                    upper_band_width,
                    bsplines_type::is_uniform());
        } else {
            matrix = ddc::detail::SplinesLinearProblemMaker::
                    make_new_block_matrix_with_band_main_block<ExecSpace>(
                            ddc::discrete_space<BSplines>().nbasis(),
                            upper_band_width,
                            upper_band_width,
                            bsplines_type::is_uniform(),
                            lower_block_size,
                            upper_block_size);
        }
    } else if constexpr (Solver == ddc::SplineSolver::GINKGO) {
        matrix = ddc::detail::SplinesLinearProblemMaker::make_new_sparse<ExecSpace>(
                ddc::discrete_space<BSplines>().nbasis(),
                cols_per_chunk,
                preconditioner_max_block_size);
    }

    build_matrix_system();

    matrix->setup_solver();
}

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
void SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::build_matrix_system()
{
    // Hermite boundary conditions at xmin, if any
    if constexpr (BcLower == ddc::BoundCond::HERMITE) {
        std::array<double, (bsplines_type::degree() / 2 + 1) * (bsplines_type::degree() + 1)>
                derivs_ptr;
        ddc::DSpan2D
                derivs(derivs_ptr.data(),
                       bsplines_type::degree() + 1,
                       bsplines_type::degree() / 2 + 1);
        ddc::discrete_space<BSplines>().eval_basis_and_n_derivs(
                derivs,
                ddc::discrete_space<BSplines>().rmin(),
                s_nbc_xmin);

        // In order to improve the condition number of the matrix, we normalize
        // all derivatives by multiplying the i-th derivative by dx^i
        for (std::size_t i = 0; i < bsplines_type::degree() + 1; ++i) {
            for (std::size_t j = 1; j < bsplines_type::degree() / 2 + 1; ++j) {
                derivs(i, j) *= ddc::detail::ipow(m_dx, j);
            }
        }

        // iterate only to deg as last bspline is 0
        for (std::size_t i = 0; i < s_nbc_xmin; ++i) {
            for (std::size_t j = 0; j < bsplines_type::degree(); ++j) {
                matrix->set_element(i, j, derivs(j, s_nbc_xmin - i - 1 + s_odd));
            }
        }
    }

    // Interpolation points
    std::array<double, bsplines_type::degree() + 1> values_ptr;
    std::experimental::mdspan<
            double,
            std::experimental::extents<std::size_t, bsplines_type::degree() + 1>> const
            values(values_ptr.data());

    int start = interpolation_domain().front().uid();
    ddc::for_each(interpolation_domain(), [&](auto ix) {
        auto jmin = ddc::discrete_space<BSplines>().eval_basis(
                values,
                ddc::coordinate(ddc::DiscreteElement<interpolation_discrete_dimension_type>(ix)));
        for (std::size_t s = 0; s < bsplines_type::degree() + 1; ++s) {
            int const j = ddc::detail::
                    modulo(int(jmin.uid() - m_offset + s),
                           static_cast<int>(ddc::discrete_space<BSplines>().nbasis()));
            matrix->set_element(ix.uid() - start + s_nbc_xmin, j, values(s));
        }
    });

    // Hermite boundary conditions at xmax, if any
    if constexpr (BcUpper == ddc::BoundCond::HERMITE) {
        std::array<double, (bsplines_type::degree() / 2 + 1) * (bsplines_type::degree() + 1)>
                derivs_ptr;
        std::experimental::mdspan<
                double,
                std::experimental::extents<
                        std::size_t,
                        bsplines_type::degree() + 1,
                        bsplines_type::degree() / 2 + 1>> const derivs(derivs_ptr.data());

        ddc::discrete_space<BSplines>().eval_basis_and_n_derivs(
                derivs,
                ddc::discrete_space<BSplines>().rmax(),
                s_nbc_xmax);

        // In order to improve the condition number of the matrix, we normalize
        // all derivatives by multiplying the i-th derivative by dx^i
        for (std::size_t i = 0; i < bsplines_type::degree() + 1; ++i) {
            for (std::size_t j = 1; j < bsplines_type::degree() / 2 + 1; ++j) {
                derivs(i, j) *= ddc::detail::ipow(m_dx, j);
            }
        }

        int const i0 = ddc::discrete_space<BSplines>().nbasis() - s_nbc_xmax;
        int const j0 = ddc::discrete_space<BSplines>().nbasis() - bsplines_type::degree();
        for (std::size_t j = 0; j < bsplines_type::degree(); ++j) {
            for (std::size_t i = 0; i < s_nbc_xmax; ++i) {
                matrix->set_element(i0 + i, j0 + j, derivs(j + 1, i + s_odd));
            }
        }
    }
}

template <
        class ExecSpace,
        class MemorySpace,
        class BSplines,
        class InterpolationDDim,
        ddc::BoundCond BcLower,
        ddc::BoundCond BcUpper,
        SplineSolver Solver,
        class... IDimX>
template <class Layout>
void SplineBuilder<
        ExecSpace,
        MemorySpace,
        BSplines,
        InterpolationDDim,
        BcLower,
        BcUpper,
        Solver,
        IDimX...>::
operator()(
        ddc::ChunkSpan<double, batched_spline_domain_type, Layout, memory_space> spline,
        ddc::ChunkSpan<double const, batched_interpolation_domain_type, Layout, memory_space> vals,
        std::optional<ddc::ChunkSpan<
                double const,
                batched_derivs_domain_type,
                Layout,
                memory_space>> const derivs_xmin,
        std::optional<ddc::ChunkSpan<
                double const,
                batched_derivs_domain_type,
                Layout,
                memory_space>> const derivs_xmax) const
{
    assert(vals.template extent<interpolation_discrete_dimension_type>()
           == ddc::discrete_space<bsplines_type>().nbasis() - s_nbc_xmin - s_nbc_xmax);

    assert((BcLower == ddc::BoundCond::HERMITE)
           != (!derivs_xmin.has_value() || derivs_xmin->template extent<deriv_type>() == 0));
    assert((BcUpper == ddc::BoundCond::HERMITE)
           != (!derivs_xmax.has_value() || derivs_xmax->template extent<deriv_type>() == 0));
    if constexpr (BcLower == BoundCond::HERMITE) {
        assert(ddc::DiscreteElement<deriv_type>(derivs_xmin->domain().front()).uid() == 1);
    }
    if constexpr (BcUpper == BoundCond::HERMITE) {
        assert(ddc::DiscreteElement<deriv_type>(derivs_xmax->domain().front()).uid() == 1);
    }

    // Hermite boundary conditions at xmin, if any
    // NOTE: For consistency with the linear system, the i-th derivative
    //       provided by the user must be multiplied by dx^i
    if constexpr (BcLower == BoundCond::HERMITE) {
        assert(derivs_xmin->template extent<deriv_type>() == s_nbc_xmin);
        auto derivs_xmin_values = *derivs_xmin;
        auto const dx_proxy = m_dx;
        ddc::parallel_for_each(
                "ddc_splines_hermite_compute_lower_coefficients",
                exec_space(),
                batch_domain(),
                KOKKOS_LAMBDA(typename batch_domain_type::discrete_element_type j) {
                    for (int i = s_nbc_xmin; i > 0; --i) {
                        spline(ddc::DiscreteElement<bsplines_type>(s_nbc_xmin - i), j)
                                = derivs_xmin_values(ddc::DiscreteElement<deriv_type>(i), j)
                                  * ddc::detail::ipow(dx_proxy, i + s_odd - 1);
                    }
                });
    }

    // Fill spline with vals (to work in spline afterward and preserve vals)
    ddc::parallel_fill(
            exec_space(),
            spline[ddc::DiscreteDomain<bsplines_type>(
                    ddc::DiscreteElement<bsplines_type>(s_nbc_xmin),
                    ddc::DiscreteVector<bsplines_type>(m_offset))],
            0.);
    // NOTE: We rely on Kokkos::deep_copy because ddc::parallel_deepcopy do not support
    //       different domain-typed Chunks.
    Kokkos::deep_copy(
            exec_space(),
            spline[ddc::DiscreteDomain<bsplines_type>(
                           ddc::DiscreteElement<bsplines_type>(s_nbc_xmin + m_offset),
                           ddc::DiscreteVector<bsplines_type>(static_cast<std::size_t>(
                                   vals.domain()
                                           .template extent<
                                                   interpolation_discrete_dimension_type>())))]
                    .allocation_kokkos_view(),
            vals.allocation_kokkos_view());



    // Hermite boundary conditions at xmax, if any
    // NOTE: For consistency with the linear system, the i-th derivative
    //       provided by the user must be multiplied by dx^i
    auto const& nbasis_proxy = ddc::discrete_space<bsplines_type>().nbasis();
    if constexpr (BcUpper == BoundCond::HERMITE) {
        assert(derivs_xmax->template extent<deriv_type>() == s_nbc_xmax);
        auto derivs_xmax_values = *derivs_xmax;
        auto const dx_proxy = m_dx;
        ddc::parallel_for_each(
                "ddc_splines_hermite_compute_upper_coefficients",
                exec_space(),
                batch_domain(),
                KOKKOS_LAMBDA(typename batch_domain_type::discrete_element_type j) {
                    for (int i = 0; i < s_nbc_xmax; ++i) {
                        spline(ddc::DiscreteElement<bsplines_type>(nbasis_proxy - s_nbc_xmax - i),
                               j)
                                = derivs_xmax_values(ddc::DiscreteElement<deriv_type>(i + 1), j)
                                  * ddc::detail::ipow(dx_proxy, i + s_odd);
                    }
                });
    }

    // Allocate and fill a transposed version of spline in order to get dimension of interest as last dimension (optimal for GPU, necessary for Ginkgo). Also select only relevant rows in case of periodic boundaries
    auto const& offset_proxy = m_offset;
    ddc::Chunk spline_tr_alloc(
            batched_spline_tr_domain(),
            ddc::KokkosAllocator<double, memory_space>());
    ddc::ChunkSpan spline_tr = spline_tr_alloc.span_view();
    ddc::parallel_for_each(
            "ddc_splines_transpose_rhs",
            exec_space(),
            batch_domain(),
            KOKKOS_LAMBDA(typename batch_domain_type::discrete_element_type const j) {
                for (std::size_t i = 0; i < nbasis_proxy; i++) {
                    spline_tr(ddc::DiscreteElement<bsplines_type>(i), j)
                            = spline(ddc::DiscreteElement<bsplines_type>(i + offset_proxy), j);
                }
            });
    // Create a 2D Kokkos::View to manage spline_tr as a matrix
    Kokkos::View<double**, Kokkos::LayoutRight, exec_space> bcoef_section(
            spline_tr.data_handle(),
            static_cast<std::size_t>(spline_tr.template extent<bsplines_type>()),
            batch_domain().size());
    // Compute spline coef
    matrix->solve(bcoef_section);
    // Transpose back spline_tr into spline.
    ddc::parallel_for_each(
            "ddc_splines_transpose_back_rhs",
            exec_space(),
            batch_domain(),
            KOKKOS_LAMBDA(typename batch_domain_type::discrete_element_type const j) {
                for (std::size_t i = 0; i < nbasis_proxy; i++) {
                    spline(ddc::DiscreteElement<bsplines_type>(i + offset_proxy), j)
                            = spline_tr(ddc::DiscreteElement<bsplines_type>(i), j);
                }
            });

    // Duplicate the lower spline coefficients to the upper side in case of periodic boundaries
    if (bsplines_type::is_periodic()) {
        ddc::parallel_for_each(
                "ddc_splines_periodic_rows_duplicate_rhs",
                exec_space(),
                batch_domain(),
                KOKKOS_LAMBDA(typename batch_domain_type::discrete_element_type const j) {
                    if (offset_proxy != 0) {
                        for (int i = 0; i < offset_proxy; ++i) {
                            spline(ddc::DiscreteElement<bsplines_type>(i), j) = spline(
                                    ddc::DiscreteElement<bsplines_type>(nbasis_proxy + i),
                                    j);
                        }
                        for (std::size_t i = offset_proxy; i < bsplines_type::degree(); ++i) {
                            spline(ddc::DiscreteElement<bsplines_type>(nbasis_proxy + i), j)
                                    = spline(ddc::DiscreteElement<bsplines_type>(i), j);
                        }
                    }
                    for (std::size_t i(0); i < bsplines_type::degree(); ++i) {
                        const ddc::DiscreteElement<bsplines_type> i_start(i);
                        const ddc::DiscreteElement<bsplines_type> i_end(nbasis_proxy + i);

                        spline(i_end, j) = spline(i_start, j);
                    }
                });
    }
}
} // namespace ddc
