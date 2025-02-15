// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <iomanip>
#include <iostream>

#include <Kokkos_Core.hpp>

#include "view.hpp"

namespace ddc::detail {

/**
 * @brief The parent class for linear problems dedicated to the computation of spline approximations.
 *
 * Store a square matrix and provide method to solve a multiple right-hand sides linear problem.
 * Implementations may have different storage formats, filling methods and multiple right-hand sides linear solvers.
 */
template <class ExecSpace>
class SplinesLinearProblem
{
public:
    /// @brief The type of a Kokkos::View storing multiple right-hand sides.
    using MultiRHS = Kokkos::View<double**, Kokkos::LayoutRight, ExecSpace>;
    using AViewType
            = Kokkos::DualView<double**, Kokkos::LayoutRight, typename ExecSpace::memory_space>;
    using PivViewType = Kokkos::DualView<int*, typename ExecSpace::memory_space>;

    /**
     *  @brief COO storage.
     *  
     *  [SHOULD BE PRIVATE (GPU programming limitation)]
     */
    struct Coo
    {
        std::size_t m_nrows;
        std::size_t m_ncols;
        Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space> m_rows_idx;
        Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space> m_cols_idx;
        Kokkos::View<double*, Kokkos::LayoutRight, typename ExecSpace::memory_space> m_values;

        Coo() : m_nrows(0), m_ncols(0) {}

        Coo(std::size_t const nrows_,
            std::size_t const ncols_,
            Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space> rows_idx_,
            Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space> cols_idx_,
            Kokkos::View<double*, Kokkos::LayoutRight, typename ExecSpace::memory_space> values_)
            : m_nrows(nrows_)
            , m_ncols(ncols_)
            , m_rows_idx(std::move(rows_idx_))
            , m_cols_idx(std::move(cols_idx_))
            , m_values(std::move(values_))
        {
            assert(m_rows_idx.extent(0) == m_cols_idx.extent(0));
            assert(m_rows_idx.extent(0) == m_values.extent(0));
        }

        KOKKOS_FUNCTION std::size_t nnz() const
        {
            return m_values.extent(0);
        }

        KOKKOS_FUNCTION std::size_t nrows() const
        {
            return m_nrows;
        }

        KOKKOS_FUNCTION std::size_t ncols() const
        {
            return m_ncols;
        }

        KOKKOS_FUNCTION Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space>
        rows_idx() const
        {
            return m_rows_idx;
        }

        KOKKOS_FUNCTION Kokkos::View<int*, Kokkos::LayoutRight, typename ExecSpace::memory_space>
        cols_idx() const
        {
            return m_cols_idx;
        }

        KOKKOS_FUNCTION Kokkos::View<double*, Kokkos::LayoutRight, typename ExecSpace::memory_space>
        values() const
        {
            return m_values;
        }
    };

private:
    std::size_t m_size;

protected:
    AViewType m_a;
    PivViewType m_ipiv;

    explicit SplinesLinearProblem(const std::size_t size) : m_size(size) {}

public:
    /// @brief Destruct
    virtual ~SplinesLinearProblem() = default;

    /**
     * @brief Get an element of the matrix at indexes i, j. It must not be called after `setup_solver`.
     *
     * @param i The row index of the desired element.
     * @param j The column index of the desired element.
     *
     * @return The value of the element of the matrix.
     */
    virtual double get_element(std::size_t i, std::size_t j) const = 0;

    /**
     * @brief Set an element of the matrix at indexes i, j. It must not be called after `setup_solver`.
     *
     * @param i The row index of the setted element.
     * @param j The column index of the setted element.
     * @param aij The value to set in the element of the matrix.
     */
    virtual void set_element(std::size_t i, std::size_t j, double aij) = 0;

    /**
     * @brief Perform a pre-process operation on the solver. Must be called after filling the matrix.
     */
    virtual void setup_solver() = 0;

    /**
     * @brief Solve the multiple right-hand sides linear problem Ax=b or its transposed version A^tx=b inplace.
     *
     * @param[in, out] multi_rhs A 2D Kokkos::View storing the multiple right-hand sides of the problem and receiving the corresponding solution.
     * @param transpose Choose between the direct or transposed version of the linear problem.
     */
    virtual void solve(MultiRHS b, bool transpose = false) const = 0;

    virtual void solve(
            typename AViewType::t_dev top_right_block,
            typename AViewType::t_dev bottom_left_block,
            typename AViewType::t_dev bottom_right_block,
            typename PivViewType::t_dev bottom_right_piv,
            MultiRHS b,
            bool transpose = false) const = 0;

    virtual void solve(
            Coo top_right_block,
            Coo bottom_left_block,
            typename AViewType::t_dev bottom_right_block,
            typename PivViewType::t_dev bottom_right_piv,
            MultiRHS b,
            bool transpose = false) const = 0;

    /**
     * @brief Get the size of the square matrix in one of its dimensions.
     *
     * @return The size of the matrix in one of its dimensions.
     */
    std::size_t size() const
    {
        return m_size;
    }

    auto get_matrix() const
    {
        return m_a.d_view;
    }

    auto get_pivot() const
    {
        return m_ipiv.d_view;
    }

    /**
     * @brief Get the required number of rows of the multi-rhs view passed to solve().
     *
     * Implementations may require a number of rows larger than what `size` returns for optimization purposes.
     *
     * @return The required number of rows of the multi-rhs view. It is guaranteed to be greater or equal to `size`.
     */
    std::size_t required_number_of_rhs_rows() const
    {
        std::size_t const nrows = impl_required_number_of_rhs_rows();
        assert(nrows >= size());
        return nrows;
    }

private:
    virtual std::size_t impl_required_number_of_rhs_rows() const
    {
        return m_size;
    }
};

/**
 * @brief Prints the matrix of a SplinesLinearProblem in a std::ostream. It must not be called after `setup_solver`.
 *
 * @param[out] os The stream in which the matrix is printed.
 * @param[in] linear_problem The SplinesLinearProblem of the matrix to print.
 *
 * @return The stream in which the matrix is printed.
**/
template <class ExecSpace>
std::ostream& operator<<(std::ostream& os, SplinesLinearProblem<ExecSpace> const& linear_problem)
{
    std::size_t const n = linear_problem.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            os << std::fixed << std::setprecision(3) << std::setw(10)
               << linear_problem.get_element(i, j);
        }
        os << std::endl;
    }
    return os;
}

} // namespace ddc::detail
