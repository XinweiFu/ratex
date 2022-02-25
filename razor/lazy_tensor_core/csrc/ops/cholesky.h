/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Modifications Copyright (c) Facebook, Inc.
 */

#pragma once

#include "lazy_tensor_core/csrc/ir.h"

namespace torch_lazy_tensors {
namespace ir {
namespace ops {

class Cholesky : public Node {
 public:
  Cholesky(const Value& input, bool lower);

  std::string ToString() const override;

  NodePtr Clone(OpList operands) const override;

  bool lower() const {
    return lower_;
  }

 private:
  bool lower_;
};

}  // namespace ops
}  // namespace ir
}  // namespace torch_lazy_tensors