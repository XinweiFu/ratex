"""Adam optimizer"""
import math
from importlib import import_module

import torch
from mnm import distributed as dist

from . import _functional as F
from . import utils

class Adam(torch.optim.Optimizer):
    r"""Implements Adam algorithm.

    It has been proposed in `Adam: A Method for Stochastic Optimization`_.
    The implementation of the L2 penalty follows changes proposed in
    `Decoupled Weight Decay Regularization`_.

    Args:
        params (iterable): iterable of parameters to optimize or dicts defining
            parameter groups
        lr (float, optional): learning rate (default: 1e-3)
        betas (Tuple[float, float], optional): coefficients used for computing
            running averages of gradient and its square (default: (0.9, 0.999))
        eps (float, optional): term added to the denominator to improve
            numerical stability (default: 1e-8)
        weight_decay (float, optional): weight decay (L2 penalty) (default: 0)
        amsgrad (boolean, optional): whether to use the AMSGrad variant of this
            algorithm from the paper `On the Convergence of Adam and Beyond`_
            (default: False)
        mark_step (boolean, optional): whether to mark step after each parameter
            update (default: False)

    .. _Adam\: A Method for Stochastic Optimization:
        https://arxiv.org/abs/1412.6980
    .. _Decoupled Weight Decay Regularization:
        https://arxiv.org/abs/1711.05101
    .. _On the Convergence of Adam and Beyond:
        https://openreview.net/forum?id=ryQu7f-RZ
    """

    def __init__(self, params, lr=1e-3, betas=(0.9, 0.999), eps=1e-8,
                 weight_decay=0, amsgrad=False, mark_step=False):
        if not 0.0 <= lr:
            raise ValueError("Invalid learning rate: {}".format(lr))
        if not 0.0 <= eps:
            raise ValueError("Invalid epsilon value: {}".format(eps))
        if not 0.0 <= betas[0] < 1.0:
            raise ValueError("Invalid beta parameter at index 0: {}".format(betas[0]))
        if not 0.0 <= betas[1] < 1.0:
            raise ValueError("Invalid beta parameter at index 1: {}".format(betas[1]))
        if not 0.0 <= weight_decay:
            raise ValueError("Invalid weight_decay value: {}".format(weight_decay))
        # TODO(@hzfan): support amsgrad
        if not amsgrad is False:
            raise NotImplementedError("amsgrad==True is not yet supported")
        defaults = dict(lr=lr, betas=betas, eps=eps,
                        weight_decay=weight_decay, amsgrad=amsgrad)
        super(Adam, self).__init__(params, defaults)
        # Distributed configs
        dctx = dist.get_context()
        self._zero_opt_level = dctx.zero_opt_level
        self._rank = dctx.rank
        self._world_size = dctx.size
        self._lm = import_module("lazy_tensor_core.core.lazy_model") if mark_step else None

    def __setstate__(self, state):
        super(Adam, self).__setstate__(state)
        for group in self.param_groups:
            group.setdefault('amsgrad', False)

    @torch.no_grad()
    def step(self, closure=None):
        """Performs a single optimization step.

        Args:
            closure (callable, optional): A closure that reevaluates the model
                and returns the loss.
        """
        loss = None
        if closure is not None:
            with torch.enable_grad():
                loss = closure()

        for group in self.param_groups:
            beta1, beta2 = group['betas']

            for p in group['params']:
                if p.grad is not None:
                    param_with_grad_global = p
                    if p.grad.is_sparse:
                        raise RuntimeError('Adam does not support sparse gradients, please consider SparseAdam instead')

                    state = self.state[p]
                    # Lazy state initialization
                    if len(state) == 0:
                        state['step'] = 0
                        # FIXME: lowering zeros_like to ltc triggers compile error
                        # state['exp_avg'] = torch.zeros_like(p, memory_format=torch.preserve_format)
                        # state['exp_avg_sq'] = torch.zeros_like(p, memory_format=torch.preserve_format)
                        if self._need_partition(p):
                            state['exp_avg'] = self._create_partitioned_buffer(p)
                            state['exp_avg_sq'] = self._create_partitioned_buffer(p)
                        else:
                            state['exp_avg'] = torch.zeros(p.data.size(), dtype=p.data.dtype).to(device=p.data.device)
                            state['exp_avg_sq'] = torch.zeros(p.data.size(), dtype=p.data.dtype).to(device=p.data.device)

                    exp_avg = state['exp_avg']
                    exp_avg_sq = state['exp_avg_sq']
                    assert exp_avg.shape == exp_avg_sq.shape
                    param_with_grad_local = self._partition(p.data, exp_avg)
                    grad = self._partition(p.grad, exp_avg)
                    state['step'] += 1
                    state_step = state['step']

                    updated_param_with_grad_local = F.adam(
                        [param_with_grad_local],
                        [grad],
                        [exp_avg],
                        [exp_avg_sq],
                        [],
                        [state_step],
                        amsgrad=group['amsgrad'],
                        beta1=beta1,
                        beta2=beta2,
                        lr=group['lr'],
                        weight_decay=group['weight_decay'],
                        eps=group['eps']
                    )

                    if self._need_partition(param_with_grad_global):
                        # This line will be eventually replaced by allgather,
                        # so the index doesn't matter
                        index = torch.zeros(updated_param_with_grad_local.size(), dtype=torch.int64).to(device=updated_param_with_grad_local.device)
                        param_with_grad_global.scatter_(dim=0, index=index, src=updated_param_with_grad_local)
                        index = None
                    else:
                        param_with_grad_global[:] = updated_param_with_grad_local

                    if self._lm:
                        updated_param_with_grad_local = None
                        self._lm.mark_step()
        return loss

    def _need_partition(self, data):
        return self._zero_opt_level > 0 and data.shape[0] >= self._world_size

    def _create_partitioned_buffer(self, data):
        new_shape = list(data.shape)
        new_shape[0] = math.ceil(new_shape[0] / self._world_size)
        return torch.zeros(*new_shape, dtype=data.dtype, requires_grad=False).to(device=data.device)

    def _partition(self, global_data, local_data):
        if self._need_partition(global_data):
            part_size = local_data.shape[0]
            # Pad the tensor if it's not divisble by the number of ranks
            # TODO: Instead of padding the entire tensor, we should pad only the last
            # partitioned part to have better performance
            if self._world_size * part_size > global_data.shape[0]:
                pad_width = [0 for _ in range(len(global_data.shape) * 2)]
                pad_width[-1] = self._world_size * part_size - global_data.shape[0]
                part_data = torch.nn.functional.pad(global_data, pad_width)
            else:
                part_data = global_data
            part_data = part_data[self._rank * part_size : (self._rank + 1) * part_size]
        else:
            part_data = global_data
        return part_data
