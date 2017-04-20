//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include "test.hpp"
#include "conv_test.hpp"

CONV4_VALID_BACK_TEST_CASE("conv/4d/valid/back/1", "[conv][conv4][back][valid]") {
    etl::fast_matrix<T, 7, 9, 5, 5> I;
    etl::fast_matrix<T, 9, 2, 3, 3> K;

    I = etl::sequence_generator(1.0) * 0.04;
    K = etl::sequence_generator(2.0) * 0.03;

    etl::fast_matrix<T, 7, 2, 3, 3> ref;
    etl::fast_matrix<T, 7, 2, 3, 3> c;

    SELECTED_SECTION(etl::conv_impl::STD) {
        ref = 0.0;
        for (std::size_t i = 0; i < etl::dim<0>(I); ++i) {
            for (std::size_t c = 0; c < etl::dim<1>(K); ++c) {
                for (std::size_t k = 0; k < etl::dim<0>(K); ++k) {
                    ref(i)(c) += conv_2d_valid(I(i)(k), K(k)(c));
                }
            }
        }
    }

    Impl::apply(I, K, c);

    for (std::size_t i = 0; i < ref.size(); ++i) {
        REQUIRE_EQUALS_APPROX_E(c[i], ref[i], base_eps * 10);
    }
}

CONV4_VALID_BACK_FLIPPED_TEST_CASE("conv/4d/valid/back/2", "[conv][conv4][back][valid]") {
    etl::fast_matrix<T, 7, 9, 5, 5> I;
    etl::fast_matrix<T, 9, 2, 3, 3> K;

    I = etl::sequence_generator(1.0) * 0.04;
    K = etl::sequence_generator(2.0) * 0.03;

    etl::fast_matrix<T, 7, 2, 3, 3> ref;
    etl::fast_matrix<T, 7, 2, 3, 3> c;

    SELECTED_SECTION(etl::conv_impl::STD) {
        ref = 0.0;
        for (std::size_t i = 0; i < etl::dim<0>(I); ++i) {
            for (std::size_t c = 0; c < etl::dim<1>(K); ++c) {
                for (std::size_t k = 0; k < etl::dim<0>(K); ++k) {
                    ref(i)(c) += conv_2d_valid_flipped(I(i)(k), K(k)(c));
                }
            }
        }
    }

    Impl::apply(I, K, c);

    for (std::size_t i = 0; i < ref.size(); ++i) {
        REQUIRE_EQUALS_APPROX_E(c[i], ref[i], base_eps * 10);
    }
}