//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#ifndef ETL_STOP_HPP
#define ETL_STOP_HPP

#include "tmp.hpp"

namespace etl {

template<typename T,
    enable_if_all_u<
        is_etl_expr<T>::value,
        not_u<etl_traits<T>::is_value>::value,
        not_u<etl_traits<T>::is_fast>::value
    > = detail::dummy>
auto s(const T& value){
    //Sizes will be directly propagated
    return dyn_matrix<typename T::value_type, etl_traits<T>::dimensions()>(value);
}

template<typename M, typename Sequence>
struct build_matrix_type;

template<typename M, std::size_t... I>
struct build_matrix_type<M, index_sequence<I...>> {
    using type = fast_matrix<typename M::value_type, etl_traits<M>::template dim<I>()...>;
};

template<typename T,
    enable_if_all_u<
        is_etl_expr<T>::value,
        not_u<etl_traits<T>::is_value>::value,
        etl_traits<T>::is_fast
    > = detail::dummy>
auto s(const T& value){
    return typename build_matrix_type<T, make_index_sequence<etl_traits<T>::dimensions()>>::type(value);
}

} //end of namespace etl

#endif
