//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file
 * \brief Implementations for basic functions.
 *
 * For CPU, these functions are simply vectorized, but for GPU, they are put on
 * to the GPU.
 */

#pragma once

//Include the implementations
#include "etl/impl/cudnn/sigmoid.hpp"

namespace etl {

namespace detail {

/*!
 * \brief Logistic sigmoid
 */
struct sigmoid {
    /*!
     * \brief Apply the functor to e
     */
    template <typename E, typename C>
    static void apply(E&& e, C&& c) {
        impl::cudnn::sigmoid(e, c);
    }

    /*!
     * \brief Return the name of the function
     */
    static constexpr const char* name(){
        return "sigmoid";
    }
};

/*!
 * \brief Logistic sigmoid
 */
struct relu {
    /*!
     * \brief Apply the functor to e
     */
    template <typename E, typename C>
    static void apply(E&& e, C&& c) {
        impl::cudnn::relu(e, c);
    }

    /*!
     * \brief Return the name of the function
     */
    static constexpr const char* name(){
        return "relu";
    }
};

/*!
 * \brief Logistic sigmoid backward
 */
struct sigmoid_backward {
    /*!
     * \brief Apply the functor to e
     */
    template <typename O, typename E, typename C>
    static void apply(O&& o, E&& e, C&& c) {
        impl::cudnn::sigmoid_backward(o, e, c);
    }

    /*!
     * \brief Return the name of the function
     */
    static constexpr const char* name(){
        return "sigmoid'";
    }
};

/*!
 * \brief Logistic sigmoid backward
 */
struct relu_backward {
    /*!
     * \brief Apply the functor to e
     */
    template <typename O, typename E, typename C>
    static void apply(O&& o, E&& e, C&& c) {
        impl::cudnn::relu_backward(o, e, c);
    }

    /*!
     * \brief Return the name of the function
     */
    static constexpr const char* name(){
        return "relu'";
    }
};

} //end of namespace detail

} //end of namespace etl