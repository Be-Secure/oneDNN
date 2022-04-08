/*******************************************************************************
 * Copyright 2022 Intel Corporation
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

#include <utility>

#include <compiler/jit/xbyak/ir/xbyak_visitor.hpp>

#include "ir_data_initializer.hpp"

namespace sc {
namespace sc_xbyak {

class ir_data_initializer_impl_t : public xbyak_visitor_t {
public:
    using xbyak_visitor_t::dispatch;
    using xbyak_visitor_t::visit;

    ir_data_initializer_impl_t() = default;

    // dispatch override
    func_c dispatch(func_c v) override {
        for (auto &p : v->params_) {
            initialize_expr_data(p, nullptr);
        }
        return xbyak_visitor_t::dispatch(std::move(v));
    }

    expr_c dispatch(expr_c v) override {
        initialize_expr_data(v, nullptr);
        update_spill_weight(v);
        return xbyak_visitor_t::dispatch(std::move(v));
    }

    stmt_c dispatch(stmt_c v) override {
        initialize_stmt_data(v);
        return xbyak_visitor_t::dispatch(std::move(v));
    }

    // visit override
    stmt_c visit(define_c v) override {
        initialize_expr_data(v->var_, current_scope());
        return xbyak_visitor_t::visit(std::move(v));
    }

    stmt_c visit(for_loop_c v) override {
        initialize_expr_data(v->var_, current_scope());
        return xbyak_visitor_t::visit(std::move(v));
    }

private:
    void initialize_expr_data(const expr_c &v, const stmt_base_t *def_scope) {
        if (!v->temp_data().isa<xbyak_expr_data_t>()) {
            v->temp_data() = xbyak_expr_data_t();
        }
        if (def_scope) { GET_EXPR_DATA(v).def_scope_ = def_scope; }
    }

    void initialize_stmt_data(const stmt_c &v) {
        if (!v->temp_data().isa<xbyak_stmt_data_t>()) {
            v->temp_data() = xbyak_stmt_data_t(loop_depth());
        }
    }

    void update_spill_weight(const expr_c &v) {
        GET_VIRTUAL_REG(v).add_weight(32 * loop_depth() + 1);
    }
};

func_c ir_data_initializer_t::operator()(func_c v) {
    ir_data_initializer_impl_t ir_data_initializer;

    return ir_data_initializer.dispatch(std::move(v));
}

} // namespace sc_xbyak
} // namespace sc