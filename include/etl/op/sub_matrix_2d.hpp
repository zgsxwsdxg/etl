//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file
 * \brief Contains the sub_matrix_2d implementation
 */

#pragma once

#ifdef ETL_CUDA
#include "etl/impl/cublas/cuda.hpp"
#endif

namespace etl {

/*!
 * \brief View that shows a 2D sub matrix of an expression
 * \tparam T The type of expression on which the view is made
 */
template <typename T, bool Aligned, typename Enable>
struct sub_matrix_2d;

/*!
 * \brief View that shows a 2D sub matrix of an expression
 * \tparam T The type of expression on which the view is made
 */
template <typename T, bool Aligned>
struct sub_matrix_2d <T, Aligned, std::enable_if_t<true>> final :
    iterable<sub_matrix_2d<T, Aligned>, false>,
    assignable<sub_matrix_2d<T, Aligned>, value_t<T>>,
    value_testable<sub_matrix_2d<T, Aligned>>,
    inplace_assignable<sub_matrix_2d<T, Aligned>>
{
    static_assert(is_etl_expr<T>::value, "sub_matrix_2d<T> only works with ETL expressions");

    using this_type            = sub_matrix_2d<T, Aligned>;                                            ///< The type of this expression
    using iterable_base_type   = iterable<this_type, false>;                                           ///< The iterable base type
    using assignable_base_type = assignable<this_type, value_t<T>>;                                    ///< The iterable base type
    using sub_type             = T;                                                                    ///< The sub type
    using value_type           = value_t<sub_type>;                                                    ///< The value contained in the expression
    using memory_type          = memory_t<sub_type>;                                                   ///< The memory acess type
    using const_memory_type    = const_memory_t<sub_type>;                                             ///< The const memory access type
    using return_type          = return_helper<sub_type, decltype(std::declval<sub_type>()[0])>;       ///< The type returned by the view
    using const_return_type    = const_return_helper<sub_type, decltype(std::declval<sub_type>()[0])>; ///< The const type return by the view
    using iterator             = etl::iterator<this_type>;                                             ///< The iterator type
    using const_iterator       = etl::iterator<const this_type>;                                       ///< The const iterator type

    /*!
     * \brief The vectorization type for V
     */
    template<typename V = default_vec>
    using vec_type               = typename V::template vec_type<value_type>;

    using assignable_base_type::operator=;
    using iterable_base_type::begin;
    using iterable_base_type::end;

private:
    sub_type sub_expr;   ///< The Sub expression
    const size_t base_i; ///< The first index
    const size_t base_j; ///< The second index
    const size_t m;      ///< The first dimension
    const size_t n;      ///< The second dimension
    const size_t base_m; ///< The first dimension of the real matrix
    const size_t base_n; ///< The second dimension of the real matrix

    friend struct etl_traits<sub_matrix_2d>;

    static constexpr order storage_order = decay_traits<sub_type>::storage_order; ///< The storage order

public:
    /*!
     * \brief Construct a new sub_matrix_2d over the given sub expression
     * \param sub_expr The sub expression
     * \param i The sub index
     */
    sub_matrix_2d(sub_type sub_expr, size_t i, size_t j, size_t m, size_t n)
            : sub_expr(sub_expr), base_i(i), base_j(j), m(m), n(n), base_m(etl::dim<0>(sub_expr)), base_n(etl::dim<1>(sub_expr)) {}

    /*!
     * \brief Returns the element at the given index
     * \param j The index
     * \return a reference to the element at the given index.
     */
    const_return_type operator[](size_t j) const {
        cpp_assert(j < m * n, "Invalid index inside sub_matrix_2d");

        if /*constexpr*/ (storage_order == order::RowMajor){
            auto ii = base_i + j / n;
            auto jj = base_j + j % n;

            return sub_expr[ii * base_n + jj];
        } else {
            auto ii = base_i + j % m;
            auto jj = base_j + j / m;

            return sub_expr[ii + jj * base_m];
        }
    }

    /*!
     * \brief Returns the element at the given index
     * \param j The index
     * \return a reference to the element at the given index.
     */
    return_type operator[](size_t j) {
        cpp_assert(j < m * n, "Invalid index inside sub_matrix_2d");

        if /*constexpr*/ (storage_order == order::RowMajor){
            auto ii = base_i + j / n;
            auto jj = base_j + j % n;

            return sub_expr[ii * base_n + jj];
        } else {
            auto ii = base_i + j % m;
            auto jj = base_j + j / m;

            return sub_expr[ii + jj * base_m];
        }
    }

    /*!
     * \brief Returns the value at the given index
     * This function never has side effects.
     * \param j The index
     * \return the value at the given index.
     */
    value_type read_flat(size_t j) const noexcept {
        cpp_assert(j < m * n, "Invalid index inside sub_matrix_2d");

        if /*constexpr*/ (storage_order == order::RowMajor){
            auto ii = base_i + j / n;
            auto jj = base_j + j % n;

            return sub_expr.read_flat(ii * base_n + jj);
        } else {
            auto ii = base_i + j % m;
            auto jj = base_j + j / m;

            return sub_expr.read_flat(ii + jj * base_m);
        }
    }

    /*!
     * \brief Access to the element at the given (args...) position
     * \param i The first index
     * \param j The second index
     * \return a reference to the element at the given position.
     */
    ETL_STRONG_INLINE(const_return_type) operator()(size_t i, size_t j) const {
        cpp_assert(i < m, "Invalid 2D index inside sub_matrix_2d");
        cpp_assert(j < n, "Invalid 2D index inside sub_matrix_2d");

        return sub_expr(base_i + i, base_j + j);
    }

    /*!
     * \brief Access to the element at the given (args...) position
     * \param i The first index
     * \param j The second index
     * \return a reference to the element at the given position.
     */
    ETL_STRONG_INLINE(return_type) operator()(size_t i, size_t j) {
        cpp_assert(i < m, "Invalid 2D index inside sub_matrix_2d");
        cpp_assert(j < n, "Invalid 2D index inside sub_matrix_2d");

        return sub_expr(base_i + i, base_j + j);
    }

    /*!
     * \brief Creates a sub view of the matrix, effectively removing the first dimension and fixing it to the given index.
     * \param x The index to use
     * \return a sub view of the matrix at position x.
     */
    auto operator()(size_t x) const {
        return sub(*this, x);
    }

    /*!
     * \brief Test if this expression aliases with the given expression
     * \param rhs The other expression to test
     * \return true if the two expressions aliases, false otherwise
     */
    template <typename E>
    bool alias(const E& rhs) const noexcept {
        return sub_expr.alias(rhs);
    }

    /*!
     * \brief Returns a reference to the ith dimension value.
     *
     * This should only be used internally and with care
     *
     * \return a refernece to the ith dimension value.
     */
    size_t& unsafe_dimension_access(size_t x) {
        return sub_expr.unsafe_dimension_access(x);
    }

    // Assignment functions

    /*!
     * \brief Assign to the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_to(L&& lhs)  const {
        std_assign_evaluate(*this, lhs);
    }

    /*!
     * \brief Add to the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_add_to(L&& lhs)  const {
        std_add_evaluate(*this, lhs);
    }

    /*!
     * \brief Sub from the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_sub_to(L&& lhs)  const {
        std_sub_evaluate(*this, lhs);
    }

    /*!
     * \brief Multiply the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_mul_to(L&& lhs)  const {
        std_mul_evaluate(*this, lhs);
    }

    /*!
     * \brief Divide the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_div_to(L&& lhs)  const {
        std_div_evaluate(*this, lhs);
    }

    /*!
     * \brief Modulo the given left-hand-side expression
     * \param lhs The expression to which assign
     */
    template<typename L>
    void assign_mod_to(L&& lhs)  const {
        std_mod_evaluate(*this, lhs);
    }

    // Internals

    /*!
     * \brief Apply the given visitor to this expression and its descendants.
     * \param visitor The visitor to apply
     */
    void visit(const detail::back_propagate_visitor& visitor) const {
        sub_expr.visit(visitor);
    }

    /*!
     * \brief Apply the given visitor to this expression and its descendants.
     * \param visitor The visitor to apply
     */
    void visit(const detail::temporary_allocator_visitor& visitor) const {
        sub_expr.visit(visitor);
    }

    /*!
     * \brief Apply the given visitor to this expression and its descendants.
     * \param visitor The visitor to apply
     */
    void visit(detail::evaluator_visitor& visitor) const {
        bool old_need_value = visitor.need_value;
        visitor.need_value = true;
        sub_expr.visit(visitor);
        visitor.need_value = old_need_value;
    }

    // TODO A sub_matrix_2d can be vectorized in 2D, but not in 1D

    /*!
     * \brief Print a representation of the view on the given stream
     * \param os The output stream
     * \param v The view to print
     * \return the output stream
     */
    friend std::ostream& operator<<(std::ostream& os, const sub_matrix_2d& v) {
        return os << "sub(" << v.sub_expr << ", " << v.base_i << ", " << v.base_j << ", " << v.base_m << ", " << v.base_n << ")";
    }
};

/*!
 * \brief Specialization for sub_matrix_2d
 */
template <typename T, bool Aligned>
struct etl_traits<etl::sub_matrix_2d<T, Aligned>> {
    using expr_t     = etl::sub_matrix_2d<T, Aligned>;  ///< The expression type
    using sub_expr_t = std::decay_t<T>;                 ///< The sub expression type
    using sub_traits = etl_traits<sub_expr_t>;          ///< The sub traits
    using value_type = typename sub_traits::value_type; ///< The value type of the expression

    static constexpr bool is_etl          = true;                        ///< Indicates if the type is an ETL expression
    static constexpr bool is_transformer  = false;                       ///< Indicates if the type is a transformer
    static constexpr bool is_view         = true;                        ///< Indicates if the type is a view
    static constexpr bool is_magic_view   = false;                       ///< Indicates if the type is a magic view
    static constexpr bool is_fast         = false;                       ///< Indicates if the expression is fast
    static constexpr bool is_linear       = sub_traits::is_linear;       ///< Indicates if the expression is linear
    static constexpr bool is_thread_safe  = sub_traits::is_thread_safe;  ///< Indicates if the expression is thread safe
    static constexpr bool is_value        = false;                       ///< Indicates if the expression is of value type
    static constexpr bool is_direct       = false;                       ///< Indicates if the expression has direct memory access
    static constexpr bool is_generator    = false;                       ///< Indicates if the expression is a generator
    static constexpr bool is_padded       = false;                       ///< Indicates if the expression is padded
    static constexpr bool is_aligned      = false;                       ///< Indicates if the expression is padded
    static constexpr bool needs_evaluator = sub_traits::needs_evaluator; ///< Indicates if the exxpression needs a evaluator visitor
    static constexpr order storage_order  = sub_traits::storage_order;   ///< The expression's storage order

    /*!
     * \brief Indicates if the expression is vectorizable using the
     * given vector mode
     * \tparam V The vector mode
     */
    template <vector_mode_t V>
    using vectorizable = std::false_type;

    /*!
     * \brief Returns the size of the given expression
     * \param v The expression to get the size for
     * \returns the size of the given expression
     */
    static size_t size(const expr_t& v) noexcept {
        return v.m * v.n;
    }

    /*!
     * \brief Returns the dth dimension of the given expression
     * \param v The expression
     * \param d The dimension to get
     * \return The dth dimension of the given expression
     */
    static size_t dim(const expr_t& v, size_t d) noexcept {
        if(d == 0){
            return v.m;
        } else {
            return v.n;
        }
    }

    /*!
     * \brief Returns the number of expressions for this type
     * \return the number of dimensions of this type
     */
    static constexpr size_t dimensions() noexcept {
        return 2;
    }
};

} //end of namespace etl
