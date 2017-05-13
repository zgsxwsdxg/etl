//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include "test.hpp"
#include "conv_test.hpp"

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/0", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 5, 5> I;
    etl::fast_matrix<T, 10, 4, 3, 3> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 7, 7> ref;
    etl::fast_matrix<T, 4, 3, 7, 7> c;

    ref = etl::conv_4d_valid_filter(I, K, 1, 1, 2, 2);
    c = etl::conv_4d_backward_filter(I, K, 1,1,0,0);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/1", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 5, 5> I;
    etl::fast_matrix<T, 10, 4, 3, 3> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 5, 5> ref;
    etl::fast_matrix<T, 4, 3, 5, 5> c;

    ref = etl::conv_4d_valid_filter(I, K, 1,1,1,1);
    c = etl::conv_4d_backward_filter(I, K, 1,1,1,1);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/2", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 7, 7> I;
    etl::fast_matrix<T, 10, 4, 3, 3> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 5, 5> ref;
    etl::fast_matrix<T, 4, 3, 5, 5> c;

    c = etl::conv_4d_backward_filter(I, K, 1,1,2,2);
    ref = etl::conv_4d_valid_filter(I, K, 1,1,0,0);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/3", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 16, 16> I;
    etl::fast_matrix<T, 10, 4, 7, 7> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 12, 12> ref;
    etl::fast_matrix<T, 4, 3, 12, 12> c;

    c = etl::conv_4d_backward_filter(I, K, 1,1,5,5);
    ref = etl::conv_4d_valid_filter(I, K, 1,1,1,1);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/4", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 4, 4> I;
    etl::fast_matrix<T, 10, 4, 5, 5> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 11, 11> ref;
    etl::fast_matrix<T, 4, 3, 11, 11> c;

    c = etl::conv_4d_backward_filter(I, K, 2,2,0,0);

    auto I_s = etl::impl::common::inner_pad(I, 2, 2);
    ref = etl::conv_4d_valid_filter(I_s, K, 1,1,4,4);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}

TEMPLATE_TEST_CASE_2("conv/4d/backward/filter/dyn/5", "[conv][conv4][backward_filter]", T, float, double) {
    etl::fast_matrix<T, 10, 3, 4, 4> I;
    etl::fast_matrix<T, 10, 4, 5, 5> K;

    I = etl::sequence_generator(3.0) * 0.4;
    K = etl::sequence_generator(2.0) * 0.3;

    etl::fast_matrix<T, 4, 3, 9, 9> ref;
    etl::fast_matrix<T, 4, 3, 9, 9> c;

    c = etl::conv_4d_backward_filter(I, K, 2,2,1,1);

    auto I_s = etl::impl::common::inner_pad(I, 2, 2);
    ref = etl::conv_4d_valid_filter(I_s, K, 1, 1, 3, 3);

    REQUIRE_DIRECT(approx_equals(c, ref, base_eps));
}
