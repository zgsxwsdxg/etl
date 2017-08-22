//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file evaluator.hpp
 * \brief The evaluator is responsible for assigning one expression to another.
 *
 * The evaluator will handle all expressions assignment and for each of them,
 * it will choose the most adapted implementation to assign one to another.
 * There are several implementations of assign:
 *   * standard: Use of standard operators []
 *   * direct: Assign directly to the memory (bypassing the operators)
 *   * fast: memcopy
 *   * vectorized: Use SSE/AVX to compute the expression and store it
 *   * parallel: Parallelized version of direct
 *   * parallel_vectorized  Parallel version of vectorized
*/

/*
 * Possible improvements
 *  * The pre/post functions should be refactored so that is less heavy on the code (too much usage)
 *  * Compound operations should ideally be direct evaluated
 */

#pragma once

#include "cpp_utils/static_if.hpp"

#include "etl/eval_selectors.hpp"       //Method selectors
#include "etl/linear_eval_functors.hpp" //Implementation functors
#include "etl/vec_eval_functors.hpp"    //Implementation functors

namespace etl {

/*
 * \brief The evaluator is responsible for assigning one expression to another.
 *
 * The implementation is chosen by SFINAE.
 */
namespace standard_evaluator {
    /*!
     * \brief Allocate temporaries and evaluate sub expressions in RHS
     * \param expr The expr to be visited
     */
    template <typename E>
    void pre_assign_rhs(E&& expr) {
        detail::evaluator_visitor eval_visitor;
        expr.visit(eval_visitor);
    }

// CPP17: if constexpr here
#ifdef ETL_PARALLEL_SUPPORT
    /*!
     * \brief Assign the result of the expression to the result with the given Functor, using parallel implementation
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename Fun, typename E, typename R>
    void par_exec(E&& expr, R&& result) {
        auto slice_functor = [&](auto&& lhs, auto&& rhs){
            Fun::apply(lhs, rhs);
        };

        engine_dispatch_1d_slice_binary(result, expr, slice_functor, 0);
    }
#else
    /*!
     * \brief Assign the result of the expression to the result with the given Functor, using parallel implementation
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename Fun, typename E, typename R>
    void par_exec(E&& expr, R&& result) {
        Fun::apply(result, expr);
    }
#endif

    // Assign functions implementations

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using the standard [] and read_flat operators.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void standard_assign_impl(E& expr, R& result) {
        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] = expr.read_flat(i);
        }
    }

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using direct memory copy between the two
     * expressions, handling possible GPU memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void fast_assign_impl_full(E& expr, R& result) {
//TODO(CPP17) if constexpr
#ifdef ETL_CUDA
        cpp_assert(expr.is_cpu_up_to_date() || expr.is_gpu_up_to_date(), "expr must be in valid state");

        if(expr.is_cpu_up_to_date()){
            direct_copy(expr.memory_start(), expr.memory_end(), result.memory_start());

            result.validate_cpu();
        }

        if(expr.is_gpu_up_to_date()){
            bool cpu_status = expr.is_cpu_up_to_date();

            result.ensure_gpu_allocated();
            result.gpu_copy_from(expr.gpu_memory());

            // Restore CPU status because gpu_copy_from will erase it
            if(cpu_status){
                result.validate_cpu();
            }
        }

        // Invalidation must be done after validation to preserve
        // valid CPU/GPU state

        if (!expr.is_cpu_up_to_date()) {
            result.invalidate_cpu();
        }

        if (!expr.is_gpu_up_to_date()) {
            result.invalidate_gpu();
        }

        cpp_assert(expr.is_cpu_up_to_date() == result.is_cpu_up_to_date(), "fast_assign must preserve CPU status");
        cpp_assert(expr.is_gpu_up_to_date() == result.is_gpu_up_to_date(), "fast_assign must preserve GPU status");
#else
        direct_copy(expr.memory_start(), expr.memory_end(), result.memory_start());
#endif
    }

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using direct memory copy between the two
     * expressions.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void fast_assign_impl(E& expr, R& result) {
        expr.ensure_cpu_up_to_date();

        direct_copy(expr.memory_start(), expr.memory_end(), result.memory_start());

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using a full GPU computation.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_assign_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        result.ensure_gpu_allocated();

        // Compute the GPU representation of the expression
        decltype(auto) t1 = expr.gpu_compute();

        // Copy the GPU memory from the expression to the result
        result.gpu_copy_from(t1.gpu_memory());

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using a direct computation and stored in memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void direct_assign_impl(E& expr, R& result) {
        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::Assign>(expr, result);
        } else {
            detail::Assign::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Assign the result of the expression to the result.
     *
     * This is done using a vectorized computation and stored in
     * memory, possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void vectorized_assign_impl(E& expr, R& result) {
        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        constexpr auto V = detail::select_vector_mode<E, R>();

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::VectorizedAssign<V>>(expr, result);
        } else {
            detail::VectorizedAssign<V>::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    // Selector versions

    /*!
     * \brief Assign the result of the expression to the result
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R, cpp_enable_iff(detail::standard_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        standard_assign_impl(expr, result);
    }

    /*!
     * \copydoc assign_evaluate_impl
     */
    template <typename E, typename R, cpp_enable_iff(std::is_same<value_t<E>, value_t<R>>::value && detail::fast_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        fast_assign_impl_full(expr, result);
    }

    /*!
     * \copydoc assign_evaluate_impl
     */
    template <typename E, typename R, cpp_enable_iff(!std::is_same<value_t<E>, value_t<R>>::value && detail::fast_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        fast_assign_impl(expr, result);
    }

    /*!
     * \copydoc assign_evaluate_impl
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        gpu_assign_impl(expr, result);
    }

    /*!
     * \copydoc assign_evaluate_impl
     */
    template <typename E, typename R, cpp_enable_iff(detail::direct_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        direct_assign_impl(expr, result);
    }

    /*!
     * \copydoc assign_evaluate_impl
     */
    template <typename E, typename R, cpp_enable_iff(detail::vectorized_assign<E, R>)>
    void assign_evaluate_impl(E&& expr, R&& result) {
        vectorized_assign_impl(expr, result);
    }

    // Compound Assign Add functions implementations

    /*!
     * \brief Add the result of the expression to the result.
     *
     * This is performed using standard computation with operator[].
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void standard_compound_add_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] += expr[i];
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Add the result of the expression to the result.
     *
     * This is performed using direct computation with, possibly in
     * parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void direct_compound_add_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::AssignAdd>(expr, result);
        } else {
            detail::AssignAdd::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Add the result of the expression to the result.
     *
     * This is performed using vectorized computation with, possibly in
     * parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void vectorized_compound_add_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        constexpr auto V = detail::select_vector_mode<E, R>();

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::VectorizedAssignAdd<V>>(expr, result);
        } else {
            detail::VectorizedAssignAdd<V>::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

#ifdef ETL_CUBLAS_MODE

    /*!
     * \brief Add the result of the expression to the result.
     *
     * This is performed using full GPU computation with, possibly in
     * parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_assign_add_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        pre_assign_rhs(expr);

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        decltype(auto) t1 = expr.gpu_compute();

        decltype(auto) handle = impl::cublas::start_cublas();

        value_t<E> alpha(1);
        impl::cublas::cublas_axpy(handle.get(), size(result), &alpha, t1.gpu_memory(), 1, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

#ifdef ETL_EGBLAS_MODE

    /*!
     * \brief Add the result of the expression to the result.
     *
     * This is performed using full GPU computation with, possibly in
     * parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_assign_add_scalar_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        impl::egblas::scalar_add(result.gpu_memory(), size(result), 1, &expr.value);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

    // Selector functions

    /*!
     * \brief Add the result of the expression to the result
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R, cpp_enable_iff(detail::standard_compound<E, R>)>
    void add_evaluate(E&& expr, R&& result) {
        standard_compound_add_impl(expr, result);
    }

    /*!
     * \copydoc add_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::direct_compound<E, R>)>
    void add_evaluate(E&& expr, R&& result) {
        direct_compound_add_impl(expr, result);
    }

    /*!
     * \copydoc add_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::vectorized_compound<E, R>)>
    void add_evaluate(E&& expr, R&& result) {
        vectorized_compound_add_impl(expr, result);
    }

#ifdef ETL_CUBLAS_MODE

    /*!
     * \copydoc add_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && !is_scalar<E>)>
    void add_evaluate(E&& expr, R&& result) {
        gpu_assign_add_impl(expr, result);
    }

#endif

#ifdef ETL_EGBLAS_MODE

    /*!
     * \copydoc add_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && is_scalar<E>)>
    void add_evaluate(E&& expr, R&& result) {
        gpu_assign_add_scalar_impl(expr, result);
    }

#endif

    // Compound assign sub implementation functions

    /*!
     * \brief Subtract the result of the expression from the result
     *
     * This is performed using standard operator[].
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void standard_compound_sub_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] -= expr[i];
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Subtract the result of the expression from the result
     *
     * This is performed using direct compution into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void direct_compound_sub_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::AssignSub>(expr, result);
        } else {
            detail::AssignSub::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Subtract the result of the expression from the result
     *
     * This is performed using vectorized compution into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void vectorized_compound_sub_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        constexpr auto V = detail::select_vector_mode<E, R>();

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::VectorizedAssignSub<V>>(expr, result);
        } else {
            detail::VectorizedAssignSub<V>::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

#ifdef ETL_CUBLAS_MODE

    /*!
     * \brief Subtract the result of the expression from the result
     *
     * This is performed using full GPU compution into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_sub_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        pre_assign_rhs(expr);

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        decltype(auto) t1 = expr.gpu_compute();

        decltype(auto) handle = impl::cublas::start_cublas();

        value_t<E> alpha(-1);
        impl::cublas::cublas_axpy(handle.get(), size(result), &alpha, t1.gpu_memory(), 1, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

#ifdef ETL_EGBLAS_MODE

    /*!
     * \brief Subtract the result of the expression from the result
     *
     * This is performed using full GPU compution into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_sub_scalar_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        auto value = -expr.value;
        impl::egblas::scalar_add(result.gpu_memory(), size(result), 1, &value);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

    // Selector functions

    /*!
     * \brief Subtract the result of the expression from the result
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R, cpp_enable_iff(detail::standard_compound<E, R>)>
    void sub_evaluate(E&& expr, R&& result) {
        standard_compound_sub_impl(expr, result);
    }

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::direct_compound<E, R>)>
    void sub_evaluate(E&& expr, R&& result) {
        direct_compound_sub_impl(expr, result);
    }

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::vectorized_compound<E, R>)>
    void sub_evaluate(E&& expr, R&& result) {
        vectorized_compound_sub_impl(expr, result);
    }

#ifdef ETL_CUBLAS_MODE

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && !is_scalar<E>)>
    void sub_evaluate(E&& expr, R&& result) {
        gpu_compound_sub_impl(expr, result);
    }

#endif

#ifdef ETL_EGBLAS_MODE

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && is_scalar<E>)>
    void sub_evaluate(E&& expr, R&& result) {
        gpu_compound_sub_scalar_impl(expr, result);
    }

#endif

    // Compound assign mul implmentation functions

    /*!
     * \brief Multiply the result by the result of the expression
     *
     * This is performed with standard computation with operator[]
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void standard_compound_mul_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] *= expr[i];
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Multiply the result by the result of the expression
     *
     * This is performed with direct computation into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void direct_compound_mul_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::AssignMul>(expr, result);
        } else {
            detail::AssignMul::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Multiply the result by the result of the expression
     *
     * This is performed with vectorized computation into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void vectorized_compound_mul_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        constexpr auto V = detail::select_vector_mode<E, R>();

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::VectorizedAssignMul<V>>(expr, result);
        } else {
            detail::VectorizedAssignMul<V>::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

#ifdef ETL_EGBLAS_MODE

    /*!
     * \brief Multiply the result by the result of the expression
     *
     * This is performed with full GPU computation into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_mul_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        pre_assign_rhs(expr);

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        decltype(auto) t1 = expr.gpu_compute();

        value_t<E> alpha(1);
        impl::egblas::axmy(size(result), &alpha, t1.gpu_memory(), 1, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

#ifdef ETL_CUBLAS_MODE

    /*!
     * \brief Multiply the result by the result of the expression
     *
     * This is performed with full GPU computation into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_mul_scalar_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        decltype(auto) handle = impl::cublas::start_cublas();
        impl::cublas::cublas_scal(handle.get(), size(result), &expr.value, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

    // Selector functions

    /*!
     * \brief Multiply the result by the result of the expression
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R, cpp_enable_iff(detail::standard_compound<E, R>)>
    void mul_evaluate(E&& expr, R&& result) {
        standard_compound_mul_impl(expr, result);
    }

    /*!
     * \copydoc mul_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::direct_compound<E, R>)>
    void mul_evaluate(E&& expr, R&& result) {
        direct_compound_mul_impl(expr, result);
    }

    //Parallel vectorized mul assign

    /*!
     * \copydoc mul_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::vectorized_compound<E, R>)>
    void mul_evaluate(E&& expr, R&& result) {
        vectorized_compound_mul_impl(expr, result);
    }

    // GPU assign mul version

#ifdef ETL_EGBLAS_MODE

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && !is_scalar<E>)>
    void mul_evaluate(E&& expr, R&& result) {
        gpu_compound_mul_impl(expr, result);
    }

#endif

#ifdef ETL_CUBLAS_MODE

    /*!
     * \copydoc mul_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound<E, R> && is_scalar<E>)>
    void mul_evaluate(E&& expr, R&& result) {
        gpu_compound_mul_scalar_impl(expr, result);
    }

#endif

    // Compound Assign Div implementation functions

    /*!
     * \brief Divide the result by the result of the expression
     *
     * This is performed using standard computation with operator[]
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void standard_compound_div_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] /= expr[i];
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Divide the result by the result of the expression
     *
     * This is performed using direct computation into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void direct_compound_div_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::AssignDiv>(expr, result);
        } else {
            detail::AssignDiv::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Divide the result by the result of the expression
     *
     * This is performed using vectorized computation into memory,
     * possibly in parallel.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void vectorized_compound_div_impl(E& expr, R& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        constexpr auto V = detail::select_vector_mode<E, R>();

        if(is_thread_safe<E> && select_parallel(etl::size(result))){
            par_exec<detail::VectorizedAssignDiv<V>>(expr, result);
        } else {
            detail::VectorizedAssignDiv<V>::apply(result, expr);
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

#ifdef ETL_EGBLAS_MODE

    /*!
     * \brief Divide the result by the result of the expression
     *
     * This is performed using full GPU computation into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_div_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        pre_assign_rhs(expr);

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        decltype(auto) t1 = expr.gpu_compute();

        value_t<E> alpha(1);
        impl::egblas::axdy(size(result), &alpha, t1.gpu_memory(), 1, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

#ifdef ETL_CUBLAS_MODE

    /*!
     * \brief Divide the result by the result of the expression
     *
     * This is performed using full GPU computation into memory.
     *
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void gpu_compound_div_scalar_impl(E& expr, R& result) {
        inc_counter("gpu:assign");

        result.ensure_gpu_up_to_date();

        // Compute the GPU representation of the expression
        auto value = value_t<E>(1.0) / expr.value;
        decltype(auto) handle = impl::cublas::start_cublas();
        impl::cublas::cublas_scal(handle.get(), size(result), &value, result.gpu_memory(), 1);

        // Validate the GPU and invalidates the CPU
        result.validate_gpu();
        result.invalidate_cpu();
    }

#endif

    // Selector functions

    /*!
     * \brief Divide the result by the result of the expression
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R, cpp_enable_iff(detail::standard_compound_div<E, R>)>
    void div_evaluate(E&& expr, R&& result) {
        standard_compound_div_impl(expr, result);
    }

    /*!
     * \copydoc div_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::direct_compound_div<E, R>)>
    void div_evaluate(E&& expr, R&& result) {
        direct_compound_div_impl(expr, result);
    }

    /*!
     * \copydoc div_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::vectorized_compound_div<E, R>)>
    void div_evaluate(E&& expr, R&& result) {
        vectorized_compound_div_impl(expr, result);
    }

#ifdef ETL_EGBLAS_MODE

    /*!
     * \copydoc sub_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound_div<E, R> && !is_scalar<E>)>
    void div_evaluate(E&& expr, R&& result) {
        gpu_compound_div_impl(expr, result);
    }

#endif

#ifdef ETL_CUBLAS_MODE

    /*!
     * \copydoc mul_evaluate
     */
    template <typename E, typename R, cpp_enable_iff(detail::gpu_compound_div<E, R> && is_scalar<E>)>
    void div_evaluate(E&& expr, R&& result) {
        gpu_compound_div_scalar_impl(expr, result);
    }

#endif

    //Standard Mod Evaluate (no optimized versions for mod)

    /*!
     * \brief Modulo the result by the result of the expression
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void mod_evaluate(E&& expr, R&& result) {
        pre_assign_rhs(expr);

        safe_ensure_cpu_up_to_date(expr);
        safe_ensure_cpu_up_to_date(result);

        for (size_t i = 0; i < etl::size(result); ++i) {
            result[i] %= expr[i];
        }

        result.validate_cpu();
        result.invalidate_gpu();
    }

    /*!
     * \brief Assign the result of the expression to the result
     * \param expr The right hand side expression
     * \param result The left hand side
     */
    template <typename E, typename R>
    void assign_evaluate(E&& expr, R&& result) {
        //Evaluate sub parts, if any
        pre_assign_rhs(expr);

        //Perform the real evaluation, selected by TMP
        assign_evaluate_impl(expr, result);
    }

} // end of namespace standard_evaluator

/*!
 * \brief Traits indicating if a direct assign is possible
 *
 * A direct assign is a standard assign without any transposition
 *
 * \tparam Expr The type of expression (RHS)
 * \tparam Result The type of result (LHS)
 */
template <typename Expr, typename Result>
constexpr bool direct_assign_compatible =
        decay_traits<Expr>::is_generator                                            // No dimensions, always possible to assign
    ||  decay_traits<Expr>::storage_order == decay_traits<Result>::storage_order // Same storage always possible to assign
    ||  all_1d<Expr, Result>                                                     // Vectors can be directly assigned, regardless of the storage order
    ;

/*!
 * \brief Evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_assign_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::assign_evaluate(expr, result);
}

/*!
 * \brief Evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_assign_evaluate(Expr&& expr, Result&& result) {
    static_assert(all_2d<Expr, Result>, "Invalid assign configuration");
    standard_evaluator::assign_evaluate(transpose(expr), result);
}

/*!
 * \brief Compound add evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_add_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::add_evaluate(expr, result);
}

/*!
 * \brief Compound add evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_add_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::add_evaluate(transpose(expr), result);
}

/*!
 * \brief Compound subtract evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_sub_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::sub_evaluate(expr, result);
}

/*!
 * \brief Compound subtract evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_sub_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::sub_evaluate(transpose(expr), result);
}

/*!
 * \brief Compound multiply evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_mul_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::mul_evaluate(expr, result);
}

/*!
 * \brief Compound multiply evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_mul_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::mul_evaluate(transpose(expr), result);
}

/*!
 * \brief Compound divide evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_div_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::div_evaluate(expr, result);
}

/*!
 * \brief Compound divide evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_div_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::div_evaluate(transpose(expr), result);
}

/*!
 * \brief Compound modulo evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(direct_assign_compatible<Expr, Result>)>
void std_mod_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::mod_evaluate(expr, result);
}

/*!
 * \brief Compound modulo evaluation of the expr into result
 * \param expr The right hand side expression
 * \param result The left hand side
 */
template <typename Expr, typename Result, cpp_enable_iff(!direct_assign_compatible<Expr, Result>)>
void std_mod_evaluate(Expr&& expr, Result&& result) {
    standard_evaluator::mod_evaluate(transpose(expr), result);
}

/*!
 * \brief Force the internal evaluation of an expression
 * \param expr The expression to force inner evaluation
 *
 * This function can be used when complex expressions are used
 * lazily.
 */
template <typename Expr>
void force(Expr&& expr) {
    standard_evaluator::pre_assign_rhs(expr);
}

} //end of namespace etl
