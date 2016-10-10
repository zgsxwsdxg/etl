//=======================================================================
// Copyright (c) 2014-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file eval_functors.hpp
 * \brief Contains the functors used by the evaluator to perform its
 * actions.
 */

#pragma once

#include "etl/visitor.hpp"        //visitor of the expressions
#include "etl/eval_selectors.hpp" //method selectors

namespace etl {

namespace detail {

/*!
 * \brief Functor for simple assign
 *
 * The result is written to lhs with operator[] and read from rhs
 * with read_flat
 */
template <typename L_Expr, typename R_Expr>
struct Assign {
    using value_type = value_t<L_Expr>;

    mutable value_type* lhs; ///< The left hand side
    R_Expr rhs;              ///< The right hand side
    const std::size_t _size; ///< The size to assign

    /*!
     * \brief Constuct a new Assign
     * \param lhs The lhs memory
     * \param rhs The rhs memory
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    Assign(L_Expr lhs, R_Expr rhs) : lhs(lhs.memory_start()), rhs(rhs), _size(etl::size(lhs)) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = 0;

        if (unroll_normal_loops) {
            iend = _size & std::size_t(-4);

            for (std::size_t i = 0; i < iend; i += 4) {
                lhs[i]     = rhs.read_flat(i);
                lhs[i + 1] = rhs.read_flat(i + 1);
                lhs[i + 2] = rhs.read_flat(i + 2);
                lhs[i + 3] = rhs.read_flat(i + 3);
            }
        }

        for (std::size_t i = iend; i < _size; ++i) {
            lhs[i] = rhs.read_flat(i);
        }
    }
};

/*!
 * \brief Common base for vectorized functors
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr, typename Base>
struct vectorized_base {
    using derived_t   = Base;             ///< The derived type
    using memory_type = value_t<L_Expr>*; ///< The memory type

    L_Expr& lhs;              ///< The left hand side
    memory_type lhs_m;        ///< The left hand side memory
    R_Expr& rhs;              ///< The right hand side
    const std::size_t _first; ///< The first index to assign
    const std::size_t _last;  ///< The last index to assign
    const std::size_t _size;  ///< The size to assign

    /*!
     * \brief The RHS value type
     */
    using lhs_value_type = value_t<L_Expr>;

    /*!
     * \brief The RHS value type
     */
    using rhs_value_type = value_t<R_Expr>;

    /*!
     * \brief The intrinsic type for the value type
     */
    using IT = typename get_intrinsic_traits<V>::template type<rhs_value_type>;

    /*!
     * \brief The vector implementation to use
     */
    using vect_impl = typename get_vector_impl<V>::type;

    /*!
     * \brief Constuct a new vectorized_base
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    vectorized_base(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), lhs_m(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Load a vector from lhs at position i
     * \param i The index where to start loading from
     * \return a vector from lhs starting at position i
     */
    inline auto lhs_load(std::size_t i) const {
        return lhs.template load<vect_impl>(i);
    }

    /*!
     * \brief Load a vector from rhs at position i
     * \param i The index where to start loading from
     * \return a vector from rhs starting at position i
     */
    inline auto rhs_load(std::size_t i) const {
        return rhs.template load<vect_impl>(i);
    }

private:
    /*!
     * \brief Returns a reference to the derived object, i.e. the object using the CRTP injector.
     * \return a reference to the derived object.
     */
    const derived_t& as_derived() const noexcept {
        return *static_cast<const derived_t*>(this);
    }
};

/*!
 * \brief Functor for vectorized assign
 *
 * The result is computed in a vectorized fashion with several
 * operations per cycle and written directly to the memory of lhs.
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr>
struct VectorizedAssign : vectorized_base<V, L_Expr, R_Expr, VectorizedAssign<V, L_Expr, R_Expr>> {
    using base_t    = vectorized_base<V, L_Expr, R_Expr, VectorizedAssign<V, L_Expr, R_Expr>>; ///< The base type
    using IT        = typename base_t::IT;                                                     ///< The intrisic type
    using vect_impl = typename base_t::vect_impl;                                              ///< The vector implementation

    using base_t::lhs_m;
    using base_t::lhs;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    /*!
     * \brief Constuct a new VectorizedAssign
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    VectorizedAssign(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::rhs_load;

    /*!
     * \brief Compute the vectorized iterations of the loop using aligned store operations
     * \param first The index when to start
     */
    void operator()() const {
        constexpr const bool remainder = !padding || !all_padded<L_Expr, R_Expr>::value;

        const size_t last = remainder ? _first + (_size & size_t(-IT::size)) : _last;

        std::size_t i = _first;

        if(streaming && _size > cache_size / (sizeof(typename base_t::lhs_value_type) * 3) && !rhs.alias(lhs)){
            for (; i < last; i += IT::size) {
                lhs.template stream<vect_impl>(rhs_load(i), i);
            }

            for (; remainder && i < _last; ++i) {
                lhs_m[i] = rhs[i];
            }
        } else {
            for (; i + (IT::size * 3) < last; i += 4 * IT::size) {
                lhs.template store<vect_impl>(rhs_load(i + 0 * IT::size), i + 0 * IT::size);
                lhs.template store<vect_impl>(rhs_load(i + 1 * IT::size), i + 1 * IT::size);
                lhs.template store<vect_impl>(rhs_load(i + 2 * IT::size), i + 2 * IT::size);
                lhs.template store<vect_impl>(rhs_load(i + 3 * IT::size), i + 3 * IT::size);
            }

            for (; i < last; i += IT::size) {
                lhs.template store<vect_impl>(rhs_load(i), i);
            }

            for (; remainder && i < _last; ++i) {
                lhs_m[i] = rhs[i];
            }
        }
    }
};

/*!
 * \brief Functor for simple compound assign add
 */
template <typename L_Expr, typename R_Expr>
struct AssignAdd {
    using value_type = value_t<L_Expr>;

    mutable value_type* lhs;         ///< The left hand side
    R_Expr& rhs;              ///< The right hand side
    const std::size_t _first; ///< The first index to assign
    const std::size_t _last;  ///< The last index to assign
    const std::size_t _size;  ///< The size to assign

    /*!
     * \brief Constuct a new AssignAdd
     * \param lhs The lhs memory
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    AssignAdd(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i] += rhs[i];
                lhs[i + 1] += rhs[i + 1];
                lhs[i + 2] += rhs[i + 2];
                lhs[i + 3] += rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] += rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign add
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr>
struct VectorizedAssignAdd : vectorized_base<V, L_Expr, R_Expr, VectorizedAssignAdd<V, L_Expr, R_Expr>> {
    using base_t    = vectorized_base<V, L_Expr, R_Expr, VectorizedAssignAdd<V, L_Expr, R_Expr>>; ///< The base type
    using IT        = typename base_t::IT;                                                        ///< The intrisic type
    using vect_impl = typename base_t::vect_impl;                                                 ///< The vector implementation

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    /*!
     * \brief Constuct a new VectorizedAssignAdd
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    VectorizedAssignAdd(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::lhs_load;
    using base_t::rhs_load;

    /*!
     * \brief Compute the vectorized iterations of the loop using aligned store operations
     * \param first The index when to start
     */
    void operator()() const {
        constexpr const bool remainder = !padding || !all_padded<L_Expr, R_Expr>::value;

        const size_t last = remainder ? _last : _first + alloc_size<typename base_t::lhs_value_type>(_size);

        std::size_t i = _first;

        for (; i + IT::size * 4 - 1 < last; i += IT::size * 4) {
            lhs.template store<vect_impl>(vect_impl::add(lhs_load(i), rhs_load(i)), i);
            lhs.template store<vect_impl>(vect_impl::add(lhs_load(i + 1 * IT::size), rhs_load(i + 1 * IT::size)), i + 1 * IT::size);
            lhs.template store<vect_impl>(vect_impl::add(lhs_load(i + 2 * IT::size), rhs_load(i + 2 * IT::size)), i + 2 * IT::size);
            lhs.template store<vect_impl>(vect_impl::add(lhs_load(i + 3 * IT::size), rhs_load(i + 3 * IT::size)), i + 3 * IT::size);
        }

        for (; i + IT::size - 1 < last; i += IT::size) {
            lhs.template store<vect_impl>(vect_impl::add(lhs_load(i), rhs_load(i)), i);
        }

        for (; remainder && i < last; ++i) {
            lhs_m[i] += rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign sub
 */
template <typename L_Expr, typename R_Expr>
struct AssignSub {
    using value_type = value_t<L_Expr>;

    mutable value_type* lhs;         ///< The left hand side
    R_Expr& rhs;              ///< The right hand side
    const std::size_t _first; ///< The first index to assign
    const std::size_t _last;  ///< The last index to assign
    const std::size_t _size;  ///< The size to assign

    /*!
     * \brief Constuct a new AssignSub
     * \param lhs The lhs memory
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    AssignSub(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i] -= rhs[i];
                lhs[i + 1] -= rhs[i + 1];
                lhs[i + 2] -= rhs[i + 2];
                lhs[i + 3] -= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] -= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign sub
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr>
struct VectorizedAssignSub : vectorized_base<V, L_Expr, R_Expr, VectorizedAssignSub<V, L_Expr, R_Expr>> {
    using base_t    = vectorized_base<V, L_Expr, R_Expr, VectorizedAssignSub<V, L_Expr, R_Expr>>; ///< The base type
    using IT        = typename base_t::IT;                                                        ///< The intrisic type
    using vect_impl = typename base_t::vect_impl;                                                 ///< The vector implementation

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    /*!
     * \brief Constuct a new VectorizedAssignSub
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    VectorizedAssignSub(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::lhs_load;
    using base_t::rhs_load;

    /*!
     * \brief Compute the vectorized iterations of the loop using aligned store operations
     * \param first The index when to start
     */
    void operator()() const {
        constexpr const bool remainder = !padding || !all_padded<L_Expr, R_Expr>::value;

        const size_t last = remainder ? _last : _first + alloc_size<typename base_t::lhs_value_type>(_size);

        std::size_t i = _first;

        for (; i + IT::size * 4 - 1 < last; i += IT::size * 4) {
            lhs.template store<vect_impl>(vect_impl::sub(lhs_load(i), rhs_load(i)), i);
            lhs.template store<vect_impl>(vect_impl::sub(lhs_load(i + 1 * IT::size), rhs_load(i + 1 * IT::size)), i + 1 * IT::size);
            lhs.template store<vect_impl>(vect_impl::sub(lhs_load(i + 2 * IT::size), rhs_load(i + 2 * IT::size)), i + 2 * IT::size);
            lhs.template store<vect_impl>(vect_impl::sub(lhs_load(i + 3 * IT::size), rhs_load(i + 3 * IT::size)), i + 3 * IT::size);
        }

        for (; i + IT::size - 1 < last; i += IT::size) {
            lhs.template store<vect_impl>(vect_impl::sub(lhs_load(i), rhs_load(i)), i);
        }

        for (; remainder && i < last; ++i) {
            lhs_m[i] -= rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign mul
 */
template <typename L_Expr, typename R_Expr>
struct AssignMul {
    using value_type = value_t<L_Expr>;

    mutable value_type* lhs;         ///< The left hand side
    R_Expr& rhs;              ///< The right hand side
    const std::size_t _first; ///< The first index to assign
    const std::size_t _last;  ///< The last index to assign
    const std::size_t _size;  ///< The size to assign

    /*!
     * \brief Constuct a new AssignMul
     * \param lhs The lhs memory
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    AssignMul(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i] *= rhs[i];
                lhs[i + 1] *= rhs[i + 1];
                lhs[i + 2] *= rhs[i + 2];
                lhs[i + 3] *= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] *= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign mul
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr>
struct VectorizedAssignMul : vectorized_base<V, L_Expr, R_Expr, VectorizedAssignMul<V, L_Expr, R_Expr>> {
    static constexpr const bool Cx = is_complex_t<value_t<L_Expr>>::value; ///< Indicates it is a complex multiplication

    using base_t    = vectorized_base<V, L_Expr, R_Expr, VectorizedAssignMul<V, L_Expr, R_Expr>>; ///< The base type
    using IT        = typename base_t::IT;                                                        ///< The intrisic type
    using vect_impl = typename base_t::vect_impl;                                                 ///< The vector implementation

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    /*!
     * \brief Constuct a new VectorizedAssignMul
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    VectorizedAssignMul(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::lhs_load;
    using base_t::rhs_load;

    /*!
     * \brief Compute the vectorized iterations of the loop using aligned store operations
     * \param first The index when to start
     */
    void operator()() const {
        constexpr const bool remainder = !padding || !all_padded<L_Expr, R_Expr>::value;

        const size_t last = remainder ? _last : _first + alloc_size<typename base_t::lhs_value_type>(_size);

        std::size_t i = _first;

        for (; i + IT::size * 4 - 1 < last; i += IT::size * 4) {
            lhs.template store<vect_impl>(vect_impl::template mul<Cx>(lhs_load(i), rhs_load(i)), i);
            lhs.template store<vect_impl>(vect_impl::template mul<Cx>(lhs_load(i + 1 * IT::size), rhs_load(i + 1 * IT::size)), i + 1 * IT::size);
            lhs.template store<vect_impl>(vect_impl::template mul<Cx>(lhs_load(i + 2 * IT::size), rhs_load(i + 2 * IT::size)), i + 2 * IT::size);
            lhs.template store<vect_impl>(vect_impl::template mul<Cx>(lhs_load(i + 3 * IT::size), rhs_load(i + 3 * IT::size)), i + 3 * IT::size);
        }

        for (; i + IT::size - 1 < last; i += IT::size) {
            lhs.template store<vect_impl>(vect_impl::template mul<Cx>(lhs_load(i), rhs_load(i)), i);
        }

        for (; remainder && i < last; ++i) {
            lhs_m[i] *= rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign div
 */
template <typename L_Expr, typename R_Expr>
struct AssignDiv {
    using value_type = value_t<L_Expr>;

    mutable value_type* lhs;         ///< The left hand side
    R_Expr& rhs;              ///< The right hand side
    const std::size_t _first; ///< The first index to assign
    const std::size_t _last;  ///< The last index to assign
    const std::size_t _size;  ///< The size to assign

    /*!
     * \brief Constuct a new AssignDiv
     * \param lhs The lhs memory
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    AssignDiv(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i] /= rhs[i];
                lhs[i + 1] /= rhs[i + 1];
                lhs[i + 2] /= rhs[i + 2];
                lhs[i + 3] /= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] /= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign div
 */
template <vector_mode_t V, typename L_Expr, typename R_Expr>
struct VectorizedAssignDiv : vectorized_base<V, L_Expr, R_Expr, VectorizedAssignDiv<V, L_Expr, R_Expr>> {
    static constexpr const bool Cx = is_complex_t<value_t<L_Expr>>::value; ///< Indicates if it is a complex division

    using base_t    = vectorized_base<V, L_Expr, R_Expr, VectorizedAssignDiv<V, L_Expr, R_Expr>>; ///< The base type
    using IT        = typename base_t::IT;                                                        ///< The intrisic type
    using vect_impl = typename base_t::vect_impl;                                                 ///< The vector implementation

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    /*!
     * \brief Constuct a new VectorizedAssignDiv
     * \param lhs The lhs expression
     * \param rhs The rhs expression
     * \param first Index to the first element to assign
     * \param last Index to the last element to assign
     */
    VectorizedAssignDiv(L_Expr& lhs, R_Expr& rhs, std::size_t first, std::size_t last)
            : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::lhs_load;
    using base_t::rhs_load;

    /*!
     * \brief Compute the vectorized iterations of the loop using aligned store operations
     * \param first The index when to start
     */
    void operator()() const {
        constexpr const bool remainder = !padding || !all_padded<L_Expr, R_Expr>::value;

        const size_t last = remainder ? _last : _first + alloc_size<typename base_t::lhs_value_type>(_size);

        std::size_t i = _first;

        for (; i + IT::size * 4 - 1 < last; i += IT::size * 4) {
            lhs.template store<vect_impl>(vect_impl::template div<Cx>(lhs_load(i), rhs_load(i)), i);
            lhs.template store<vect_impl>(vect_impl::template div<Cx>(lhs_load(i + 1 * IT::size), rhs_load(i + 1 * IT::size)), i + 1 * IT::size);
            lhs.template store<vect_impl>(vect_impl::template div<Cx>(lhs_load(i + 2 * IT::size), rhs_load(i + 2 * IT::size)), i + 2 * IT::size);
            lhs.template store<vect_impl>(vect_impl::template div<Cx>(lhs_load(i + 3 * IT::size), rhs_load(i + 3 * IT::size)), i + 3 * IT::size);
        }

        for (; i + IT::size - 1 < last; i += IT::size) {
            lhs.template store<vect_impl>(vect_impl::template div<Cx>(lhs_load(i), rhs_load(i)), i);
        }

        for (; remainder && i < last; ++i) {
            lhs_m[i] /= rhs[i];
        }
    }
};

} //end of namespace detail

} //end of namespace etl
