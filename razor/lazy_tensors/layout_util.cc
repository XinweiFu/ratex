/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Modifications Copyright (c) Facebook, Inc.
 */

#include "lazy_tensors/layout_util.h"

#include "lazy_tensors/core/platform/hash.h"

namespace lazy_tensors {

size_t LayoutUtil::Hash(const Layout& layout) {
  size_t hash_value = std::hash<size_t>()(0);

  for (int64 minor_to_major : layout.minor_to_major()) {
    hash_value = Hash64Combine(hash_value, hash<int64>()(minor_to_major));
  }

  return hash_value;
}

}  // namespace lazy_tensors