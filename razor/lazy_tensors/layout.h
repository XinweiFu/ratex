/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Modifications Copyright (c) Facebook, Inc.
 */

#pragma once

#include "lazy_tensors/span.h"
#include "lazy_tensors/types.h"

namespace lazy_tensors {

class Tile {};

class Layout {
 public:
  int64 minor_to_major(int index) const {
    return minor_to_major_.at(index);
  }

  lazy_tensors::Span<const int64> minor_to_major() const {
    return minor_to_major_;
  }

  std::vector<int64>* mutable_minor_to_major() {
    return &minor_to_major_;
  }

  Layout& add_minor_to_major(int64 value) {
    minor_to_major_.push_back(value);
    return *this;
  }

 private:
  std::vector<int64> minor_to_major_;
};

}  // namespace lazy_tensors