//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include "etl/expr/base_temporary_expr.hpp"

//The implementations
#include "etl/impl/std/gemm.hpp"
#include "etl/impl/std/strassen_mmul.hpp"
#include "etl/impl/blas/gemm.hpp"
#include "etl/impl/vec/gemm.hpp"
#include "etl/impl/vec/gemm_conv.hpp"
#include "etl/impl/cublas/gemm.hpp"

namespace etl {

/*!
 * \brief A transposition expression.
 * \tparam A The transposed type
 */
template <typename A, typename B, bool Strassen>
struct gemm_expr : base_temporary_expr_bin<gemm_expr<A, B, Strassen>, A, B> {
    using value_type  = value_t<A>;                               ///< The type of value of the expression
    using this_type   = gemm_expr<A, B, Strassen>;                ///< The type of this expression
    using base_type   = base_temporary_expr_bin<this_type, A, B>; ///< The base type
    using left_traits = decay_traits<A>;                          ///< The traits of the sub type

    static constexpr auto storage_order = left_traits::storage_order; ///< The sub storage order

    /*!
     * \brief Construct a new expression
     * \param a The sub expression
     */
    explicit gemm_expr(A a, B b) : base_type(a, b) {
        //Nothing else to init
    }

    /*!
     * \brief Assert for the validity of the matrix-matrix multiplication operation
     * \param a The left side matrix
     * \param b The right side matrix
     * \param c The result matrix
     */
    template <typename C, cpp_disable_iff(all_fast<A, B, C>)>
    static void check(const A& a, const B& b, const C& c) {
        cpp_assert(
            dim<1>(a) == dim<0>(b)         //interior dimensions
                && dim<0>(a) == dim<0>(c)  //exterior dimension 1
                && dim<1>(b) == dim<1>(c), //exterior dimension 2
            "Invalid sizes for multiplication");
        cpp_unused(a);
        cpp_unused(b);
        cpp_unused(c);
    }

    /*!
     * \brief Assert for the validity of the matrix-matrix multiplication operation
     * \param a The left side matrix
     * \param b The right side matrix
     * \param c The result matrix
     */
    template <typename C, cpp_enable_iff(all_fast<A, B, C>)>
    static void check(const A& a, const B& b, const C& c) {
        static_assert(
            dim<1, A>() == dim<0, B>()         //interior dimensions
                && dim<0, A>() == dim<0, C>()  //exterior dimension 1
                && dim<1, B>() == dim<1, C>(), //exterior dimension 2
            "Invalid sizes for multiplication");
        cpp_unused(a);
        cpp_unused(b);
        cpp_unused(c);
    }

    // Assignment functions

    /*!
     * \brief Select an implementation of GEMM, not considering local context
     * \param n1 The left dimension of the multiplication
     * \param n2 The inner dimension of the multiplication
     * \param n3 The right dimension of the multiplication
     * \return The implementation to use
     */
    template <typename AA, typename BB, typename C>
    static constexpr gemm_impl select_default_gemm_impl(const size_t n1, const size_t n2, const size_t n3) {
        cpp_unused(n1);
        cpp_unused(n2);
        cpp_unused(n3);

        //Note since these boolean will be known at compile time, the conditions will be a lot simplified
        constexpr bool blas   = cblas_enabled;
        constexpr bool cublas = cublas_enabled;
        constexpr bool homo   = all_homogeneous<AA, BB, C>;

        if (cublas && homo) {
            return gemm_impl::CUBLAS;
        } else if (blas && homo) {
            return gemm_impl::BLAS;
        }

        if(vec_enabled && homo && all_vectorizable_t<vector_mode, AA, BB, C>){
            return gemm_impl::VEC;
        }

        return gemm_impl::STD;
    }

    /*!
     * \brief Select an implementation of GEMM
     * \param n1 The left dimension of the  multiplication
     * \param n2 The inner dimension of the  multiplication
     * \param n3 The right dimension of the  multiplication
     * \return The implementation to use
     */
    template <typename AA, typename BB, typename C>
    static inline gemm_impl select_gemm_impl(const size_t n1, const size_t n2, const size_t n3) {
        auto def = select_default_gemm_impl<AA, BB, C>(n1, n2, n3);

        if (local_context().gemm_selector.forced) {
            auto forced = local_context().gemm_selector.impl;

            switch (forced) {
                //CUBLAS cannot always be used
                case gemm_impl::CUBLAS:
                    if (!cublas_enabled || !all_homogeneous<AA, BB, C>) {                                                                                     //COVERAGE_EXCLUDE_LINE
                        std::cerr << "Forced selection to CUBLAS gemm implementation, but not possible for this expression" << std::endl; //COVERAGE_EXCLUDE_LINE
                        return def;                                                              //COVERAGE_EXCLUDE_LINE
                    }                                                                                                                     //COVERAGE_EXCLUDE_LINE

                    return forced;

                //BLAS cannot always be used
                case gemm_impl::BLAS:
                    if (!cblas_enabled || !all_homogeneous<AA, BB, C>) {                                                                                    //COVERAGE_EXCLUDE_LINE
                        std::cerr << "Forced selection to BLAS gemm implementation, but not possible for this expression" << std::endl; //COVERAGE_EXCLUDE_LINE
                        return def;                                                            //COVERAGE_EXCLUDE_LINE
                    }                                                                                                                   //COVERAGE_EXCLUDE_LINE

                    return forced;

                //VEC cannot always be used
                case gemm_impl::VEC:
                    if (!vec_enabled || !all_vectorizable_t<vector_mode, AA, BB, C> || !all_homogeneous<AA, BB, C>) {                                               //COVERAGE_EXCLUDE_LINE
                        std::cerr << "Forced selection to VEC gemm implementation, but not possible for this expression" << std::endl; //COVERAGE_EXCLUDE_LINE
                        return def;                                                            //COVERAGE_EXCLUDE_LINE
                    }                                                                                                                   //COVERAGE_EXCLUDE_LINE

                    return forced;

                //In other cases, simply use the forced impl
                default:
                    return forced;
            }
        }

        return def;
    }

    /*!
     * \brief Compute C = trans(A) * trans(B)
     * \param a The A matrix
     * \param b The B matrix
     * \param c The C matrix (output)
     */
    template <typename AA, typename BB, typename C, cpp_enable_iff(is_transpose_expr<AA> && is_transpose_expr<BB>)>
    static void apply_raw(AA&& a, BB&& b, C&& c) {
        auto impl = select_gemm_impl<AA, BB, C>(etl::dim<0>(a), etl::dim<1>(a), etl::dim<1>(c));

        if (impl == gemm_impl::STD) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::standard::mm_mul(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::VEC) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::vec::gemm(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::BLAS) {
            standard_evaluator::pre_assign_rhs(a.a());
            standard_evaluator::pre_assign_rhs(b.a());

            etl::impl::blas::gemm_tt(make_temporary(a.a()), make_temporary(b.a()), c);
        } else if (impl == gemm_impl::CUBLAS) {
            standard_evaluator::pre_assign_rhs(a.a());
            standard_evaluator::pre_assign_rhs(b.a());

            etl::impl::cublas::gemm_tt(make_temporary(a.a()), make_temporary(b.a()), c);
        } else {
            cpp_unreachable("Invalid selection of gemm");
        }
    }

    /*!
     * \brief Compute C = A * trans(B)
     * \param a The A matrix
     * \param b The B matrix
     * \param c The C matrix (output)
     */
    template <typename AA, typename BB, typename C, cpp_enable_iff(!is_transpose_expr<AA> && is_transpose_expr<BB>)>
    static void apply_raw(AA&& a, BB&& b, C&& c) {
        auto impl = select_gemm_impl<AA, BB, C>(etl::dim<0>(a), etl::dim<1>(a), etl::dim<1>(c));

        if (impl == gemm_impl::STD) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::standard::mm_mul(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::VEC) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b.a());

            etl::impl::vec::gemm_nt(make_temporary(a), make_temporary(b.a()), c);
        } else if (impl == gemm_impl::BLAS) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b.a());

            etl::impl::blas::gemm_nt(make_temporary(a), make_temporary(b.a()), c);
        } else if (impl == gemm_impl::CUBLAS) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b.a());

            etl::impl::cublas::gemm_nt(make_temporary(a), make_temporary(b.a()), c);
        } else {
            cpp_unreachable("Invalid selection of gemm");
        }
    }

    /*!
     * \brief Compute C = trans(A) * trans(B)
     * \param a The A matrix
     * \param b The B matrix
     * \param c The C matrix (output)
     */
    template <typename AA, typename BB, typename C, cpp_enable_iff(is_transpose_expr<AA> && !is_transpose_expr<BB>)>
    static void apply_raw(AA&& a, BB&& b, C&& c) {
        auto impl = select_gemm_impl<AA, BB, C>(etl::dim<0>(a), etl::dim<1>(a), etl::dim<1>(c));

        if (impl == gemm_impl::STD) {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::standard::mm_mul(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::VEC) {
            standard_evaluator::pre_assign_rhs(a.a());
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::vec::gemm_tn(make_temporary(a.a()), make_temporary(b), c);
        } else if (impl == gemm_impl::BLAS) {
            standard_evaluator::pre_assign_rhs(a.a());
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::blas::gemm_tn(make_temporary(a.a()), make_temporary(b), c);
        } else if (impl == gemm_impl::CUBLAS) {
            standard_evaluator::pre_assign_rhs(a.a());
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::cublas::gemm_tn(make_temporary(a.a()), make_temporary(b), c);
        } else {
            cpp_unreachable("Invalid selection of gemm");
        }
    }

    /*!
     * \brief Compute C = A * B
     * \param a The A matrix
     * \param b The B matrix
     * \param c The C matrix (output)
     */
    template <typename AA, typename BB, typename C, cpp_enable_iff(!is_transpose_expr<AA> && !is_transpose_expr<BB>)>
    static void apply_raw(AA&& a, BB&& b, C&& c) {
        auto impl = select_gemm_impl<AA, BB, C>(etl::dim<0>(a), etl::dim<1>(a), etl::dim<1>(c));

        standard_evaluator::pre_assign_rhs(a);
        standard_evaluator::pre_assign_rhs(b);

        if (impl == gemm_impl::STD) {
            etl::impl::standard::mm_mul(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::VEC) {
            etl::impl::vec::gemm(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::BLAS) {
            etl::impl::blas::gemm(make_temporary(a), make_temporary(b), c);
        } else if (impl == gemm_impl::CUBLAS) {
            etl::impl::cublas::gemm(make_temporary(a), make_temporary(b), c);
        } else {
            cpp_unreachable("Invalid selection of gemm");
        }
    }

    /*!
     * \brief Assign to a matrix of the same storage order
     * \param c The expression to which assign
     */
    template<typename C>
    void assign_to(C&& c)  const {
        static_assert(all_etl_expr<A, B, C>, "gemm only supported for ETL expressions");

        auto& a = this->a();
        auto& b = this->b();

        check(a, b, c);

        if /* constexpr */ (!Strassen){
            apply_raw(a, b, c);
        } else {
            standard_evaluator::pre_assign_rhs(a);
            standard_evaluator::pre_assign_rhs(b);

            etl::impl::standard::strassen_mm_mul(make_temporary(a), make_temporary(b), c);
        }
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

    /*!
     * \brief Print a representation of the expression on the given stream
     * \param os The output stream
     * \param expr The expression to print
     * \return the output stream
     */
    friend std::ostream& operator<<(std::ostream& os, const gemm_expr& expr) {
        return os << expr._a << " * " << expr._b;
    }
};

/*!
 * \brief Traits for a transpose expression
 * \tparam A The transposed sub type
 */
template <typename A, typename B, bool Strassen>
struct etl_traits<etl::gemm_expr<A, B, Strassen>> {
    using expr_t       = etl::gemm_expr<A, B, Strassen>; ///< The expression type
    using left_expr_t  = std::decay_t<A>;            ///< The left sub expression type
    using right_expr_t = std::decay_t<B>;            ///< The right sub expression type
    using left_traits  = etl_traits<left_expr_t>;    ///< The left sub traits
    using right_traits = etl_traits<right_expr_t>;   ///< The right sub traits
    using value_type   = value_t<A>;                 ///< The value type of the expression

    static constexpr bool is_etl         = true;                                          ///< Indicates if the type is an ETL expression
    static constexpr bool is_transformer = false;                                         ///< Indicates if the type is a transformer
    static constexpr bool is_view        = false;                                         ///< Indicates if the type is a view
    static constexpr bool is_magic_view  = false;                                         ///< Indicates if the type is a magic view
    static constexpr bool is_fast        = left_traits::is_fast && right_traits::is_fast; ///< Indicates if the expression is fast
    static constexpr bool is_linear      = false;                                         ///< Indicates if the expression is linear
    static constexpr bool is_thread_safe = true;                                          ///< Indicates if the expression is thread safe
    static constexpr bool is_value       = false;                                         ///< Indicates if the expression is of value type
    static constexpr bool is_direct      = true;                                          ///< Indicates if the expression has direct memory access
    static constexpr bool is_generator   = false;                                         ///< Indicates if the expression is a generator
    static constexpr bool is_padded      = false;                                         ///< Indicates if the expression is padded
    static constexpr bool is_aligned     = true;                                          ///< Indicates if the expression is padded
    static constexpr bool is_temporary   = true;                                          ///< Indicates if the expression needs a evaluator visitor
    static constexpr order storage_order = left_traits::storage_order;                    ///< The expression's storage order
    static constexpr bool gpu_computable = cuda_enabled;                                  ///< Indicates if the expression can be computed on GPU

    /*!
     * \brief Indicates if the expression is vectorizable using the
     * given vector mode
     * \tparam V The vector mode
     */
    template <vector_mode_t V>
    static constexpr bool vectorizable = true;

    /*!
     * \brief Returns the DDth dimension of the expression
     * \return the DDth dimension of the expression
     */
    template <size_t DD>
    static constexpr size_t dim() {
        return DD == 0 ? decay_traits<A>::template dim<0>()
                       : decay_traits<B>::template dim<1>();
    }

    /*!
     * \brief Returns the dth dimension of the expression
     * \param e The sub expression
     * \param d The dimension to get
     * \return the dth dimension of the expression
     */
    static size_t dim(const expr_t& e, size_t d) {
        if (d == 0){
            return etl::dim(e._a, 0);
        } else {
            return etl::dim(e._b, 1);
        }
    }

    /*!
     * \brief Returns the size of the expression
     * \param e The sub expression
     * \return the size of the expression
     */
    static size_t size(const expr_t& e) {
        return etl::dim(e._a, 0) * etl::dim(e._b, 1);
    }

    /*!
     * \brief Returns the size of the expression
     * \return the size of the expression
     */
    static constexpr size_t size() {
        return decay_traits<A>::template dim<0>() * decay_traits<B>::template dim<1>();
    }

    /*!
     * \brief Returns the number of dimensions of the expression
     * \return the number of dimensions of the expression
     */
    static constexpr size_t dimensions() {
        return 2;
    }
};

/*!
 * \brief Multiply two matrices together
 * \param a The left hand side matrix
 * \param b The right hand side matrix
 * \return An expression representing the matrix-matrix multiplication of a and b
 */
template <typename A, typename B, cpp_enable_iff(all_2d<A, B>)>
gemm_expr<detail::build_type<A>, detail::build_type<B>, false> operator*(A&& a, B&& b) {
    static_assert(all_etl_expr<A, B>, "Matrix multiplication only supported for ETL expressions");
    static_assert(all_2d<A, B>, "Matrix multiplication only works in 2D");

    return gemm_expr<detail::build_type<A>, detail::build_type<B>, false>{a, b};
}

/*!
 * \brief Multiply two matrices together
 * \param a The left hand side matrix
 * \param b The right hand side matrix
 * \return An expression representing the matrix-matrix multiplication of a and b
 */
template <typename A, typename B, cpp_enable_iff(all_2d<A, B>)>
gemm_expr<detail::build_type<A>, detail::build_type<B>, false> mul(A&& a, B&& b) {
    static_assert(all_etl_expr<A, B>, "Matrix multiplication only supported for ETL expressions");
    static_assert(all_2d<A, B>, "Matrix multiplication only works in 2D");

    return gemm_expr<detail::build_type<A>, detail::build_type<B>, false>{a, b};
}

/*!
 * \brief Multiply two matrices together and store the result in c
 * \param a The left hand side matrix
 * \param b The right hand side matrix
 * \param c The expression used to store the result
 * \return An expression representing the matrix-matrix multiplication of a and b
 */
template <typename A, typename B, typename C, cpp_enable_iff(all_2d<A, B, C>)>
auto mul(A&& a, B&& b, C&& c) {
    static_assert(all_etl_expr<A, B, C>, "Matrix multiplication only supported for ETL expressions");
    static_assert(all_2d<A, B, C>, "Matrix multiplication only works in 2D");

    c = mul(a, b);
    return c;
}

/*!
 * \brief Multiply two matrices together using strassen
 * \param a The left hand side matrix
 * \param b The right hand side matrix
 * \return An expression representing the matrix-matrix multiplication of a and b
 */
template <typename A, typename B>
gemm_expr<detail::build_type<A>, detail::build_type<B>, true> strassen_mul(A&& a, B&& b) {
    static_assert(all_etl_expr<A, B>, "Matrix multiplication only supported for ETL expressions");
    static_assert(all_2d<A, B>, "Matrix multiplication only works in 2D");

    return gemm_expr<detail::build_type<A>, detail::build_type<B>, true>{a, b};
}

/*!
 * \brief Multiply two matrices together using strassen and store the result in c
 * \param a The left hand side matrix
 * \param b The right hand side matrix
 * \param c The expression used to store the result
 * \return An expression representing the matrix-matrix multiplication of a and b
 */
template <typename A, typename B, typename C>
auto strassen_mul(A&& a, B&& b, C&& c) {
    static_assert(all_etl_expr<A, B, C>, "Matrix multiplication only supported for ETL expressions");
    static_assert(all_2d<A, B, C>, "Matrix multiplication only works in 2D");

    c = mul(a,b);
    return c;
}

} //end of namespace etl
