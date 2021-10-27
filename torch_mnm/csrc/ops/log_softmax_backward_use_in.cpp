#include "torch_mnm/csrc/ops/log_softmax_backward_use_in.h"
#include "torch_mnm/csrc/ops/mnm_ops.h"

#include "absl/strings/str_join.h"
#include "lazy_tensor_core/csrc/compiler/node_lowering.h"
#include "lazy_tensor_core/csrc/reduction.h"
#include "lazy_tensor_core/csrc/tensor_util.h"
#include "lazy_tensor_core/csrc/torch_util.h"
#include "lazy_tensors/computation_client/util.h"

#include "lazy_tensors/computation_client/debug_macros.h"
#include "lazy_tensors/computation_client/util.h"

namespace torch_lazy_tensors {
namespace ir {
namespace ops {

LogSoftmaxBackwardUseIn::LogSoftmaxBackwardUseIn(const Value& grad_output, const Value& output,
                                                 lazy_tensors::int64 dim, const Value& self)
    : Node(mnm_log_softmax_backward_use_in, {grad_output, output, self}, grad_output.shape(),
           /*num_outputs=*/1, lazy_tensors::util::MHash(dim)),
      dim_(dim) {
}

NodePtr LogSoftmaxBackwardUseIn::Clone(OpList operands) const {
  return MakeNode<LogSoftmaxBackwardUseIn>(operands.at(0), operands.at(1), dim_, operands.at(2));
}

std::string LogSoftmaxBackwardUseIn::ToString() const {
  std::stringstream ss;
  ss << Node::ToString() << ", dim=" << dim_;
  return ss.str();
}

}  // namespace ops
}  // namespace ir
}  // namespace torch_lazy_tensors
