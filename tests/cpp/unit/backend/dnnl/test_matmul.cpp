/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <functional>
#include <random>

#include "interface/c_types_map.hpp"

#include "gtest/gtest.h"

#include "cpp/unit/backend/dnnl/dnnl_test_common.hpp"
#include "cpp/unit/unit_test_common.hpp"
#include "cpp/unit/utils.hpp"

namespace impl = dnnl::graph::impl;
namespace utils = dnnl::graph::tests::unit::utils;

TEST(Execute, MatmulFp32) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);
    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {-2.0, -1.5};
    test::vector<float> bias_data {3.75};
    test::vector<float> ref_dst_data {10};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t bias_ts(bias, &eng, bias_data.data());
    impl::tensor_t dst_ts(dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }

    // test with second iteration to see
    // if memory cache works correctly
    test::vector<float> src_data2 {-2.0, -1.5};
    test::vector<float> weight_data2 {-1.0, -1.0};
    test::vector<float> bias_data2 {1.5};
    test::vector<float> ref_dst_data2 {5};
    test::vector<float> dst_data2(ref_dst_data2.size(), 0.0);

    impl::tensor_t src_ts2(src, &eng, src_data2.data());
    impl::tensor_t weight_ts2(weight, &eng, weight_data2.data());
    impl::tensor_t bias_ts2(bias, &eng, bias_data2.data());
    impl::tensor_t dst_ts2(dst, &eng, dst_data2.data());

    cp.execute(&strm, {src_ts2, weight_ts2, bias_ts2}, {dst_ts2});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data2.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data2[i], ref_dst_data2[i]);
    }
}

TEST(Execute, MatmulF16F16F16) {
    impl::op_t matmul_op(impl::op_kind::MatMul);

    impl::engine_t &eng = get_engine();
    SKIP_IF(eng.kind() != impl::engine_kind::gpu,
            "Skip fp16 test for non-GPU device.");
#ifndef NDEBUG
    SKIP_IF(eng.kind() == impl::engine_kind::gpu,
            "Temporarily skip for primitive bug.");
#endif

    // in real use case, the data type should be half
    test::vector<float> src_data {-2.0, -1.5, -1.0, -0.5, 0.0, 0.5};
    test::vector<float> weight_data {
            -2.0, -1.5, -1.0, -0.5, 0.0, 0.5, -2.0, -1.5};
    test::vector<float> dst_data(12, 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f16);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f16);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f16);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts}, {dst_ts});
    strm.wait();
}

TEST(Execute, MatmulBf16Bf16Bf16) {
    impl::op_t matmul_op(impl::op_kind::MatMul);

    impl::engine_t &eng = get_engine();
    static auto isa = dnnl_get_effective_cpu_isa();
    SKIP_IF(isa < dnnl_cpu_isa_avx512_core
                    && eng.kind() == impl::engine_kind::cpu,
            "Skip bf16 examples for systems that do not support avx512_core.");

    // in real use case, the data type should be half
    test::vector<float> src_data {-2.0, -1.5, -1.0, -0.5, 0.0, 0.5};
    test::vector<float> weight_data {
            -2.0, -1.5, -1.0, -0.5, 0.0, 0.5, -2.0, -1.5};
    test::vector<float> dst_data(12, 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::bf16);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2}, impl::data_type::bf16);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(2, {1}, impl::data_type::bf16);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts}, {dst_ts});
    strm.wait();
}

TEST(Execute, MatmulNdx1d) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::vector<int64_t>> weight_shapes {{16}, {16, 1}};
    std::vector<std::vector<int64_t>> src_shapes {
            {4, 2, 16}, {6, 4, 2, 16}, {8, 6, 4, 2, 16}};
    std::vector<std::vector<int64_t>> dst_shapes {{4, 2}, {6, 4, 2},
            {8, 6, 4, 2}, {4, 2, 1}, {6, 4, 2, 1}, {8, 6, 4, 2, 1}};

    // Initialize
    std::default_random_engine generator;
    std::normal_distribution<float> distribution(0.0f, 1.0f);

    for (size_t w_idx = 0; w_idx < weight_shapes.size(); ++w_idx) {
        for (size_t idx = 0; idx < src_shapes.size(); idx++) {
            auto src_shape = src_shapes[idx];
            auto dst_shape = dst_shapes[idx + w_idx * src_shapes.size()];

            test::vector<float> src_data(product(src_shape));
            test::vector<float> weight_data(product(weight_shapes[w_idx]));
            test::vector<float> dst_data(product(dst_shape));
            test::vector<float> ref_dst_data(product(dst_shape), 0.0);

            std::generate(src_data.begin(), src_data.end(),
                    [&]() { return distribution(generator); });
            std::generate(weight_data.begin(), weight_data.end(),
                    [&]() { return distribution(generator); });

            // prepare logical tensor
            impl::logical_tensor_t src = utils::logical_tensor_init(
                    0, src_shape, impl::data_type::f32);
            impl::logical_tensor_t weight = utils::logical_tensor_init(
                    1, weight_shapes[w_idx], impl::data_type::f32);
            impl::logical_tensor_t dst = utils::logical_tensor_init(
                    2, dst_shape, impl::data_type::f32);

            impl::op_t matmul_op(impl::op_kind::MatMul);
            matmul_op.add_input(src);
            matmul_op.add_input(weight);
            matmul_op.add_output(dst);

            impl::graph_t g(engine.kind());
            g.add_op(&matmul_op);
            g.build_graph();

            impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
            apass->run(g);
            ASSERT_EQ(g.get_num_partitions(), 1U);
            auto part = g.get_partitions()[0];

            // compile
            impl::partition_t p;
            p.init(part);

            impl::compiled_partition_t cp(p);

            std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
            std::vector<const impl::logical_tensor_t *> outputs {&dst};

            ASSERT_EQ(p.compile(&cp, inputs, outputs, &engine),
                    impl::status::success);

            impl::tensor_t src_ts(src, &engine, src_data.data());
            impl::tensor_t weight_ts(weight, &engine, weight_data.data());
            impl::tensor_t dst_ts(dst, &engine, dst_data.data());

            ASSERT_EQ(cp.execute(&strm, {src_ts, weight_ts}, {dst_ts}),
                    impl::status::success);
            strm.wait();

            // compute the ref results
            size_t M = product(src_shape)
                    / static_cast<size_t>(src_shape.back());
            size_t K = static_cast<size_t>(src_shape.back());
            size_t N = weight_shapes[w_idx].size() == 1
                    ? static_cast<size_t>(1)
                    : static_cast<size_t>(weight_shapes[w_idx].back());
            // size_t N = static_cast<size_t>(1);
            for (size_t m = 0; m < M; m++) {
                for (size_t n = 0; n < N; n++) {
                    for (size_t k = 0; k < K; k++)
                        ref_dst_data[m * N + n]
                                += src_data[m * K + k] * weight_data[k * N + n];
                }
            }

            // FIXME(qun) the max error will be bigger than 1e-6
            for (size_t i = 0; i < ref_dst_data.size(); ++i) {
                ASSERT_NEAR(dst_data[i], ref_dst_data[i], 3e-6f);
            }
        }
    }
}

TEST(Execute, Matmul1dxNd) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::vector<int64_t>> src_shapes {{16}, {1, 16}};
    std::vector<std::vector<int64_t>> weight_shapes {
            {4, 16, 2}, {6, 4, 16, 2}, {8, 6, 4, 16, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {{4, 2}, {6, 4, 2},
            {8, 6, 4, 2}, {4, 1, 2}, {6, 4, 1, 2}, {8, 6, 4, 1, 2}};

    // Initialize
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

    for (size_t idx = 0; idx < src_shapes.size(); ++idx) {
        for (size_t w_idx = 0; w_idx < weight_shapes.size(); w_idx++) {
            auto src_shape = src_shapes[idx];
            auto dst_shape = dst_shapes[w_idx + idx * weight_shapes.size()];

            test::vector<float> src_data(product(src_shape));
            test::vector<float> weight_data(product(weight_shapes[w_idx]));
            test::vector<float> dst_data(product(dst_shape));
            test::vector<float> ref_dst_data(product(dst_shape), 0.0);

            std::generate(src_data.begin(), src_data.end(),
                    [&]() { return distribution(generator); });
            std::generate(weight_data.begin(), weight_data.end(),
                    [&]() { return distribution(generator); });

            // prepare logical tensor
            impl::logical_tensor_t src = utils::logical_tensor_init(
                    0, src_shape, impl::data_type::f32);
            impl::logical_tensor_t weight = utils::logical_tensor_init(
                    1, weight_shapes[w_idx], impl::data_type::f32);
            impl::logical_tensor_t dst = utils::logical_tensor_init(
                    2, dst_shape, impl::data_type::f32);

            impl::op_t matmul_op(impl::op_kind::MatMul);
            matmul_op.add_input(src);
            matmul_op.add_input(weight);
            matmul_op.add_output(dst);

            impl::graph_t g(engine.kind());
            g.add_op(&matmul_op);
            g.build_graph();

            impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
            apass->run(g);
            ASSERT_EQ(g.get_num_partitions(), 1U);
            auto part = g.get_partitions()[0];

            // compile
            impl::partition_t p;
            p.init(part);

            impl::compiled_partition_t cp(p);

            std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
            std::vector<const impl::logical_tensor_t *> outputs {&dst};

            p.compile(&cp, inputs, outputs, &engine);

            impl::tensor_t src_ts(src, &engine, src_data.data());
            impl::tensor_t weight_ts(weight, &engine, weight_data.data());
            impl::tensor_t dst_ts(dst, &engine, dst_data.data());

            cp.execute(&strm, {src_ts, weight_ts}, {dst_ts});
            strm.wait();

            // compute the ref results
            size_t M = product(dst_shape)
                    / static_cast<size_t>(dst_shape.back());
            size_t K = static_cast<size_t>(src_shape.back());
            size_t N = weight_shapes[w_idx].back();
            for (size_t m = 0; m < M; m++) {
                for (size_t n = 0; n < N; n++) {
                    for (size_t k = 0; k < K; k++)
                        ref_dst_data[m * N + n] += src_data[k]
                                * weight_data[m * K * N + k * N + n];
                }
            }

            // FIXME(qun) the max error will be bigger than 1e-6
            for (size_t i = 0; i < ref_dst_data.size(); ++i) {
                ASSERT_NEAR(dst_data[i], ref_dst_data[i], 3e-6f);
            }
        }
    }
}

TEST(Execute, Matmul3dx3d) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    test::vector<float> weight_data {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    test::vector<float> ref_dst_data {2.0, 2.0, 2.0, 2.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {4, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {4, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(2, {4, 1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasAdd) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::BiasAdd, "add_op");
    add_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> post_src_data {-2.0};
    test::vector<float> ref_dst_data {-3.75};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t post_src
            = utils::logical_tensor_init(3, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            4, impl::data_type::f32, impl::layout_type::strided);
    impl::logical_tensor_t add_dst = utils::logical_tensor_init(
            5, impl::data_type::f32, impl::layout_type::strided);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(post_src);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &post_src};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t post_src_ts(post_src, &engine, post_src_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, post_src_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasAddPerTensorBroadcast) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0, 2.0};
    test::vector<float> post_src_data {-2.0};
    test::vector<float> ref_dst_data {-5.0, 3.0, -4.0, 2.25};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    std::vector<impl::dims> post_src_shapes = {{1}, {1, 1}};

    for (auto &post_src_shape : post_src_shapes) {
        impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
        impl::op_t add_op(1, impl::op_kind::Add, "add_op");

        // prepare logical tensor
        impl::logical_tensor_t src
                = utils::logical_tensor_init(0, {2, 1}, impl::data_type::f32);
        impl::logical_tensor_t weight
                = utils::logical_tensor_init(1, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t bias
                = utils::logical_tensor_init(2, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t post_src = utils::logical_tensor_init(
                3, post_src_shape, impl::data_type::f32);
        impl::logical_tensor_t dst
                = utils::logical_tensor_init(4, {2, 2}, impl::data_type::f32);
        impl::logical_tensor_t add_dst
                = utils::logical_tensor_init(5, {2, 2}, impl::data_type::f32);

        matmul_op.add_input(src);
        matmul_op.add_input(weight);
        matmul_op.add_input(bias);
        matmul_op.add_output(dst);
        add_op.add_input(dst);
        add_op.add_input(post_src);
        add_op.add_output(add_dst);

        impl::graph_t g(engine.kind());
        ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
        ASSERT_EQ(g.add_op(&add_op), impl::status::success);
        g.build_graph();

        impl::pass::pass_base_ptr apass
                = get_pass("matmul_bias_post_ops_chain_fusion");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> inputs {
                &src, &weight, &bias, &post_src};
        std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

        p.compile(&cp, inputs, outputs, &engine);

        impl::tensor_t src_ts(src, &engine, src_data.data());
        impl::tensor_t weight_ts(weight, &engine, weight_data.data());
        impl::tensor_t bias_ts(bias, &engine, bias_data.data());
        impl::tensor_t post_src_ts(post_src, &engine, post_src_data.data());
        impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

        cp.execute(&strm, {src_ts, weight_ts, bias_ts, post_src_ts}, {dst_ts});
        strm.wait();
        for (size_t i = 0; i < ref_dst_data.size(); ++i) {
            ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
        }
    }
}

TEST(Execute, MatmulBiasAddPerChannelBroadcast) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0, 2.0};
    test::vector<float> post_src_data {-2.0, -1.0};
    test::vector<float> ref_dst_data {-5.0, 4.0, -4.0, 3.25};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    std::vector<impl::dims> post_src_shapes = {{2}, {1, 2}};

    for (auto &post_src_shape : post_src_shapes) {
        impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
        impl::op_t add_op(1, impl::op_kind::Add, "add_op");
        // prepare logical tensor
        impl::logical_tensor_t src
                = utils::logical_tensor_init(0, {2, 1}, impl::data_type::f32);
        impl::logical_tensor_t weight
                = utils::logical_tensor_init(1, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t bias
                = utils::logical_tensor_init(2, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t post_src = utils::logical_tensor_init(
                3, post_src_shape, impl::data_type::f32);
        impl::logical_tensor_t dst
                = utils::logical_tensor_init(4, {2, 2}, impl::data_type::f32);
        impl::logical_tensor_t add_dst
                = utils::logical_tensor_init(5, {2, 2}, impl::data_type::f32);

        matmul_op.add_input(src);
        matmul_op.add_input(weight);
        matmul_op.add_input(bias);
        matmul_op.add_output(dst);
        add_op.add_input(dst);
        add_op.add_input(post_src);
        add_op.add_output(add_dst);

        impl::graph_t g(engine.kind());
        ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
        ASSERT_EQ(g.add_op(&add_op), impl::status::success);
        g.build_graph();

        impl::pass::pass_base_ptr apass
                = get_pass("matmul_bias_post_ops_chain_fusion");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> inputs {
                &src, &weight, &bias, &post_src};
        std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

        p.compile(&cp, inputs, outputs, &engine);

        impl::tensor_t src_ts(src, &engine, src_data.data());
        impl::tensor_t weight_ts(weight, &engine, weight_data.data());
        impl::tensor_t bias_ts(bias, &engine, bias_data.data());
        impl::tensor_t post_src_ts(post_src, &engine, post_src_data.data());
        impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

        cp.execute(&strm, {src_ts, weight_ts, bias_ts, post_src_ts}, {dst_ts});
        strm.wait();
        for (size_t i = 0; i < ref_dst_data.size(); ++i) {
            ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
        }
    }
}

TEST(Compile, MatmulBiasAddUnsupportedBroadcast) {
    impl::engine_t &engine = get_engine();

    std::vector<impl::dims> post_src_shapes = {{3}, {1, 3}};

    for (auto &post_src_shape : post_src_shapes) {
        impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
        impl::op_t add_op(1, impl::op_kind::Add, "add_op");
        // prepare logical tensor
        impl::logical_tensor_t src
                = utils::logical_tensor_init(0, {2, 1}, impl::data_type::f32);
        impl::logical_tensor_t weight
                = utils::logical_tensor_init(1, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t bias
                = utils::logical_tensor_init(2, {1, 2}, impl::data_type::f32);
        impl::logical_tensor_t post_src = utils::logical_tensor_init(
                3, post_src_shape, impl::data_type::f32);
        impl::logical_tensor_t dst
                = utils::logical_tensor_init(4, {2, 2}, impl::data_type::f32);
        impl::logical_tensor_t add_dst
                = utils::logical_tensor_init(5, {2, 2}, impl::data_type::f32);

        matmul_op.add_input(src);
        matmul_op.add_input(weight);
        matmul_op.add_input(bias);
        matmul_op.add_output(dst);
        add_op.add_input(dst);
        add_op.add_input(post_src);
        add_op.add_output(add_dst);

        impl::graph_t g(engine.kind());
        ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
        ASSERT_EQ(g.add_op(&add_op), impl::status::success);
        g.build_graph();

        impl::pass::pass_base_ptr apass
                = get_pass("matmul_bias_post_ops_chain_fusion");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> inputs {
                &src, &weight, &bias, &post_src};
        std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

        ASSERT_EQ(p.compile(&cp, inputs, outputs, &engine),
                impl::status::invalid_shape);
    }
}

TEST(ExecuteSubgraphInt8, MatmulNdx2d) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));

        // random generate src, weight and bias data random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_out = 1;
        int64_t zp_src = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        qout_op.add_input(dst_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());

        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];
        ASSERT_EQ(part->get_ops().size(), 4U);

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                {dst_s8_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulNdx1d) {
    // compare results between: case 1: [quantize] - [dequantize] -
    // [fp32_matmul] - [quantize] case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::vector<int64_t>> src_shapes {{3, 8, 4}, {8, 4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 1}, {4}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 8, 1}, {8, 1}, {3, 8}, {8}};

    for (size_t i = 0; i < weight_shapes.size(); ++i) {
        for (size_t j = 0; j < src_shapes.size(); ++j) {
            // prepare fp32 data
            std::vector<int64_t> src_shape = src_shapes[j];
            std::vector<int64_t> weight_shape = weight_shapes[i];
            std::vector<int64_t> dst_shape
                    = dst_shapes[j + i * src_shapes.size()];

            test::vector<uint8_t> src_data(product(src_shape));
            test::vector<int8_t> weight_data(product(weight_shape));

            // random generate src, weight and bias data random seed = 7
            std::default_random_engine generator(7);
            std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
            std::uniform_real_distribution<float> s8_distribution(
                    -127.0f, 128.0f);
            std::generate(src_data.begin(), src_data.end(), [&]() {
                return static_cast<uint8_t>(u8_distribution(generator));
            });
            std::generate(weight_data.begin(), weight_data.end(), [&]() {
                return static_cast<int8_t>(s8_distribution(generator));
            });
            float scale_src = 1 / 255.f; // map to 0~255
            float scale_wei = 1 / 127.f;
            float scale_out = 1;
            int64_t zp_src = 0;
            int64_t zp_wei = 0;
            // The following cmd will be skiped by benchdnn, since oneDNN didn't
            // support reorder with zps on GPU: "./tests/benchdnn/benchdnn
            // --reorder --engine=gpu --mode=C --sdt=f32 --ddt=s8
            // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
            int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

            impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
            dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
            dqdata_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_src});
            dqdata_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_src});
            dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
            dqweight_op.set_attr<std::string>(
                    impl::op_attr::qtype, "per_tensor");
            dqweight_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_wei});
            dqweight_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_wei});
            dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
            matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
            matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

            impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
            qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
            qout_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_out});
            qout_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_out});
            qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            // prepare logical tensor
            impl::logical_tensor_t src_u8 = utils::logical_tensor_init(
                    1, src_shape, impl::data_type::u8);
            impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                    2, src_shape, impl::data_type::f32);
            impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                    4, weight_shape, impl::data_type::s8);
            impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                    5, weight_shape, impl::data_type::f32);
            impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                    7, dst_shape, impl::data_type::f32);
            impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
                    8, dst_shape, impl::data_type::s8);

            dqdata_op.add_input(src_u8);
            dqdata_op.add_output(src_f32_dq);

            dqweight_op.add_input(weight_s8);
            dqweight_op.add_output(weight_f32_dq);

            matmul_op.add_input(src_f32_dq);
            matmul_op.add_input(weight_f32_dq);
            matmul_op.add_output(dst_f32);

            qout_op.add_input(dst_f32);
            qout_op.add_output(dst_s8);

            impl::graph_t g(engine.kind());
            g.add_op(&dqdata_op);
            g.add_op(&dqweight_op);
            g.add_op(&matmul_op);
            g.add_op(&qout_op);
            g.build_graph();

            impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
            impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
            // -------------------------case 1----------------------------------
            test::vector<int8_t> case1_out_data(product(dst_shape));
            impl::tensor_t dst_s8_case1_ts(
                    dst_s8, &engine, case1_out_data.data());
            ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts}, {dst_s8_case1_ts},
                              engine, strm),
                    impl::status::success);

            // -------------------------case 2----------------------------------
            impl::pass::pass_base_ptr apass
                    = get_pass(engine.kind() == impl::engine_kind::gpu
                                    ? "int8_matmul_post_ops_fusion_gpu"
                                    : "int8_matmul_post_ops_fusion_cpu");
            apass->run(g);
            ASSERT_EQ(g.get_num_partitions(), 1U);
            auto part = g.get_partitions()[0];

            // compile
            impl::partition_t p;
            p.init(part);

            impl::compiled_partition_t cp(p);

            std::vector<const impl::logical_tensor_t *> lt_ins {
                    &src_u8, &weight_s8};
            std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

            p.compile(&cp, lt_ins, lt_outs, &engine);

            test::vector<int8_t> case2_out_data(product(dst_shape));
            impl::tensor_t dst_s8_case2_ts(
                    dst_s8, &engine, case2_out_data.data());
            cp.execute(&strm, {src_u8_ts, weight_s8_ts}, {dst_s8_case2_ts});
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(ExecuteSubgraphInt8, MatmulNdx2dWithTranspose) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{2, 4}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};

    for (size_t i = 0; i < src_shapes.size(); ++i) {
        for (size_t j = 0; j < weight_shapes.size(); ++j) {
            // prepare fp32 data
            std::vector<int64_t> src_shape = src_shapes[i];
            std::vector<int64_t> weight_shape = weight_shapes[j];
            std::vector<int64_t> bias_shape {1};
            std::vector<int64_t> dst_shape = dst_shapes[i];

            test::vector<uint8_t> src_data(product(src_shape));
            test::vector<int8_t> weight_data(product(weight_shape));
            test::vector<float> bias_data(product(bias_shape));

            // random generate src, weight and bias data random seed = 7
            std::default_random_engine generator(7);
            std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
            std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
            std::uniform_real_distribution<float> s8_distribution(
                    -127.0f, 128.0f);
            std::generate(src_data.begin(), src_data.end(), [&]() {
                return static_cast<uint8_t>(u8_distribution(generator));
            });
            std::generate(weight_data.begin(), weight_data.end(), [&]() {
                return static_cast<int8_t>(s8_distribution(generator));
            });
            std::generate(bias_data.begin(), bias_data.end(),
                    [&]() { return f32_distribution(generator); });
            float scale_src = 1 / 255.f; // map to 0~255
            float scale_wei = 1 / 127.f;
            float scale_out = 1;
            int64_t zp_src = 0;
            int64_t zp_wei = 0;
            // The following cmd will be skiped by benchdnn, since oneDNN didn't
            // support reorder with zps on GPU: "./tests/benchdnn/benchdnn
            // --reorder --engine=gpu --mode=C --sdt=f32 --ddt=s8
            // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
            int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

            // -------------------------case 1----------------------------------
            impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
            dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
            dqdata_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_src});
            dqdata_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_src});
            dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
            dqweight_op.set_attr<std::string>(
                    impl::op_attr::qtype, "per_tensor");
            dqweight_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_wei});
            dqweight_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_wei});
            dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
            matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
            matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

            impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
            qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
            qout_op.set_attr<std::vector<int64_t>>(
                    impl::op_attr::zps, {zp_out});
            qout_op.set_attr<std::vector<float>>(
                    impl::op_attr::scales, {scale_out});
            qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

            // prepare logical tensor
            impl::logical_tensor_t src_u8 = utils::logical_tensor_init(
                    1, src_shape, impl::data_type::u8);
            impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                    2, src_shape, impl::data_type::f32);
            impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                    4, weight_shape, impl::data_type::s8);
            impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                    5, weight_shape, impl::data_type::f32);
            impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                    6, bias_shape, impl::data_type::f32);
            impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                    7, dst_shape, impl::data_type::f32);
            impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
                    8, dst_shape, impl::data_type::s8);

            dqdata_op.add_input(src_u8);
            dqdata_op.add_output(src_f32_dq);

            dqweight_op.add_input(weight_s8);
            dqweight_op.add_output(weight_f32_dq);

            matmul_op.add_input(src_f32_dq);
            matmul_op.add_input(weight_f32_dq);
            matmul_op.add_input(bias_f32);
            matmul_op.add_output(dst_f32);

            qout_op.add_input(dst_f32);
            qout_op.add_output(dst_s8);

            impl::graph_t g(engine.kind());
            g.add_op(&dqdata_op);
            g.add_op(&dqweight_op);
            g.add_op(&matmul_op);
            g.add_op(&qout_op);
            g.build_graph();

            impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
            impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
            impl::tensor_t bias_f32_ts
                    = impl::tensor_t(bias_f32, &engine, bias_data.data());
            // -------------------------case 1----------------------------------
            test::vector<int8_t> case1_out_data(product(dst_shape));
            impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
            ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                              {dst_s8_ts}, engine, strm),
                    impl::status::success);

            // -------------------------case 2----------------------------------
            impl::pass::pass_base_ptr apass
                    = get_pass(engine.kind() == impl::engine_kind::gpu
                                    ? "int8_matmul_post_ops_fusion_gpu"
                                    : "int8_matmul_post_ops_fusion_cpu");
            apass->run(g);
            ASSERT_EQ(g.get_num_partitions(), 1U);
            auto part = g.get_partitions()[0];

            // compile
            impl::partition_t p;
            p.init(part);

            impl::compiled_partition_t cp(p);

            std::vector<const impl::logical_tensor_t *> lt_ins {
                    &src_u8, &weight_s8, &bias_f32};
            std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

            p.compile(&cp, lt_ins, lt_outs, &engine);

            test::vector<int8_t> case2_out_data(product(dst_shape));
            impl::tensor_t dst_s8_case2_ts(
                    dst_s8, &engine, case2_out_data.data());
            cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                    {dst_s8_case2_ts});
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasSumNdx2d) {
    // skip the test on AArch64 or some older machine without avx support
    SKIP_IF(dnnl_get_effective_cpu_isa() < dnnl_cpu_isa_avx,
            "skip on machine without AVX");
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::string> other_qtypes = {"symmetric", "asymmetric"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for_(const auto &other_qtype : other_qtypes)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<int8_t> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_other = 1 / 127.f;
        float scale_out = 1;
        int64_t zp_src = 0;
        // post-sum and reorder didn't support zps on gpu
        int64_t zp_other = other_qtype == "symmetric"
                        || engine.kind() == impl::engine_kind::gpu
                ? 0
                : 128;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqother_op(5, impl::op_kind::Dequantize, "dqother_op");
        dqother_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqother_op.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_other});
        dqother_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        dqother_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t add_op(6, impl::op_kind::Add, "add_op");

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_s8 = utils::logical_tensor_init(
                10, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_f32_dq = utils::logical_tensor_init(
                11, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_add_f32 = utils::logical_tensor_init(
                12, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        dqother_op.add_input(other_s8);
        dqother_op.add_output(other_f32_dq);

        add_op.add_input(dst_f32);
        add_op.add_input(other_f32_dq);
        add_op.add_output(dst_add_f32);

        qout_op.add_input(dst_add_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&dqother_op);
        g.add_op(&add_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        impl::tensor_t other_s8_ts(other_s8, &engine, other_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g,
                          {src_u8_ts, weight_s8_ts, bias_f32_ts, other_s8_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];
        ASSERT_EQ(part->get_ops().size(), 6U);

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32, &other_s8};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts, other_s8_ts},
                {dst_s8_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasBinary) {
    // skip the test on AArch64 or some older machine without avx support
    SKIP_IF(dnnl_get_effective_cpu_isa() < dnnl_cpu_isa_avx,
            "skip on machine without AVX");
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_channel"};
    std::vector<impl::op_kind_t> binary_kinds {impl::op_kind::Multiply,
            impl::op_kind::Divide, impl::op_kind::Maximum,
            impl::op_kind::Minimum, impl::op_kind::Subtract};
    std::vector<std::vector<int64_t>> src_shapes {{3, 3, 8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {{3, 3, 8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for_(size_t j = 0; j < weight_shapes.size(); ++j)
    for (const auto &binary_kind : binary_kinds) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<float> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_out = 1;
        int64_t zp_src = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t binary_op(6, binary_kind, "binary_op");

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_f32 = utils::logical_tensor_init(
                9, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_binary_f32 = utils::logical_tensor_init(
                10, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        binary_op.add_input(dst_f32);
        binary_op.add_input(other_f32);
        binary_op.add_output(dst_binary_f32);

        qout_op.add_input(dst_binary_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&binary_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        impl::tensor_t other_f32_ts(other_f32, &engine, other_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g,
                          {src_u8_ts, weight_s8_ts, bias_f32_ts, other_f32_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];
        ASSERT_EQ(part->get_ops().size(), 5U);

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32, &other_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts, other_f32_ts},
                {dst_s8_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasAddMul) {
    // skip the test on AArch64 or some older machine without avx support
    SKIP_IF(dnnl_get_effective_cpu_isa() < dnnl_cpu_isa_avx,
            "skip on machine without AVX");
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::string> other_qtypes = {"symmetric", "asymmetric"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for_(const auto &other_qtype : other_qtypes)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // the following cmd is not implemented for gpu:
        // ./benchdnn --matmul --cfg=u8s8f32 --engine=gpu --stag=ab
        // --wtag=ab --dtag=ab --attr-oscale=common:0.000031
        // --attr-post-ops=sum:1+mul:f32:common:ab --attr-zero-points
        // =wei:per_oc:1 8x4:4x2:8x2
        if (engine.kind() == impl::engine_kind::gpu && qtype == "per_channel")
            continue;
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<int8_t> add_other_data(product(dst_shape));
        test::vector<float> mul_other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(add_other_data.begin(), add_other_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(mul_other_data.begin(), mul_other_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_other = 1 / 127.f;
        float scale_out = 1;
        int64_t zp_src = 0;
        // post-sum and reorder didn't support zps on gpu
        int64_t zp_other = other_qtype == "symmetric"
                        || engine.kind() == impl::engine_kind::gpu
                ? 0
                : 128;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(4, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqother_op(5, impl::op_kind::Dequantize, "dqother_op");
        dqother_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqother_op.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_other});
        dqother_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        dqother_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t add_op(6, impl::op_kind::Add, "add_op");

        impl::op_t mul_op(7, impl::op_kind::Multiply, "mul_op");

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_s8 = utils::logical_tensor_init(
                10, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_f32_dq = utils::logical_tensor_init(
                11, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_add_f32 = utils::logical_tensor_init(
                12, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t mul_src_f32 = utils::logical_tensor_init(
                13, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_mul_f32 = utils::logical_tensor_init(
                14, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        dqother_op.add_input(other_s8);
        dqother_op.add_output(other_f32_dq);

        add_op.add_input(dst_f32);
        add_op.add_input(other_f32_dq);
        add_op.add_output(dst_add_f32);

        mul_op.add_input(dst_add_f32);
        mul_op.add_input(mul_src_f32);
        mul_op.add_output(dst_mul_f32);

        qout_op.add_input(dst_mul_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&dqother_op);
        g.add_op(&add_op);
        g.add_op(&mul_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        impl::tensor_t add_other_s8_ts(
                other_s8, &engine, add_other_data.data());
        impl::tensor_t mul_other_f32_ts(
                mul_src_f32, &engine, mul_other_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g,
                          {src_u8_ts, weight_s8_ts, bias_f32_ts,
                                  add_other_s8_ts, mul_other_f32_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];
        ASSERT_EQ(part->get_ops().size(), 7U);

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32, &other_s8, &mul_src_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        cp.execute(&strm,
                {src_u8_ts, weight_s8_ts, bias_f32_ts, add_other_s8_ts,
                        mul_other_f32_ts},
                {dst_s8_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasSumNdx2dX8s8f32) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<int8_t> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_other = 1 / 127.f;
        int64_t zp_src = 0;
        int64_t zp_other = 0;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t dqother_op(4, impl::op_kind::Dequantize, "dqother_op");
        dqother_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqother_op.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_other});
        dqother_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        dqother_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t add_op(5, impl::op_kind::Add, "add_op");

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t other_s8
                = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t other_f32_dq = utils::logical_tensor_init(
                10, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_add_f32 = utils::logical_tensor_init(
                11, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        dqother_op.add_input(other_s8);
        dqother_op.add_output(other_f32_dq);

        add_op.add_input(dst_f32);
        add_op.add_input(other_f32_dq);
        add_op.add_output(dst_add_f32);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&dqother_op);
        g.add_op(&add_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        impl::tensor_t other_s8_ts(other_s8, &engine, other_data.data());
        // -------------------------case 1----------------------------------
        test::vector<float> case1_out_data(product(dst_shape));
        impl::tensor_t dst_f32_ts(dst_add_f32, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g,
                          {src_u8_ts, weight_s8_ts, bias_f32_ts, other_s8_ts},
                          {dst_f32_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32, &other_s8};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_add_f32};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<float> case2_out_data(product(dst_shape));
        impl::tensor_t dst_f32_case2_ts(
                dst_add_f32, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts, other_s8_ts},
                {dst_f32_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasNdx2dX8s8f32) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        int64_t zp_src = 0;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        // -------------------------case 1----------------------------------
        test::vector<float> case1_out_data(product(dst_shape));
        impl::tensor_t dst_f32_ts(dst_f32, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                          {dst_f32_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_f32};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<float> case2_out_data(product(dst_shape));
        impl::tensor_t dst_f32_case2_ts(
                dst_f32, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                {dst_f32_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulNdx2dX8s8f32) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        float scale_src = 1 / 255.f; // map to 0~255
        int64_t zp_src = 0;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_output(dst_f32);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        // -------------------------case 1----------------------------------
        test::vector<float> case1_out_data(product(dst_shape));
        impl::tensor_t dst_f32_ts(dst_f32, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts}, {dst_f32_ts}, engine,
                          strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_f32};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<float> case2_out_data(product(dst_shape));
        impl::tensor_t dst_f32_case2_ts(
                dst_f32, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts}, {dst_f32_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasGeluNdx2dX8s8f32) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        int64_t zp_src = 0;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t gelu_op(4, impl::op_kind::GELU, "gelu_op");

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t gelu_f32 = utils::logical_tensor_init(
                8, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        gelu_op.add_input(dst_f32);
        gelu_op.add_output(gelu_f32);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&gelu_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        // -------------------------case 1----------------------------------
        test::vector<float> case1_out_data(product(dst_shape));
        impl::tensor_t dst_f32_ts(gelu_f32, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                          {dst_f32_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&gelu_f32};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<float> case2_out_data(product(dst_shape));
        impl::tensor_t dst_f32_case2_ts(
                gelu_f32, &engine, case2_out_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts},
                {dst_f32_case2_ts});
        strm.wait();

        static auto isa = dnnl_get_effective_cpu_isa();
        if (engine.kind() == impl::engine_kind::cpu
                && isa < dnnl_cpu_isa_avx512_core_vnni)
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                    /*atol*/ 1.f));
        else
            ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                    /*atol*/ 1.f));
    }
}

TEST(Compile, MatmulAddGetInplacePair) {
    impl::engine_t &eng = get_engine();

    impl::graph_t agraph(eng.kind());
    impl::op_t mm {0, impl::op_kind::MatMul, "matmul"};
    impl::op_t add {1, impl::op_kind::Add, "add"};
    impl::op_t mm2 {2, impl::op_kind::MatMul, "matmul2"};

    impl::logical_tensor_t lt_mm_src = utils::logical_tensor_init(
            1, {1, 16, 4, 4}, impl::data_type::f32);
    impl::logical_tensor_t lt_mm_weight
            = utils::logical_tensor_init(2, {4, 4}, impl::data_type::f32);
    impl::logical_tensor_t lt_mm_out = utils::logical_tensor_init(
            3, {1, 16, 4, 4}, impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t lt_mm_src2 = utils::logical_tensor_init(
            4, {1, 16, 4, 4}, impl::data_type::f32);
    impl::logical_tensor_t lt_mm_weight2
            = utils::logical_tensor_init(5, {4, 4}, impl::data_type::f32);
    impl::logical_tensor_t lt_mm_out2 = utils::logical_tensor_init(
            6, {1, 16, 4, 4}, impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t lt_add_out = utils::logical_tensor_init(
            7, {1, 16, 4, 4}, impl::data_type::f32, impl::layout_type::any);
    mm.add_input(lt_mm_src);
    mm.add_input(lt_mm_weight);
    mm.add_output(lt_mm_out);
    mm2.add_input(lt_mm_src2);
    mm2.add_input(lt_mm_weight2);
    mm2.add_output(lt_mm_out2);
    add.add_input(lt_mm_out);
    add.add_input(lt_mm_out2);
    add.add_output(lt_add_out);

    ASSERT_EQ(agraph.add_op(&mm), impl::status::success);
    ASSERT_EQ(agraph.add_op(&mm2), impl::status::success);
    ASSERT_EQ(agraph.add_op(&add), impl::status::success);
    agraph.build_graph();
    ASSERT_EQ(agraph.num_ops(), 3U);

    impl::pass::pass_base_ptr apass1 = get_pass("matmul_post_ops_chain_fusion");
    impl::pass::pass_base_ptr apass2 = get_pass("matmul_pass");

    apass1->run(agraph);
    apass2->run(agraph);
    ASSERT_EQ(agraph.get_num_partitions(), 2U);
    auto part1 = agraph.get_partitions()[0]; // matmul_add
    auto part2 = agraph.get_partitions()[1]; // matmul
    impl::partition_t p1, p2;
    p1.init(part1);
    p2.init(part2);

    impl::compiled_partition_t cp1(p1), cp2(p2);

    // compile matmul partition
    std::vector<const impl::logical_tensor_t *> inputs2 {
            &lt_mm_src2, &lt_mm_weight2};
    std::vector<const impl::logical_tensor_t *> outputs2 {&lt_mm_out2};
    p2.compile(&cp2, inputs2, outputs2, &eng);
    cp2.query_logical_tensor(lt_mm_out2.id, &lt_mm_out2);

    // compile matmul_add partition
    std::vector<const impl::logical_tensor_t *> inputs1 {
            &lt_mm_src, &lt_mm_weight, &lt_mm_out2};
    std::vector<const impl::logical_tensor_t *> outputs1 {&lt_add_out};
    p1.compile(&cp1, inputs1, outputs1, &eng);

    std::vector<impl::inplace_pair_t> inplace_pairs = cp1.get_inplace_pairs();
    ASSERT_EQ(inplace_pairs.size(), 1U);
    ASSERT_EQ(inplace_pairs[0].input_id, lt_mm_out2.id);
    ASSERT_EQ(inplace_pairs[0].output_id, lt_add_out.id);
}

TEST(ExecuteSubgraphInt8, Matmul2dx3dWithTranspose) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::vector<int64_t>> src_shapes {{8, 4}};
    std::vector<std::vector<int64_t>> weight_shapes {{8, 2, 4}};
    std::vector<std::vector<int64_t>> dst_shapes {{8, 8, 2}};

    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {1};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<int8_t> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));

        // random generate src, weight and bias data random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> int8_distribution(0.0f, 100.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(int8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(), [&]() {
            return static_cast<int8_t>(int8_distribution(generator));
        });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f;
        float scale_wei = 1 / 127.f;
        float scale_out = 1;
        int64_t zp_src = 0;
        int64_t zp_wei = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        impl::op_t qdata_op(0, impl::op_kind::Quantize, "qdata_op");
        qdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        qdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        qdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
        qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_wei});
        qweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_wei});
        qweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqweight_op.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_wei});
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_wei});
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

        impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        qout_op.add_input(dst_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);
        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_s8, &bias_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        // execute
        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        test::vector<int8_t> dst_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, dst_data.data());
        cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_f32_ts}, {dst_s8_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasSumGetInplacePair) {
    // skip the test on AArch64 or some older machine without avx support
    SKIP_IF(dnnl_get_effective_cpu_isa() < dnnl_cpu_isa_avx,
            "skip on machine without AVX");
    impl::engine_t &engine = get_engine();
    SKIP_IF(engine.kind() == impl::engine_kind::gpu,
            "Skip for GPU - no inplace for layout mismatch.");

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::string> other_qtypes = {"symmetric", "asymmetric"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for_(const auto &other_qtype : other_qtypes)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        float scale_src = 1 / 255.f; // map to 0~255
        float scale_other = 1 / 127.f;
        float scale_out = 1;
        int64_t zp_src = 0;
        int64_t zp_other = other_qtype == "symmetric" ? 0 : 128;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t add_op(8, impl::op_kind::Add, "add_op");

        impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqdata_op2(9, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op2.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op2.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op2.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op2.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op2(10, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op2.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op2.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op2.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op2.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op2(11, impl::op_kind::MatMul, "matmul_op");
        matmul_op2.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op2.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op2(6, impl::op_kind::Quantize, "qother_op");
        qout_op2.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op2.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_other});
        qout_op2.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        qout_op2.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqout_o2p(7, impl::op_kind::Dequantize, "dqother_op");
        dqout_o2p.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqout_o2p.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_other});
        dqout_o2p.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        dqout_o2p.set_attr<int64_t>(impl::op_attr::axis, 0);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);

        impl::logical_tensor_t src_u8_2 = utils::logical_tensor_init(
                21, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq_2 = utils::logical_tensor_init(
                22, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8_2 = utils::logical_tensor_init(
                24, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq_2 = utils::logical_tensor_init(
                25, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32_2 = utils::logical_tensor_init(
                26, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32_2 = utils::logical_tensor_init(
                27, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8_2 = utils::logical_tensor_init(
                28, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t dst_f32_dq_2 = utils::logical_tensor_init(
                29, dst_shape, impl::data_type::f32);

        impl::logical_tensor_t dst_add_f32 = utils::logical_tensor_init(
                12, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);

        dqdata_op2.add_input(src_u8_2);
        dqdata_op2.add_output(src_f32_dq_2);
        dqweight_op2.add_input(weight_s8_2);
        dqweight_op2.add_output(weight_f32_dq_2);
        matmul_op2.add_input(src_f32_dq_2);
        matmul_op2.add_input(weight_f32_dq_2);
        matmul_op2.add_input(bias_f32_2);
        matmul_op2.add_output(dst_f32_2);
        qout_op2.add_input(dst_f32_2);
        qout_op2.add_output(dst_s8_2);
        dqout_o2p.add_input(dst_s8_2);
        dqout_o2p.add_output(dst_f32_dq_2);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);
        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);
        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);
        add_op.add_input(dst_f32);
        add_op.add_input(dst_f32_dq_2);
        add_op.add_output(dst_add_f32);

        qout_op.add_input(dst_add_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op2);
        g.add_op(&dqweight_op2);
        g.add_op(&matmul_op2);
        g.add_op(&qout_op2);
        g.add_op(&dqout_o2p);

        g.add_op(&dqdata_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&add_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::pass::pass_base_ptr apass
                = get_pass("int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 2U);
        auto part2 = g.get_partitions()[0]; // int8_mamtul_bias_sum
        auto part1 = g.get_partitions()[1]; // int8_matmul_bias

        // compile
        impl::partition_t p1, p2;
        p1.init(part1);
        p2.init(part2);

        impl::compiled_partition_t cp1(p1);
        impl::compiled_partition_t cp2(p2);

        dst_s8_2.layout_type = impl::layout_type::any;
        std::vector<const impl::logical_tensor_t *> lt_ins1 {
                &src_u8_2, &weight_s8_2, &bias_f32_2};
        std::vector<const impl::logical_tensor_t *> lt_outs1 {&dst_s8_2};
        p1.compile(&cp1, lt_ins1, lt_outs1, &engine);

        cp1.query_logical_tensor(dst_s8_2.id, &dst_s8_2);

        dst_s8.layout_type = impl::layout_type::any;
        std::vector<const impl::logical_tensor_t *> lt_ins2 {
                &src_u8, &weight_s8, &bias_f32, &dst_s8_2};
        std::vector<const impl::logical_tensor_t *> lt_outs2 {&dst_s8};
        p2.compile(&cp2, lt_ins2, lt_outs2, &engine);

        std::vector<impl::inplace_pair_t> inplace_pairs
                = cp2.get_inplace_pairs();

        ASSERT_EQ(inplace_pairs.size(), 1U);
        ASSERT_EQ(inplace_pairs[0].input_id, dst_s8_2.id);
        ASSERT_EQ(inplace_pairs[0].output_id, dst_s8.id);
    }
}

TEST(ExecuteSubgraphInt8, MatmulBiasU8s8bf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    // gpu doesn't support mixed int8-bf16 matmul with runtime zero points
    SKIP_IF(engine.kind() == impl::engine_kind::gpu, "skip on gpu");

    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-127.0f, 127.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::bf16);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_input(bias_bf16);
    matmul_op.add_output(dst_bf16);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_bf16_matmul_post_ops_fusion_gpu"
                            : "int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_s8, &bias_bf16};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_bf16};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<float> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    impl::tensor_t bias_bf16_ts(bias_bf16, &engine, bias_data.data());
    impl::tensor_t dst_ts(dst_bf16, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_bf16_ts}, {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasAddU8s8bf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    // gpu doesn't support mixed int8-bf16 matmul with runtime zero points
    SKIP_IF(engine.kind() == impl::engine_kind::gpu, "skip on gpu");

    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));
    test::vector<uint8_t> other_data(product(dst_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::generate(other_data.begin(), other_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-127.0f, 127.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqother_op(2, impl::op_kind::Dequantize, "dqother_op");
    dqother_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqother_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqother_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqother_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t tcdata_op {3, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {4, impl::op_kind::TypeCast, "typecast_weight"};
    impl::op_t tcother_op {5, impl::op_kind::TypeCast, "typecast_other"};

    impl::op_t matmul_op(6, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t add_op(7, impl::op_kind::Add, "add_op");

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t other_u8
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::u8);
    impl::logical_tensor_t other_f32_dq
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t other_bf16
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(11, dst_shape, impl::data_type::bf16);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    dqother_op.add_input(other_u8);
    dqother_op.add_output(other_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    tcother_op.add_input(other_f32_dq);
    tcother_op.add_output(other_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_input(bias_bf16);
    matmul_op.add_output(matmul_bf16);

    add_op.add_input(other_bf16);
    add_op.add_input(matmul_bf16);
    add_op.add_output(dst_bf16);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&dqother_op);
    g.add_op(&matmul_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&tcother_op);
    g.add_op(&add_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_bf16_matmul_post_ops_fusion_gpu"
                            : "int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_s8, &bias_bf16, &other_u8};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_bf16};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<float> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    impl::tensor_t bias_bf16_ts(bias_bf16, &engine, bias_data.data());
    impl::tensor_t other_u8_ts(other_u8, &engine, other_data.data());
    impl::tensor_t dst_ts(dst_bf16, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_bf16_ts, other_u8_ts},
            {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasAddBF16U8s8bf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    // gpu doesn't support mixed int8-bf16 matmul with runtime zero points
    SKIP_IF(engine.kind() == impl::engine_kind::gpu, "skip on gpu");

    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));
    test::vector<float> other_data(product(dst_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::generate(other_data.begin(), other_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-127.0f, 127.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t add_op(5, impl::op_kind::Add, "add_op");

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t other_bf16
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::bf16);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_input(bias_bf16);
    matmul_op.add_output(matmul_bf16);

    add_op.add_input(other_bf16);
    add_op.add_input(matmul_bf16);
    add_op.add_output(dst_bf16);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&add_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_bf16_matmul_post_ops_fusion_gpu"
                            : "int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_s8, &bias_bf16, &other_bf16};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_bf16};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<float> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    impl::tensor_t bias_bf16_ts(bias_bf16, &engine, bias_data.data());
    impl::tensor_t other_bf16_ts(other_bf16, &engine, other_data.data());
    impl::tensor_t dst_ts(dst_bf16, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_bf16_ts, other_bf16_ts},
            {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasaddAddBF16U8s8bf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    if (engine.kind() == impl::engine_kind::gpu) return;
    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> other_data(product(dst_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::generate(other_data.begin(), other_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-1.0f, 1.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(10, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t tc_bias_op(5, impl::op_kind::TypeCast, "typecast_bias");

    impl::op_t biasadd_op(6, impl::op_kind::BiasAdd, "biasadd_op");
    // matmul bias shoule add to the last dim
    biasadd_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");

    impl::op_t add_op(7, impl::op_kind::Add, "add_op");

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32 = utils::logical_tensor_init(
            30, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(7, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_out_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t other_bf16
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(11, dst_shape, impl::data_type::bf16);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_output(matmul_bf16);

    tc_bias_op.add_input(bias_f32);
    tc_bias_op.add_output(bias_bf16);

    biasadd_op.add_input(matmul_bf16);
    biasadd_op.add_input(bias_bf16);
    biasadd_op.add_output(bias_out_bf16);

    add_op.add_input(other_bf16);
    add_op.add_input(bias_out_bf16);
    add_op.add_output(dst_bf16);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&qweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tc_bias_op);
    g.add_op(&biasadd_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&add_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_bf16_matmul_post_ops_fusion_gpu"
                            : "int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32, &other_bf16};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_bf16};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<float> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    impl::tensor_t other_bf16_ts(other_bf16, &engine, other_data.data());
    impl::tensor_t dst_ts(dst_bf16, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts, other_bf16_ts},
            {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasU8s8u8MixBf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-127.0f, 127.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t tcdst_op {5, impl::op_kind::TypeCast, "typecast_dst"};

    impl::op_t qout_op(6, impl::op_kind::Quantize, "qdout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_f32
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_u8
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::u8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_input(bias_bf16);
    matmul_op.add_output(matmul_bf16);

    tcdst_op.add_input(matmul_bf16);
    tcdst_op.add_output(matmul_f32);

    qout_op.add_input(matmul_f32);
    qout_op.add_output(dst_u8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&tcdst_op);
    g.add_op(&qout_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_bf16_matmul_post_ops_fusion_gpu"
                            : "int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_s8, &bias_bf16};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_u8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<uint8_t> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    impl::tensor_t bias_bf16_ts(bias_bf16, &engine, bias_data.data());
    impl::tensor_t dst_ts(dst_u8, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_bf16_ts}, {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasaddU8s8u8MixBf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    if (engine.kind() == impl::engine_kind::gpu) return;
    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-1.f, 1.f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(10, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t tc_bias_op(5, impl::op_kind::TypeCast, "typecast_bias");

    impl::op_t biasadd_op(6, impl::op_kind::BiasAdd, "biasadd_op");
    // matmul bias shoule add to the last dim
    biasadd_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");

    impl::op_t tcdst_op {7, impl::op_kind::TypeCast, "typecast_dst"};

    impl::op_t qout_op(8, impl::op_kind::Quantize, "qdout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32 = utils::logical_tensor_init(
            30, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(7, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_out_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_f32
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_u8
            = utils::logical_tensor_init(111, dst_shape, impl::data_type::u8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_output(matmul_bf16);

    tc_bias_op.add_input(bias_f32);
    tc_bias_op.add_output(bias_bf16);

    biasadd_op.add_input(matmul_bf16);
    biasadd_op.add_input(bias_bf16);
    biasadd_op.add_output(bias_out_bf16);

    tcdst_op.add_input(bias_out_bf16);
    tcdst_op.add_output(matmul_f32);

    qout_op.add_input(matmul_f32);
    qout_op.add_output(dst_u8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tc_bias_op);
    g.add_op(&biasadd_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&tcdst_op);
    g.add_op(&qout_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_u8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<uint8_t> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    impl::tensor_t dst_ts(dst_u8, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasGeluU8s8u8MixBf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-127.0f, 127.0f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t gelu_op {5, impl::op_kind::GELU, "gelu_op"};
    impl::op_t tcgelu_op {6, impl::op_kind::TypeCast, "typecast_gelu"};

    impl::op_t qout_op(7, impl::op_kind::Quantize, "qdout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t gelu_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t gelu_f32
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_u8
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::u8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_input(bias_bf16);
    matmul_op.add_output(matmul_bf16);

    gelu_op.add_input(matmul_bf16);
    gelu_op.add_output(gelu_bf16);

    tcgelu_op.add_input(gelu_bf16);
    tcgelu_op.add_output(gelu_f32);

    qout_op.add_input(gelu_f32);
    qout_op.add_output(dst_u8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&gelu_op);
    g.add_op(&tcgelu_op);
    g.add_op(&qout_op);
    g.build_graph();

    auto &backend_ptr
            = dnnl::graph::impl::dnnl_impl::dnnl_backend::get_singleton();
    auto pm = dnnl::graph::impl::pass::pass_manager_t(
            backend_ptr.get_pass_registry());
    pm.run_passes(g, "", impl::partition_policy::fusion);

    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_s8, &bias_bf16};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_u8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<uint8_t> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    impl::tensor_t bias_bf16_ts(bias_bf16, &engine, bias_data.data());
    impl::tensor_t dst_ts(dst_u8, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts, bias_bf16_ts}, {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulBiasaddGeluU8s8u8MixBf16) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    if (engine.kind() == impl::engine_kind::gpu) return;
    std::string qtype = "per_channel";
    std::vector<int64_t> src_shape = {1, 8, 16};
    std::vector<int64_t> weight_shape = {8, 16};
    std::vector<int64_t> bias_shape = {8};
    std::vector<int64_t> dst_shape = {1, 8, 8};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> distribution(0.0f, 255.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return distribution(generator); });
    std::uniform_real_distribution<float> distribution2(-1.f, 1.f);
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return distribution2(generator); });
    std::uniform_real_distribution<float> distribution3(0.0f, 20.0f);
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return distribution3(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    int64_t zp_src = 110;

    size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
    std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
    std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

    impl::op_t dqdata_op(0, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(10, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(1, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op {2, impl::op_kind::TypeCast, "typecast_data"};
    impl::op_t tcweight_op {3, impl::op_kind::TypeCast, "typecast_weight"};

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

    impl::op_t tc_bias_op(5, impl::op_kind::TypeCast, "typecast_bias");

    impl::op_t biasadd_op(6, impl::op_kind::BiasAdd, "biasadd_op");
    // matmul bias shoule add to the last dim
    biasadd_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");

    impl::op_t gelu_op {7, impl::op_kind::GELU, "gelu_op"};
    impl::op_t tcgelu_op {8, impl::op_kind::TypeCast, "typecast_gelu"};

    impl::op_t qout_op(9, impl::op_kind::Quantize, "qdout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(0, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16
            = utils::logical_tensor_init(2, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32 = utils::logical_tensor_init(
            30, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16 = utils::logical_tensor_init(
            5, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(7, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_out_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t matmul_bf16
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t gelu_bf16
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t gelu_f32
            = utils::logical_tensor_init(11, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_u8
            = utils::logical_tensor_init(12, dst_shape, impl::data_type::u8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16);

    matmul_op.add_input(src_bf16);
    matmul_op.add_input(weight_bf16);
    matmul_op.add_output(matmul_bf16);

    tc_bias_op.add_input(bias_f32);
    tc_bias_op.add_output(bias_bf16);

    biasadd_op.add_input(matmul_bf16);
    biasadd_op.add_input(bias_bf16);
    biasadd_op.add_output(bias_out_bf16);

    gelu_op.add_input(bias_out_bf16);
    gelu_op.add_output(gelu_bf16);

    tcgelu_op.add_input(gelu_bf16);
    tcgelu_op.add_output(gelu_f32);

    qout_op.add_input(gelu_f32);
    qout_op.add_output(dst_u8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tc_bias_op);
    g.add_op(&biasadd_op);
    g.add_op(&tcdata_op);
    g.add_op(&tcweight_op);
    g.add_op(&gelu_op);
    g.add_op(&tcgelu_op);
    g.add_op(&qout_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("int8_bf16_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_u8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<uint8_t> dst_data(product(dst_shape));
    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_bf32_ts(bias_f32, &engine, bias_data.data());
    impl::tensor_t dst_ts(dst_u8, &engine, dst_data.data());
    cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_bf32_ts}, {dst_ts});
    strm.wait();
}

TEST(Execute, MatmulScalarOutput) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);
    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {-2.0, -1.5, 1.0};
    test::vector<float> weight_data {-2.0, -1.5, 1.0};
    test::vector<float> ref_dst_data {7.25};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {3}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {3}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            2, impl::data_type::f32, impl::layout_type::any);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    ASSERT_EQ(p.compile(&cp, inputs, outputs, &eng), impl::status::success);

    // output should be a scalar (ndims=0, layout_type=strided)
    impl::logical_tensor_t scalar_lt;
    cp.query_logical_tensor(dst.id, &scalar_lt);
    ASSERT_EQ(scalar_lt.layout_type, impl::layout_type::strided);
    ASSERT_EQ(scalar_lt.ndims, 0);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(scalar_lt, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    ASSERT_EQ(cp.execute(&strm, {src_ts, weight_ts}, {dst_ts}),
            impl::status::success);
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasSumNdx2d) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::string> weight_qtypes = {"symmetric", "asymmetric"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto &qtype : qtypes)
    for_(const auto &wei_qtype : weight_qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<float> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<int8_t> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_other = 1 / 127.f;
        float scale_out = 1;
        // reorder with zps is not supported on GPU
        int64_t zp_src = engine.kind() == impl::engine_kind::gpu ? 0 : 90;
        int64_t zp_other = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        auto generate_zps = [&]() {
            // backend integration doesn't support per_channel asym quant now.
            if (qtype == "per_channel" || wei_qtype == "symmetric"
                    || engine.kind() == impl::engine_kind::gpu)
                return 0;
            else
                return 78;
        };

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, generate_zps());

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
        qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        qweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqother_op(6, impl::op_kind::Dequantize, "dqother_op");
        dqother_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqother_op.set_attr<std::vector<int64_t>>(
                impl::op_attr::zps, {zp_other});
        dqother_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_other});
        dqother_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t add_op(7, impl::op_kind::Add, "add_op");

        // The logical tensor in graph stage should only have valid id and dtype
        auto src_u8 = utils::logical_tensor_init(1, impl::data_type::u8);
        auto src_f32_dq = utils::logical_tensor_init(2, impl::data_type::f32);
        auto weight_f32 = utils::logical_tensor_init(3, impl::data_type::f32);
        auto weight_s8 = utils::logical_tensor_init(4, impl::data_type::s8);
        auto weight_f32_dq
                = utils::logical_tensor_init(5, impl::data_type::f32);
        auto bias_f32 = utils::logical_tensor_init(6, impl::data_type::f32);
        auto dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        auto dst_s8
                = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);
        auto other_s8 = utils::logical_tensor_init(
                11, dst_shape, impl::data_type::s8);
        auto other_f32_dq = utils::logical_tensor_init(
                12, dst_shape, impl::data_type::f32);
        auto dst_add_f32 = utils::logical_tensor_init(
                13, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        qweight_op.add_input(weight_f32);
        qweight_op.add_output(weight_s8);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        dqother_op.add_input(other_s8);
        dqother_op.add_output(other_f32_dq);

        add_op.add_input(dst_f32);
        add_op.add_input(other_f32_dq);
        add_op.add_output(dst_add_f32);

        qout_op.add_input(dst_add_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&qweight_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&dqother_op);
        g.add_op(&add_op);
        g.add_op(&qout_op);
        g.build_graph();

        // prepare in/out full shape
        src_u8 = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        weight_f32 = utils::logical_tensor_init(
                3, weight_shape, impl::data_type::f32);
        // set weight to be constant
        weight_f32.property = impl::property_type::constant;
        dst_s8 = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);
        other_s8 = utils::logical_tensor_init(
                11, dst_shape, impl::data_type::s8);
        bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        // set bias to be constant
        bias_f32.property = impl::property_type::constant;

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
        impl::tensor_t other_s8_ts(other_s8, &engine, other_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g,
                          {src_u8_ts, weight_f32_ts, bias_f32_ts, other_s8_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);

        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_f32, &bias_f32, &other_s8};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        for (size_t iter = 0; iter < 5; iter++) {
            cp.execute(&strm,
                    {src_u8_ts, weight_f32_ts, bias_f32_ts, other_s8_ts},
                    {dst_s8_case2_ts});
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(ExecuteSubgraphInt8, U8S8U8MatmulAddF32) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {{8, 4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {{8, 2}};
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<float> weight_data(product(weight_shape));
        test::vector<float> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 127.0f);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_out = 1 / 255.f;
        int64_t zp_src = 90;
        int64_t zp_out = 78;

        std::vector<float> scale_wei(dst_shape.back(), 1 / 127.f);
        std::vector<int64_t> zp_wei(dst_shape.back(), 0);

        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
        qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        qweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t add_op(6, impl::op_kind::Add, "add_op");

        // The logical tensor in graph stage should only have valid id and dtype
        auto src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        auto src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        auto weight_f32 = utils::logical_tensor_init(
                3, weight_shape, impl::data_type::f32);
        weight_f32.property = impl::property_type::constant;
        auto weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        auto weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        auto dst_f32 = utils::logical_tensor_init(
                6, dst_shape, impl::data_type::f32);
        auto other_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        auto dst_add_f32 = utils::logical_tensor_init(
                8, dst_shape, impl::data_type::f32);
        auto dst_u8
                = utils::logical_tensor_init(9, dst_shape, impl::data_type::u8);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        qweight_op.add_input(weight_f32);
        qweight_op.add_output(weight_s8);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_output(dst_f32);

        add_op.add_input(dst_f32);
        add_op.add_input(other_f32);
        add_op.add_output(dst_add_f32);

        qout_op.add_input(dst_add_f32);
        qout_op.add_output(dst_u8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&qweight_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&add_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
        impl::tensor_t other_f32_ts(other_f32, &engine, other_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_u8_ts(dst_u8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_f32_ts, other_f32_ts},
                          {dst_u8_ts}, engine, strm),
                impl::status::success);

        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_f32, &other_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_u8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_u8_case2_ts(dst_u8, &engine, case2_out_data.data());
        for (size_t iter = 0; iter < 5; iter++) {
            cp.execute(&strm, {src_u8_ts, weight_f32_ts, other_f32_ts},
                    {dst_u8_case2_ts});
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasNdx2dWithTranspose) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{2, 4}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};

    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<float> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));
        test::vector<int8_t> other_data(product(dst_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(other_data.begin(), other_data.end(), [&]() {
            return static_cast<int8_t>(s8_distribution(generator));
        });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_out = 1;
        int64_t zp_src = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        // -------------------------case 1----------------------------------
        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
        qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        qweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        qweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);

        impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        // prepare logical tensor
        // The logical tensor in graph stage should only have valid id and dtype
        auto src_u8 = utils::logical_tensor_init(1, impl::data_type::u8);
        auto src_f32_dq = utils::logical_tensor_init(2, impl::data_type::f32);
        auto weight_f32 = utils::logical_tensor_init(3, impl::data_type::f32);
        auto weight_s8 = utils::logical_tensor_init(4, impl::data_type::s8);
        auto weight_f32_dq
                = utils::logical_tensor_init(5, impl::data_type::f32);
        auto dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        auto dst_s8
                = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);
        auto bias_f32 = utils::logical_tensor_init(6, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        qweight_op.add_input(weight_f32);
        qweight_op.add_output(weight_s8);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        qout_op.add_input(dst_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&qweight_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&qout_op);
        g.build_graph();

        src_u8 = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        weight_f32 = utils::logical_tensor_init(
                3, weight_shape, impl::data_type::f32);
        // set weight to be constant
        weight_f32.property = impl::property_type::constant;
        dst_s8 = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);
        bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        // set bias to be constant
        bias_f32.property = impl::property_type::constant;

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_f32_ts, bias_f32_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);
        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_f32, &bias_f32};
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        for (size_t iter = 0; iter < 1; iter++) {
            cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts},
                    {dst_s8_case2_ts});
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasReluNdx2d) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<bool> with_bias_types {true, false};
    std::vector<std::string> qtypes {"per_tensor", "per_channel"};
    std::vector<std::vector<int64_t>> src_shapes {
            {3, 3, 3, 8, 4}, {3, 3, 8, 4}, {3, 8, 4}, {8, 4}, {4}};
    std::vector<std::vector<int64_t>> weight_shapes {{4, 2}};
    std::vector<std::vector<int64_t>> dst_shapes {
            {3, 3, 3, 8, 2}, {3, 3, 8, 2}, {3, 8, 2}, {8, 2}, {2}};
    for_(const auto with_bias : with_bias_types)
    for_(const auto &qtype : qtypes)
    for_(size_t i = 0; i < src_shapes.size(); ++i)
    for (size_t j = 0; j < weight_shapes.size(); ++j) {
        // prepare fp32 data
        std::vector<int64_t> src_shape = src_shapes[i];
        std::vector<int64_t> weight_shape = weight_shapes[j];
        std::vector<int64_t> bias_shape {2};
        std::vector<int64_t> dst_shape = dst_shapes[i];

        test::vector<uint8_t> src_data(product(src_shape));
        test::vector<float> weight_data(product(weight_shape));
        test::vector<float> bias_data(product(bias_shape));

        // random generate src, weight and bias data
        // random seed = 7
        std::default_random_engine generator(7);
        std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
        std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
        std::generate(src_data.begin(), src_data.end(), [&]() {
            return static_cast<uint8_t>(u8_distribution(generator));
        });
        std::generate(weight_data.begin(), weight_data.end(),
                [&]() { return f32_distribution(generator); });
        std::generate(bias_data.begin(), bias_data.end(),
                [&]() { return f32_distribution(generator); });
        float scale_src = 1 / 255.f; // map to 0~255
        float scale_out = 1;
        int64_t zp_src = 0;
        // The following cmd will be skiped by benchdnn, since oneDNN didn't
        // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
        // --engine=gpu --mode=C --sdt=f32 --ddt=s8
        // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
        int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

        size_t scales_wei_sizes = qtype == "per_tensor" ? 1 : dst_shape.back();
        std::vector<float> scale_wei(scales_wei_sizes, 1 / 127.f);
        std::vector<int64_t> zp_wei(scales_wei_sizes, 0);

        // -------------------------case 1----------------------------------
        impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
        dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
        dqdata_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_src});
        dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
        qweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        qweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
        dqweight_op.set_attr<std::string>(impl::op_attr::qtype, qtype);
        dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
        dqweight_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, scale_wei);
        dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

        impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
        matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
        matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

        impl::op_t relu_op(5, impl::op_kind::ReLU, "relu_op");

        impl::op_t qout_op(6, impl::op_kind::Quantize, "qout_op");
        qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
        qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
        qout_op.set_attr<std::vector<float>>(
                impl::op_attr::scales, {scale_out});
        qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

        // prepare logical tensor
        impl::logical_tensor_t src_u8
                = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
        impl::logical_tensor_t src_f32_dq = utils::logical_tensor_init(
                2, src_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_f32 = utils::logical_tensor_init(
                3, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t weight_s8 = utils::logical_tensor_init(
                4, weight_shape, impl::data_type::s8);
        impl::logical_tensor_t weight_f32_dq = utils::logical_tensor_init(
                5, weight_shape, impl::data_type::f32);
        impl::logical_tensor_t bias_f32 = utils::logical_tensor_init(
                6, bias_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_f32 = utils::logical_tensor_init(
                7, dst_shape, impl::data_type::f32);
        impl::logical_tensor_t dst_s8
                = utils::logical_tensor_init(8, dst_shape, impl::data_type::s8);
        impl::logical_tensor_t dst_relu_f32 = utils::logical_tensor_init(
                9, dst_shape, impl::data_type::f32);

        dqdata_op.add_input(src_u8);
        dqdata_op.add_output(src_f32_dq);

        qweight_op.add_input(weight_f32);
        qweight_op.add_output(weight_s8);

        dqweight_op.add_input(weight_s8);
        dqweight_op.add_output(weight_f32_dq);

        matmul_op.add_input(src_f32_dq);
        matmul_op.add_input(weight_f32_dq);
        if (with_bias) matmul_op.add_input(bias_f32);
        matmul_op.add_output(dst_f32);

        relu_op.add_input(dst_f32);
        relu_op.add_output(dst_relu_f32);

        qout_op.add_input(dst_relu_f32);
        qout_op.add_output(dst_s8);

        impl::graph_t g(engine.kind());
        g.add_op(&dqdata_op);
        g.add_op(&qweight_op);
        g.add_op(&dqweight_op);
        g.add_op(&matmul_op);
        g.add_op(&relu_op);
        g.add_op(&qout_op);
        g.build_graph();

        impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
        impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
        impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
        // -------------------------case 1----------------------------------
        test::vector<int8_t> case1_out_data(product(dst_shape));
        impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
        ASSERT_EQ(run_graph(g, {src_u8_ts, weight_f32_ts, bias_f32_ts},
                          {dst_s8_ts}, engine, strm),
                impl::status::success);

        // -------------------------case 2----------------------------------
        impl::pass::pass_base_ptr apass
                = get_pass(engine.kind() == impl::engine_kind::gpu
                                ? "int8_matmul_post_ops_fusion_gpu"
                                : "int8_matmul_post_ops_fusion_cpu");
        apass->run(g);
        ASSERT_EQ(g.get_num_partitions(), 1U);
        auto part = g.get_partitions()[0];

        // compile
        impl::partition_t p;
        p.init(part);

        impl::compiled_partition_t cp(p);

        std::vector<const impl::logical_tensor_t *> lt_ins {
                &src_u8, &weight_f32, &bias_f32};
        if (!with_bias) lt_ins.pop_back();
        std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

        p.compile(&cp, lt_ins, lt_outs, &engine);

        test::vector<int8_t> case2_out_data(product(dst_shape));
        impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
        for (size_t iter = 0; iter < 5; iter++) {
            if (with_bias) {
                cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts},
                        {dst_s8_case2_ts});
            } else {
                cp.execute(
                        &strm, {src_u8_ts, weight_f32_ts}, {dst_s8_case2_ts});
            }
            strm.wait();

            static auto isa = dnnl_get_effective_cpu_isa();
            if (engine.kind() == impl::engine_kind::cpu
                    && isa < dnnl_cpu_isa_avx512_core_vnni)
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                                /*atol*/ 1.f));
            else
                ASSERT_TRUE(
                        allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                                /*atol*/ 1.f));
        }
    }
}

TEST(Execute, MatmulReluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t relu_op(1, impl::op_kind::ReLU, "relu_op");

    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, 1.5};
    test::vector<float> ref_dst_data {0.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t relu_dst
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    relu_op.add_input(dst);
    relu_op.add_output(relu_dst);

    impl::graph_t g(eng.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&relu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&relu_dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(relu_dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasFusion) {
    impl::op_t matmul_op(impl::op_kind::MatMul);

    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {-2.0, -1.5, 3.0, 0.5};
    test::vector<float> weight_data {-2.0, -1.5, 1.0, 1.0};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {7.25, 4.5};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t bias_ts(bias, &eng, bias_data.data());
    impl::tensor_t dst_ts(dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulSumBroadcast1d) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");

    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5, 3.0, 0.5};
    test::vector<float> weight_data {-2.0, -1.5, 1.0, 1.0};
    test::vector<float> add_src1_data {1.0, 1.0};
    test::vector<float> ref_dst_data {5.0, 4.0, 4.0, 3.25, 4.0, 4.0, 1.5, 1.5};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_src1
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(4, {2, 2, 2}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(add_src1);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &add_src1};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t add_src1_ts(add_src1, &engine, add_src1_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, add_src1_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulSumFusion) {
    impl::engine_t &engine = get_engine();

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");

    test::vector<float> src_data {-2.0, -1.5, 3.0, 0.5};
    test::vector<float> weight_data {-2.0, -1.5, 1.0, 1.0};
    test::vector<float> add_src1_data {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    test::vector<float> ref_dst_data {5.0, 4.0, 4.0, 3.25, 4.0, 4.0, 1.5, 1.5};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_src1
            = utils::logical_tensor_init(2, {2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(4, {2, 2, 2}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(add_src1);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &add_src1};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t add_src1_ts(add_src1, &engine, add_src1_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, add_src1_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulSumGeluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");
    impl::op_t gelu_op(2, impl::op_kind::GELU, "gelu_op");

    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {1.0, 1.0, 1.0, 1.0};
    test::vector<float> weight_data {0.5, 0.5, 0.5, 0.5};
    test::vector<float> other_data {1.0, 1.0};
    test::vector<float> ref_dst_data {1.9544998f, 1.9544998f};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t other
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 1, 1}, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(4, {2, 1, 1}, impl::data_type::f32);
    impl::logical_tensor_t gelu_dst
            = utils::logical_tensor_init(5, {2, 1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(other);
    add_op.add_output(add_dst);
    gelu_op.add_input(add_dst);
    gelu_op.add_output(gelu_dst);

    impl::graph_t g(eng.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    ASSERT_EQ(g.add_op(&gelu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &other};
    std::vector<const impl::logical_tensor_t *> outputs {&gelu_dst};

    p.compile(&cp, inputs, outputs, &eng);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t other_ts(other, &eng, other_data.data());
    impl::tensor_t dst_ts(gelu_dst, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, other_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulSumReluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");
    impl::op_t relu_op(2, impl::op_kind::ReLU, "relu_op");

    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {1.0, 2.0, 3.0, 4.0};
    test::vector<float> weight_data {-1.0, -2.0, 3.0, 4.0};
    test::vector<float> other_data {2.5};
    test::vector<float> ref_dst_data {0, 27.5f};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t other
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 1, 1}, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(4, {2, 1, 1}, impl::data_type::f32);
    impl::logical_tensor_t relu_dst
            = utils::logical_tensor_init(5, {2, 1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(other);
    add_op.add_output(add_dst);
    relu_op.add_input(add_dst);
    relu_op.add_output(relu_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    ASSERT_EQ(g.add_op(&relu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &other};
    std::vector<const impl::logical_tensor_t *> outputs {&relu_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t other_ts(other, &engine, other_data.data());
    impl::tensor_t dst_ts(relu_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, other_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasReluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t relu_op(1, impl::op_kind::ReLU, "relu_op");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {0.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t relu_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    relu_op.add_input(dst);
    relu_op.add_output(relu_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&relu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&relu_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(relu_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasGeluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t gelu_op(1, impl::op_kind::GELU, "gelu_op");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {1.0, 1.0, 1.0, 1.0};
    test::vector<float> weight_data {0.5, 0.5, 0.5, 0.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {1.9544998f, 1.9544998f};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 1, 1}, impl::data_type::f32);
    impl::logical_tensor_t gelu_dst
            = utils::logical_tensor_init(5, {2, 1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    gelu_op.add_input(dst);
    gelu_op.add_output(gelu_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&gelu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&gelu_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(gelu_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasRelu6Fusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t clamp_op(1, impl::op_kind::Clamp, "clamp_op");
    clamp_op.set_attr<float>(impl::op_attr::min, 0.0);
    clamp_op.set_attr<float>(impl::op_attr::max, 6.0);

    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {-2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {6.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t clamp_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    clamp_op.add_input(dst);
    clamp_op.add_output(clamp_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&clamp_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&clamp_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(clamp_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasClampFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t clamp_op(1, impl::op_kind::Clamp, "clamp_op");
    clamp_op.set_attr<float>(impl::op_attr::min, -3.0);
    clamp_op.set_attr<float>(impl::op_attr::max, 3.0);

    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {-2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {3.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t clamp_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    clamp_op.add_input(dst);
    clamp_op.add_output(clamp_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&clamp_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&clamp_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(clamp_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasEluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t elu_op(1, impl::op_kind::Elu, "elu_op");
    elu_op.set_attr<float>(impl::op_attr::alpha, 1.f);

    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {-0.75};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);
    ref_dst_data[0] = static_cast<float>(exp(-0.75) - 1);
    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t elu_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    elu_op.add_input(dst);
    elu_op.add_output(elu_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&elu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&elu_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(elu_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasSigmoidFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t sigmoid_op(1, impl::op_kind::Sigmoid, "sigmoid_op");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> ref_dst_data {-0.75};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);
    ref_dst_data[0] = static_cast<float>(1 / (exp(-ref_dst_data[0]) + 1));
    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            3, {1, 1}, impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t sigmoid_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    sigmoid_op.add_input(dst);
    sigmoid_op.add_output(sigmoid_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&sigmoid_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&sigmoid_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t dst_ts(sigmoid_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasAddFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> post_src_data {-2.0};
    test::vector<float> ref_dst_data {-2.75};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t post_src
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            4, {1, 1}, impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(post_src);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &bias, &post_src};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t post_src_ts(post_src, &engine, post_src_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts, post_src_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulDivFusion) {
    impl::engine_t &engine = get_engine();

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t div_op(1, impl::op_kind::Divide, "div_op");

    test::vector<float> src_data {-2.0, -1.5, 3.0, 0.5};
    test::vector<float> weight_data {-2.0, -1.5, 1.0, 1.0};
    test::vector<float> div_src1_data {-1.0};
    test::vector<float> ref_dst_data {
            -4.0, -3.0, -3.0, -2.25, -3.0, -3.0, -0.5, -0.5};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t div_src1
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t div_dst
            = utils::logical_tensor_init(4, {2, 2, 2}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    div_op.add_input(dst);
    div_op.add_input(div_src1);
    div_op.add_output(div_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&div_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &div_src1};
    std::vector<const impl::logical_tensor_t *> outputs {&div_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t div_src1_ts(div_src1, &engine, div_src1_data.data());
    impl::tensor_t dst_ts(div_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, div_src1_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulDivAddFusion) {
    impl::engine_t &engine = get_engine();

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t div_op(1, impl::op_kind::Divide, "div_op");
    impl::op_t add_op(2, impl::op_kind::Add, "add_op");

    test::vector<float> src_data {-2.0, -1.5, 3.0, 0.5};
    test::vector<float> weight_data {-2.0, -1.5, 1.0, 1.0};
    test::vector<float> div_src1_data {-1.0};
    test::vector<float> add_src1_data {1.0, 2.0};
    test::vector<float> ref_dst_data {
            -3.0, -1.0, -2.0, -0.25, -2.0, -1.0, 0.5, 1.5};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2, 2, 1}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {1, 2, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t div_src1
            = utils::logical_tensor_init(2, {1}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t div_dst
            = utils::logical_tensor_init(4, {1, 2, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_src1
            = utils::logical_tensor_init(5, {1, 1, 1, 2}, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(6, {1, 2, 2, 2}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    div_op.add_input(dst);
    div_op.add_input(div_src1);
    div_op.add_output(div_dst);
    add_op.add_input(div_dst);
    add_op.add_input(add_src1);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&div_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &div_src1, &add_src1};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t div_src1_ts(div_src1, &engine, div_src1_data.data());
    impl::tensor_t add_src1_ts(add_src1, &engine, add_src1_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, div_src1_ts, add_src1_ts}, {dst_ts});
    strm.wait();

    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulSwapBinaryMulAddFusion) {
    impl::engine_t &engine = get_engine();

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t div_op(1, impl::op_kind::Multiply, "div_op");
    impl::op_t add_op(2, impl::op_kind::Add, "add_op");

    std::vector<int64_t> src_shape {2, 2, 2, 2};
    std::vector<int64_t> div_shape {1};
    std::vector<int64_t> add_shape {2, 1, 2, 2};

    test::vector<float> src_data(product(src_shape));
    test::vector<float> weight_data(product(src_shape));
    test::vector<float> div_src1_data(product(div_shape));
    test::vector<float> add_src1_data(product(add_shape));
    test::vector<float> dst_data(product(src_shape), 0.0);

    // random generate src, weight and bias data random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(), [&]() {
        return static_cast<uint8_t>(f32_distribution(generator));
    });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return static_cast<int8_t>(f32_distribution(generator)); });
    std::generate(div_src1_data.begin(), div_src1_data.end(),
            [&]() { return static_cast<int8_t>(f32_distribution(generator)); });
    std::generate(add_src1_data.begin(), add_src1_data.end(),
            [&]() { return static_cast<int8_t>(f32_distribution(generator)); });

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, src_shape, impl::data_type::f32);
    impl::logical_tensor_t div_src1
            = utils::logical_tensor_init(2, div_shape, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, src_shape, impl::data_type::f32);
    impl::logical_tensor_t div_dst
            = utils::logical_tensor_init(4, src_shape, impl::data_type::f32);
    impl::logical_tensor_t add_src1
            = utils::logical_tensor_init(5, add_shape, impl::data_type::f32);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(6, src_shape, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);
    div_op.add_input(dst);
    div_op.add_input(div_src1);
    div_op.add_output(div_dst);
    add_op.add_input(add_src1);
    add_op.add_input(div_dst);
    add_op.add_output(add_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&div_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &div_src1, &add_src1};
    std::vector<const impl::logical_tensor_t *> outputs {&add_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t div_src1_ts(div_src1, &engine, div_src1_data.data());
    impl::tensor_t add_src1_ts(add_src1, &engine, add_src1_data.data());
    impl::tensor_t dst_ts(add_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, div_src1_ts, add_src1_ts}, {dst_ts});
    strm.wait();
}

TEST(ExecuteSubgraphInt8, MatmulReluFusion) {
    // compare results between:
    // case 1: [quantize] - [dequantize] - [fp32_matmul] - [relu] - [quantize]
    // case 2: [quantize] - [int8_matmul]
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    // prepare fp32 data
    std::vector<int64_t> src_shape {8, 6};
    std::vector<int64_t> weight_shape {6, 4};
    std::vector<int64_t> dst_shape {8, 4};

    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<int8_t> weight_data(product(weight_shape));

    // random generate src, weight and bias data random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> s8_distribution(-127.0f, 128.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return static_cast<int8_t>(s8_distribution(generator)); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_wei = 1 / 127.f;
    float scale_out = 1;
    int64_t zp_src = 0;
    int64_t zp_wei = 0;
    // The following cmd will be skiped by benchdnn, since oneDNN didn't
    // support reorder with zps on GPU: "./tests/benchdnn/benchdnn --reorder
    // --engine=gpu --mode=C --sdt=f32 --ddt=s8
    // --attr-zero-points=dst:common:78 --stag=aBc8b --dtag=abc 1x8x10"
    int64_t zp_out = engine.kind() == impl::engine_kind::gpu ? 0 : 78;

    // -------------------------case 1----------------------------------
    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t dqweight_op(2, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_wei});
    dqweight_op.set_attr<std::vector<float>>(
            impl::op_attr::scales, {scale_wei});
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t matmul_op(3, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t relu_op(4, impl::op_kind::ReLU, "relu_op");

    impl::op_t qout_op(5, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_relu_f32
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_s8
            = utils::logical_tensor_init(9, dst_shape, impl::data_type::s8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    matmul_op.add_input(src_f32_dq);
    matmul_op.add_input(weight_f32_dq);
    matmul_op.add_output(dst_f32);

    relu_op.add_input(dst_f32);
    relu_op.add_output(dst_relu_f32);

    qout_op.add_input(dst_relu_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&relu_op);
    g.add_op(&qout_op);
    g.build_graph();

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_s8_ts(weight_s8, &engine, weight_data.data());
    // -------------------------case 1----------------------------------
    test::vector<int8_t> case1_out_data(product(dst_shape));
    impl::tensor_t dst_s8_ts(dst_s8, &engine, case1_out_data.data());
    ASSERT_EQ(
            run_graph(g, {src_u8_ts, weight_s8_ts}, {dst_s8_ts}, engine, strm),
            impl::status::success);
    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass(engine.kind() == impl::engine_kind::gpu
                            ? "int8_matmul_post_ops_fusion_gpu"
                            : "int8_matmul_post_ops_fusion_cpu");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];
    ASSERT_TRUE(part != nullptr);

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {&src_u8, &weight_s8};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    test::vector<int8_t> case2_out_data(product(dst_shape));
    impl::tensor_t dst_s8_case2_ts(dst_s8, &engine, case2_out_data.data());
    cp.execute(&strm, {src_u8_ts, weight_s8_ts}, {dst_s8_case2_ts});
    strm.wait();

    static auto isa = dnnl_get_effective_cpu_isa();
    if (engine.kind() == impl::engine_kind::cpu
            && isa < dnnl_cpu_isa_avx512_core_vnni)
        ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.1f,
                /*atol*/ 1.f));
    else
        ASSERT_TRUE(allclose(case1_out_data, case2_out_data, /*rtol*/ 0.01f,
                /*atol*/ 1.f));
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasReshapeTransposeQuantize) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {8, 4};
    std::vector<int64_t> weight_shape {4, 4};
    std::vector<int64_t> bias_shape {4};
    std::vector<int64_t> dst_shape {8, 4};
    std::vector<int64_t> reshape_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 127.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t reshape_op(5, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    impl::op_t transpose_op(6, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t qout_op(7, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t reshape_f32 = utils::logical_tensor_init(
            8, reshape_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            9, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            10, transpose_shape, impl::data_type::s8, impl::layout_type::any);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    matmul_op.add_input(src_f32_dq);
    matmul_op.add_input(weight_f32_dq);
    matmul_op.add_input(bias_f32);
    matmul_op.add_output(dst_f32);

    reshape_op.add_input(dst_f32);
    reshape_op.add_output(reshape_f32);

    transpose_op.add_input(reshape_f32);
    transpose_op.add_output(transpose_f32);

    qout_op.add_input(transpose_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&reshape_op);
    g.add_op(&transpose_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_s8_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_s8_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasTransposeReshapeQuantize) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> bias_shape {2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reshape_shape {4, 8};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 127.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t transpose_op(5, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reshape_op(6, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    impl::op_t qout_op(7, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            8, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t reshape_f32 = utils::logical_tensor_init(
            9, reshape_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            10, reshape_shape, impl::data_type::s8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    matmul_op.add_input(src_f32_dq);
    matmul_op.add_input(weight_f32_dq);
    matmul_op.add_input(bias_f32);
    matmul_op.add_output(dst_f32);

    transpose_op.add_input(dst_f32);
    transpose_op.add_output(transpose_f32);

    reshape_op.add_input(transpose_f32);
    reshape_op.add_output(reshape_f32);

    qout_op.add_input(reshape_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&transpose_op);
    g.add_op(&reshape_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_s8_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_s8_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMixBf16MatmulBiasReshapeTransposeQuantize) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {8, 4};
    std::vector<int64_t> weight_shape {4, 4};
    std::vector<int64_t> bias_shape {4};
    std::vector<int64_t> dst_shape {8, 4};
    std::vector<int64_t> reshape_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 127.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op(2, impl::op_kind::TypeCast, "tcdata_op");

    impl::op_t qweight_op(3, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t dqweight_op(4, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t tcweight_op(5, impl::op_kind::TypeCast, "tcweight_op");

    impl::op_t matmul_op(6, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t tc_bias_op(7, impl::op_kind::TypeCast, "tcbias_op");

    impl::op_t biasadd_op(8, impl::op_kind::BiasAdd, "biasadd_op");
    biasadd_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");

    impl::op_t reshape_op(9, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    impl::op_t transpose_op(10, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t tcout_op(11, impl::op_kind::TypeCast, "tcout_op");

    impl::op_t qout_op(12, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16_dq
            = utils::logical_tensor_init(3, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(6, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16_dq = utils::logical_tensor_init(
            7, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(8, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(9, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t biasadd_bf16
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(11, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t reshape_bf16 = utils::logical_tensor_init(
            12, reshape_shape, impl::data_type::bf16);
    impl::logical_tensor_t transpose_bf16 = utils::logical_tensor_init(
            13, transpose_shape, impl::data_type::bf16);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            14, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            15, transpose_shape, impl::data_type::s8, impl::layout_type::any);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16_dq);

    matmul_op.add_input(src_bf16_dq);
    matmul_op.add_input(weight_bf16_dq);
    matmul_op.add_output(dst_bf16);

    tc_bias_op.add_input(bias_f32);
    tc_bias_op.add_output(bias_bf16);

    biasadd_op.add_input(dst_bf16);
    biasadd_op.add_input(bias_bf16);
    biasadd_op.add_output(biasadd_bf16);

    reshape_op.add_input(biasadd_bf16);
    reshape_op.add_output(reshape_bf16);

    transpose_op.add_input(reshape_bf16);
    transpose_op.add_output(transpose_bf16);

    tcout_op.add_input(transpose_bf16);
    tcout_op.add_output(transpose_f32);

    qout_op.add_input(transpose_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&tcdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&tcweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tc_bias_op);
    g.add_op(&biasadd_op);
    g.add_op(&reshape_op);
    g.add_op(&transpose_op);
    g.add_op(&tcout_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_bf16_matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_s8_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_s8_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMixBf16MatmulBiasTransposeReshapeQuantize) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> bias_shape {2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reshape_shape {4, 8};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 127.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op(2, impl::op_kind::TypeCast, "tcdata_op");

    impl::op_t qweight_op(3, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t dqweight_op(4, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 1);

    impl::op_t tcweight_op(5, impl::op_kind::TypeCast, "tcweight_op");

    impl::op_t matmul_op(6, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t tc_bias_op(7, impl::op_kind::TypeCast, "tcbias_op");

    impl::op_t biasadd_op(8, impl::op_kind::BiasAdd, "biasadd_op");
    biasadd_op.set_attr<std::string>(impl::op_attr::data_format, "NXC");

    impl::op_t transpose_op(9, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reshape_op(10, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    impl::op_t tcout_op(11, impl::op_kind::TypeCast, "tcout_op");

    impl::op_t qout_op(12, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16_dq
            = utils::logical_tensor_init(3, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(6, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16_dq = utils::logical_tensor_init(
            7, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(8, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t bias_bf16
            = utils::logical_tensor_init(9, bias_shape, impl::data_type::bf16);
    impl::logical_tensor_t biasadd_bf16
            = utils::logical_tensor_init(10, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(11, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t transpose_bf16 = utils::logical_tensor_init(
            12, transpose_shape, impl::data_type::bf16);
    impl::logical_tensor_t reshape_bf16 = utils::logical_tensor_init(
            13, reshape_shape, impl::data_type::bf16);
    impl::logical_tensor_t reshape_f32 = utils::logical_tensor_init(
            14, reshape_shape, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            15, reshape_shape, impl::data_type::s8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16_dq);

    matmul_op.add_input(src_bf16_dq);
    matmul_op.add_input(weight_bf16_dq);
    matmul_op.add_output(dst_bf16);

    tc_bias_op.add_input(bias_f32);
    tc_bias_op.add_output(bias_bf16);

    biasadd_op.add_input(dst_bf16);
    biasadd_op.add_input(bias_bf16);
    biasadd_op.add_output(biasadd_bf16);

    transpose_op.add_input(biasadd_bf16);
    transpose_op.add_output(transpose_bf16);

    reshape_op.add_input(transpose_bf16);
    reshape_op.add_output(reshape_bf16);

    tcout_op.add_input(reshape_bf16);
    tcout_op.add_output(reshape_f32);

    qout_op.add_input(reshape_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&tcdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&tcweight_op);
    g.add_op(&matmul_op);
    g.add_op(&tc_bias_op);
    g.add_op(&biasadd_op);
    g.add_op(&transpose_op);
    g.add_op(&reshape_op);
    g.add_op(&tcout_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_bf16_matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_s8_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_s8_ts});
        strm.wait();
    }
}

TEST(Execute, MatmulBiasReshapeTranspose) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {8, 4};
    std::vector<int64_t> weight_shape {4, 4};
    std::vector<int64_t> bias_shape {4};
    std::vector<int64_t> dst_shape {8, 4};
    std::vector<int64_t> reshape_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t reshape_op(1, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    impl::op_t transpose_op(2, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    // prepare logical tensor
    impl::logical_tensor_t src_f32
            = utils::logical_tensor_init(0, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(1, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(2, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(3, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t reshape_f32 = utils::logical_tensor_init(
            4, reshape_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            5, transpose_shape, impl::data_type::f32, impl::layout_type::any);

    matmul_op.add_input(src_f32);
    matmul_op.add_input(weight_f32);
    matmul_op.add_input(bias_f32);
    matmul_op.add_output(dst_f32);

    reshape_op.add_input(dst_f32);
    reshape_op.add_output(reshape_f32);

    transpose_op.add_input(reshape_f32);
    transpose_op.add_output(transpose_f32);

    impl::graph_t g(engine.kind());
    g.add_op(&matmul_op);
    g.add_op(&reshape_op);
    g.add_op(&transpose_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_f32, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&transpose_f32};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_f32_ts(src_f32, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<float> dst_out_data(product(dst_shape));
    impl::tensor_t dst_f32_ts(transpose_f32, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(
                &strm, {src_f32_ts, weight_f32_ts, bias_f32_ts}, {dst_f32_ts});
        strm.wait();
    }
}

TEST(Execute, MatmulBiasTransposeReshape) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> bias_shape {2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reshape_shape {4, 8};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });

    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t transpose_op(1, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reshape_op(2, impl::op_kind::StaticReshape, "reshape_op");
    reshape_op.set_attr(impl::op_attr::shape, reshape_shape);
    reshape_op.set_attr(impl::op_attr::special_zero, false);

    // prepare logical tensor
    impl::logical_tensor_t src_f32
            = utils::logical_tensor_init(0, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(1, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(2, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(3, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            4, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t reshape_f32 = utils::logical_tensor_init(
            5, reshape_shape, impl::data_type::f32);

    matmul_op.add_input(src_f32);
    matmul_op.add_input(weight_f32);
    matmul_op.add_input(bias_f32);
    matmul_op.add_output(dst_f32);

    transpose_op.add_input(dst_f32);
    transpose_op.add_output(transpose_f32);

    reshape_op.add_input(transpose_f32);
    reshape_op.add_output(reshape_f32);

    impl::graph_t g(engine.kind());
    g.add_op(&matmul_op);
    g.add_op(&transpose_op);
    g.add_op(&reshape_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("matmul_transpose_optional_reshape_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_f32, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&reshape_f32};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_f32_ts(src_f32, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<float> dst_out_data(product(dst_shape));
    impl::tensor_t dst_f32_ts(reshape_f32, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(
                &strm, {src_f32_ts, weight_f32_ts, bias_f32_ts}, {dst_f32_ts});
        strm.wait();
    }
}

TEST(Execute, MatmulTransposeReorder) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reorder_stride {16, 8, 2, 1};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });

    impl::op_t matmul_op(1, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t transpose_op(2, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reorder_op(3, impl::op_kind::Reorder, "reorder_op");

    // prepare logical tensor
    impl::logical_tensor_t src_f32
            = utils::logical_tensor_init(0, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(1, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(2, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            3, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t reorder_f32 = utils::logical_tensor_init(
            4, transpose_shape, reorder_stride, impl::data_type::f32);

    matmul_op.add_input(src_f32);
    matmul_op.add_input(weight_f32);
    matmul_op.add_output(dst_f32);

    transpose_op.add_input(dst_f32);
    transpose_op.add_output(transpose_f32);

    reorder_op.add_input(transpose_f32);
    reorder_op.add_output(reorder_f32);

    impl::graph_t g(engine.kind());
    g.add_op(&matmul_op);
    g.add_op(&transpose_op);
    g.add_op(&reorder_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("matmul_transpose_reorder_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {&src_f32, &weight_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&reorder_f32};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_f32_ts(src_f32, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    test::vector<float> dst_out_data(product(dst_shape));
    impl::tensor_t dst_ts(reorder_f32, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_f32_ts, weight_f32_ts}, {dst_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMatmulBiasTransposeReorder) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> bias_shape {2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reorder_stride {16, 8, 2, 1};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));
    test::vector<float> bias_data(product(bias_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    std::generate(bias_data.begin(), bias_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 255.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t qweight_op(2, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 3);

    impl::op_t dqweight_op(3, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 3);

    impl::op_t matmul_op(4, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t transpose_op(5, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reorder_op(6, impl::op_kind::Reorder, "reorder_op");

    impl::op_t qout_op(7, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(3, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t bias_f32
            = utils::logical_tensor_init(6, bias_shape, impl::data_type::f32);
    bias_f32.property = impl::property_type::constant;
    impl::logical_tensor_t dst_f32
            = utils::logical_tensor_init(7, dst_shape, impl::data_type::f32);
    impl::logical_tensor_t transpose_f32 = utils::logical_tensor_init(
            8, transpose_shape, impl::data_type::f32);
    impl::logical_tensor_t reorder_f32 = utils::logical_tensor_init(
            10, transpose_shape, reorder_stride, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            15, transpose_shape, reorder_stride, impl::data_type::s8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    matmul_op.add_input(src_f32_dq);
    matmul_op.add_input(weight_f32_dq);
    matmul_op.add_input(bias_f32);
    matmul_op.add_output(dst_f32);

    transpose_op.add_input(dst_f32);
    transpose_op.add_output(transpose_f32);

    reorder_op.add_input(transpose_f32);
    reorder_op.add_output(reorder_f32);

    qout_op.add_input(reorder_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&matmul_op);
    g.add_op(&transpose_op);
    g.add_op(&reorder_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_matmul_transpose_reorder_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {
            &src_u8, &weight_f32, &bias_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    impl::tensor_t bias_f32_ts(bias_f32, &engine, bias_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts, bias_f32_ts}, {dst_ts});
        strm.wait();
    }
}

TEST(ExecuteSubgraphInt8, QuantWeiMixBf16MatmulTransposeReorder) {
    impl::engine_t &engine = get_engine();
    impl::stream_t &strm = get_stream();

    std::vector<int64_t> src_shape {2, 4, 2, 2};
    std::vector<int64_t> weight_shape {2, 4, 2, 2};
    std::vector<int64_t> dst_shape {2, 4, 2, 2};
    std::vector<int64_t> transpose_order {0, 2, 1, 3};
    std::vector<int64_t> transpose_shape {2, 2, 4, 2};
    std::vector<int64_t> reorder_stride {16, 8, 2, 1};

    // prepare fp32 data
    test::vector<uint8_t> src_data(product(src_shape));
    test::vector<float> weight_data(product(weight_shape));

    // random generate src, weight and bias data
    // random seed = 7
    std::default_random_engine generator(7);
    std::uniform_real_distribution<float> u8_distribution(0.0f, 255.0f);
    std::uniform_real_distribution<float> f32_distribution(0.0f, 1.0f);
    std::generate(src_data.begin(), src_data.end(),
            [&]() { return static_cast<uint8_t>(u8_distribution(generator)); });
    std::generate(weight_data.begin(), weight_data.end(),
            [&]() { return f32_distribution(generator); });
    float scale_src = 1 / 255.f; // map to 0~255
    float scale_out = 1 / 255.f;
    int64_t zp_src = 121;
    int64_t zp_out = 0;

    std::vector<float> scale_wei(1, 1 / 127.f);
    std::vector<int64_t> zp_wei(1, 0);

    impl::op_t dqdata_op(1, impl::op_kind::Dequantize, "dqdata_op");
    dqdata_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqdata_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_src});
    dqdata_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_src});
    dqdata_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    impl::op_t tcdata_op(2, impl::op_kind::TypeCast, "tcdata_op");

    impl::op_t qweight_op(3, impl::op_kind::Quantize, "qweight_op");
    qweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    qweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    qweight_op.set_attr<int64_t>(impl::op_attr::axis, 3);

    impl::op_t dqweight_op(4, impl::op_kind::Dequantize, "dqweight_op");
    dqweight_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    dqweight_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, zp_wei);
    dqweight_op.set_attr<std::vector<float>>(impl::op_attr::scales, scale_wei);
    dqweight_op.set_attr<int64_t>(impl::op_attr::axis, 3);

    impl::op_t tcweight_op(5, impl::op_kind::TypeCast, "tcweight_op");

    impl::op_t matmul_op(6, impl::op_kind::MatMul, "matmul_op");
    matmul_op.set_attr<bool>(impl::op_attr::transpose_a, false);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, false);

    impl::op_t transpose_op(7, impl::op_kind::StaticTranspose, "transpose_op");
    transpose_op.set_attr(impl::op_attr::order, transpose_order);

    impl::op_t reorder_op(8, impl::op_kind::Reorder, "reorder_op");

    impl::op_t tcout_op(9, impl::op_kind::TypeCast, "tcout_op");

    impl::op_t qout_op(10, impl::op_kind::Quantize, "qout_op");
    qout_op.set_attr<std::string>(impl::op_attr::qtype, "per_tensor");
    qout_op.set_attr<std::vector<int64_t>>(impl::op_attr::zps, {zp_out});
    qout_op.set_attr<std::vector<float>>(impl::op_attr::scales, {scale_out});
    qout_op.set_attr<int64_t>(impl::op_attr::axis, 0);

    // prepare logical tensor
    impl::logical_tensor_t src_u8
            = utils::logical_tensor_init(1, src_shape, impl::data_type::u8);
    impl::logical_tensor_t src_f32_dq
            = utils::logical_tensor_init(2, src_shape, impl::data_type::f32);
    impl::logical_tensor_t src_bf16_dq
            = utils::logical_tensor_init(3, src_shape, impl::data_type::bf16);
    impl::logical_tensor_t weight_f32
            = utils::logical_tensor_init(4, weight_shape, impl::data_type::f32);
    weight_f32.property = impl::property_type::constant;
    impl::logical_tensor_t weight_s8
            = utils::logical_tensor_init(5, weight_shape, impl::data_type::s8);
    impl::logical_tensor_t weight_f32_dq
            = utils::logical_tensor_init(6, weight_shape, impl::data_type::f32);
    impl::logical_tensor_t weight_bf16_dq = utils::logical_tensor_init(
            7, weight_shape, impl::data_type::bf16);
    impl::logical_tensor_t dst_bf16
            = utils::logical_tensor_init(8, dst_shape, impl::data_type::bf16);
    impl::logical_tensor_t transpose_bf16 = utils::logical_tensor_init(
            9, transpose_shape, impl::data_type::bf16);
    impl::logical_tensor_t reorder_bf16 = utils::logical_tensor_init(
            10, transpose_shape, reorder_stride, impl::data_type::bf16);
    impl::logical_tensor_t reorder_f32 = utils::logical_tensor_init(
            11, transpose_shape, reorder_stride, impl::data_type::f32);
    impl::logical_tensor_t dst_s8 = utils::logical_tensor_init(
            12, transpose_shape, reorder_stride, impl::data_type::s8);

    dqdata_op.add_input(src_u8);
    dqdata_op.add_output(src_f32_dq);

    tcdata_op.add_input(src_f32_dq);
    tcdata_op.add_output(src_bf16_dq);

    qweight_op.add_input(weight_f32);
    qweight_op.add_output(weight_s8);

    dqweight_op.add_input(weight_s8);
    dqweight_op.add_output(weight_f32_dq);

    tcweight_op.add_input(weight_f32_dq);
    tcweight_op.add_output(weight_bf16_dq);

    matmul_op.add_input(src_bf16_dq);
    matmul_op.add_input(weight_bf16_dq);
    matmul_op.add_output(dst_bf16);

    transpose_op.add_input(dst_bf16);
    transpose_op.add_output(transpose_bf16);

    reorder_op.add_input(transpose_bf16);
    reorder_op.add_output(reorder_bf16);

    tcout_op.add_input(reorder_bf16);
    tcout_op.add_output(reorder_f32);

    qout_op.add_input(reorder_f32);
    qout_op.add_output(dst_s8);

    impl::graph_t g(engine.kind());
    g.add_op(&dqdata_op);
    g.add_op(&tcdata_op);
    g.add_op(&qweight_op);
    g.add_op(&dqweight_op);
    g.add_op(&tcweight_op);
    g.add_op(&matmul_op);
    g.add_op(&transpose_op);
    g.add_op(&reorder_op);
    g.add_op(&tcout_op);
    g.add_op(&qout_op);
    g.build_graph();

    // -------------------------case 2----------------------------------
    impl::pass::pass_base_ptr apass
            = get_pass("int8_bf16_matmul_transpose_reorder_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> lt_ins {&src_u8, &weight_f32};
    std::vector<const impl::logical_tensor_t *> lt_outs {&dst_s8};

    p.compile(&cp, lt_ins, lt_outs, &engine);

    impl::tensor_t src_u8_ts(src_u8, &engine, src_data.data());
    impl::tensor_t weight_f32_ts(weight_f32, &engine, weight_data.data());
    test::vector<int8_t> dst_out_data(product(dst_shape));
    impl::tensor_t dst_ts(dst_s8, &engine, dst_out_data.data());
    for (size_t iter = 0; iter < 5; iter++) {
        cp.execute(&strm, {src_u8_ts, weight_f32_ts}, {dst_ts});
        strm.wait();
    }
}

TEST(Execute, MatmulStridedScalarOutput) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);
    impl::engine_t &eng = get_engine();

    test::vector<float> src_data {-2.0, -1.5, 1.0};
    test::vector<float> weight_data {-2.0, -1.5, 1.0};
    test::vector<float> ref_dst_data {7.25};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {3}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {3}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            2, {}, impl::data_type::f32, impl::layout_type::strided);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    ASSERT_EQ(p.compile(&cp, inputs, outputs, &eng), impl::status::success);

    // output should be a scalar (ndims=0, layout_type=strided)
    impl::logical_tensor_t scalar_lt;
    cp.query_logical_tensor(dst.id, &scalar_lt);
    ASSERT_EQ(scalar_lt.layout_type, impl::layout_type::strided);
    ASSERT_EQ(scalar_lt.ndims, 0);

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, weight_data.data());
    impl::tensor_t dst_ts(scalar_lt, &eng, dst_data.data());

    impl::stream_t &strm = get_stream();
    ASSERT_EQ(cp.execute(&strm, {src_ts, weight_ts}, {dst_ts}),
            impl::status::success);
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulBiasAddReluFusion) {
    impl::op_t matmul_op(0, impl::op_kind::MatMul, "matmul_op");
    impl::op_t add_op(1, impl::op_kind::Add, "add_op");
    impl::op_t relu_op(2, impl::op_kind::ReLU, "relu_op");
    impl::engine_t &engine = get_engine();

    test::vector<float> src_data {-2.0, -1.5};
    test::vector<float> weight_data {2.0, -1.5};
    test::vector<float> bias_data {1.0};
    test::vector<float> post_src_data {-2.0};
    test::vector<float> ref_dst_data {0.0};
    test::vector<float> dst_data(ref_dst_data.size(), 0.0);

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 1}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t post_src
            = utils::logical_tensor_init(3, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            4, {1, 1}, impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t add_dst
            = utils::logical_tensor_init(5, {1, 1}, impl::data_type::f32);
    impl::logical_tensor_t relu_dst
            = utils::logical_tensor_init(6, {1, 1}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);
    add_op.add_input(dst);
    add_op.add_input(post_src);
    add_op.add_output(add_dst);
    relu_op.add_input(add_dst);
    relu_op.add_output(relu_dst);

    impl::graph_t g(engine.kind());
    ASSERT_EQ(g.add_op(&matmul_op), impl::status::success);
    ASSERT_EQ(g.add_op(&add_op), impl::status::success);
    ASSERT_EQ(g.add_op(&relu_op), impl::status::success);
    g.build_graph();

    impl::pass::pass_base_ptr apass
            = get_pass("matmul_bias_post_ops_chain_fusion");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {
            &src, &weight, &bias, &post_src};
    std::vector<const impl::logical_tensor_t *> outputs {&relu_dst};

    p.compile(&cp, inputs, outputs, &engine);

    impl::tensor_t src_ts(src, &engine, src_data.data());
    impl::tensor_t weight_ts(weight, &engine, weight_data.data());
    impl::tensor_t bias_ts(bias, &engine, bias_data.data());
    impl::tensor_t post_src_ts(post_src, &engine, post_src_data.data());
    impl::tensor_t dst_ts(relu_dst, &engine, dst_data.data());

    impl::stream_t &strm = get_stream();
    cp.execute(&strm, {src_ts, weight_ts, bias_ts, post_src_ts}, {dst_ts});
    strm.wait();
    for (size_t i = 0; i < ref_dst_data.size(); ++i) {
        ASSERT_FLOAT_EQ(dst_data[i], ref_dst_data[i]);
    }
}

TEST(Execute, MatmulEmptyInput) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    impl::engine_t &eng = get_engine();

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {2, 3, 4}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {2, 4, 0}, impl::data_type::f32);
    impl::logical_tensor_t dst = utils::logical_tensor_init(
            3, impl::data_type::f32, impl::layout_type::any);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};

    ASSERT_EQ(p.compile(&cp, inputs, outputs, &eng), impl::status::success);

    impl::logical_tensor_t empty_lt;
    cp.query_logical_tensor(dst.id, &empty_lt);
    ASSERT_EQ(empty_lt.layout_type, impl::layout_type::strided);
    ASSERT_EQ(empty_lt.ndims, 3);
    ASSERT_EQ(empty_lt.dims[0], 2);
    ASSERT_EQ(empty_lt.dims[1], 3);
    ASSERT_EQ(empty_lt.dims[2], 0);

    test::vector<float> src_data {-2.0, -1.5};

    impl::tensor_t src_ts(src, &eng, src_data.data());
    impl::tensor_t weight_ts(weight, &eng, nullptr);
    impl::tensor_t dst_ts(dst, &eng, nullptr);

    impl::stream_t &strm = get_stream();
    ASSERT_EQ(cp.execute(&strm, {src_ts, weight_ts}, {dst_ts}),
            impl::status::success);
    strm.wait();
}

TEST(Compile, InputShapeWithDynamicDim) {
    impl::op_t matmul_op(impl::op_kind::MatMul);
    matmul_op.set_attr<bool>(impl::op_attr::transpose_b, true);
    impl::engine_t &eng = get_engine();

    // prepare logical tensor
    impl::logical_tensor_t src
            = utils::logical_tensor_init(0, {1, 2, 4}, impl::data_type::f32);
    impl::logical_tensor_t weight
            = utils::logical_tensor_init(1, {1, 2, 4}, impl::data_type::f32);
    impl::logical_tensor_t bias
            = utils::logical_tensor_init(2, {1, 2, 2}, impl::data_type::f32);
    impl::logical_tensor_t dst
            = utils::logical_tensor_init(3, {1, 2, 2}, impl::data_type::f32);

    matmul_op.add_input(src);
    matmul_op.add_input(weight);
    matmul_op.add_input(bias);
    matmul_op.add_output(dst);

    impl::graph_t g(eng.kind());
    g.add_op(&matmul_op);
    g.build_graph();

    impl::pass::pass_base_ptr apass = get_pass("matmul_pass");
    apass->run(g);
    ASSERT_EQ(g.get_num_partitions(), 1U);
    auto part = g.get_partitions()[0];

    // compile
    impl::partition_t p;
    p.init(part);

    impl::compiled_partition_t cp(p);

    std::vector<const impl::logical_tensor_t *> inputs {&src, &weight, &bias};
    std::vector<const impl::logical_tensor_t *> outputs {&dst};
    ASSERT_EQ(p.compile(&cp, inputs, outputs, &eng), impl::status::success);

    // case 1: input shape has unknown dim, compilation will fail
    src = utils::logical_tensor_init(0, {1, 2, -1}, impl::data_type::f32);

    impl::op_t matmul_op_2(impl::op_kind::MatMul);
    matmul_op_2.set_attr<bool>(impl::op_attr::transpose_b, true);

    matmul_op_2.add_input(src);
    matmul_op_2.add_input(weight);
    matmul_op_2.add_input(bias);
    matmul_op_2.add_output(dst);

    impl::graph_t g2(eng.kind());
    g2.add_op(&matmul_op_2);
    g2.build_graph();

    apass->run(g2);
    ASSERT_EQ(g2.get_num_partitions(), 1U);
    auto part2 = g2.get_partitions()[0];

    // compile
    impl::partition_t p2;
    p2.init(part2);

    impl::compiled_partition_t cp2(p2);
    ASSERT_EQ(p2.compile(&cp2, inputs, outputs, &eng),
            impl::status::invalid_arguments);

    // case 2: input shape has dynamic dim, pattern will be skipped
    src = utils::logical_tensor_init(0, {1, 2, -2}, impl::data_type::f32);

    impl::op_t matmul_op_3(impl::op_kind::MatMul);
    matmul_op_3.set_attr<bool>(impl::op_attr::transpose_b, true);

    matmul_op_3.add_input(src);
    matmul_op_3.add_input(weight);
    matmul_op_3.add_input(bias);
    matmul_op_3.add_output(dst);

    impl::graph_t g3(eng.kind());
    g3.add_op(&matmul_op_3);
    g3.build_graph();

    apass->run(g3);
    ASSERT_EQ(g3.get_num_partitions(), 0U);
}
