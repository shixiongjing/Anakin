#include "framework/operators/input.h"

namespace anakin {

namespace ops {

#define INSTANCE_INPUT(Ttype, Ptype) \
template<> \
void InputSplit<Ttype, Ptype>::operator()(OpContext<Ttype>& ctx, \
      const std::vector<Tensor4dPtr<Ttype>>& ins, \
      std::vector<Tensor4dPtr<Ttype>>& outs) {}


template<typename Ttype, Precision Ptype>
Status InputSplitHelper<Ttype, Ptype>::InitParam() {
    LOG(WARNING) << "Parsing Input op parameter.";
    input_shape = GET_PARAMETER(PTuple<int>, input_shape);
    if (CHECK_PARAMETER(max_len)) {
        max_len = GET_PARAMETER(int , max_len);
    }
    if (CHECK_PARAMETER(max_batch)) {
        max_batch = GET_PARAMETER(int , max_batch);
    }

    for (int i = 0; i < input_shape.size(); i++) {
        LOG(INFO) << " |-- shape [" << i << "]: " << input_shape[i];
    }

    return Status::OK();
}

template<typename Ttype, Precision Ptype>
Status InputSplitHelper<Ttype, Ptype>::Init(OpContext<Ttype> &ctx,
                                              const std::vector<Tensor4dPtr<Ttype>> &ins,
                                              std::vector<Tensor4dPtr<Ttype>> &outs) {
    return Status::OK();
}

template<typename Ttype, Precision Ptype>
Status InputSplitHelper<Ttype, Ptype>::InferShape(const std::vector<Tensor4dPtr<Ttype>> &ins,
                               std::vector<Tensor4dPtr<Ttype>> &outs) {
    saber::Shape out_shape;
    for (int i = 0; i < input_shape.size(); i++) {
        out_shape.push_back(input_shape[i]);
    }
    for (auto& tensor_p : outs) {
        DLOG(INFO)<<"init input shape "<<out_shape;
        tensor_p->set_shape_without_layout(out_shape);
    }
    if (max_len != 0 && max_batch != 0) {
        std::vector<std::vector<int>> seq_offset(1, std::vector<int>(max_batch + 1, 0));
        int i;
        for (i = 0; i < max_batch; i++) {
            seq_offset[0][i] = i * max_len;
        }
        seq_offset[0][i] = i * max_len;
        for (auto& tensor_p : outs) {
            tensor_p->set_seq_offset(seq_offset);
        }
    }
    
    return Status::OK();
}

#ifdef USE_CUDA
INSTANCE_INPUT(NV, Precision::FP32);
template class InputSplitHelper<NV, Precision::FP32>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, NV, Precision::FP32);
INSTANCE_INPUT(NV, Precision::INT8);
template class InputSplitHelper<NV, Precision::INT8>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, NV, Precision::INT8);
#endif
#ifdef USE_MLU
INSTANCE_INPUT(MLU, Precision::FP32);
template class InputSplitHelper<MLU, Precision::FP32>;
template class InputSplitHelper<MLU, Precision::FP16>;
template class InputSplitHelper<MLU, Precision::INT8>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, MLU, Precision::FP32);
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, MLU, Precision::FP16);
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, MLU, Precision::INT8);
#endif


#ifdef USE_ARM_PLACE
INSTANCE_INPUT(ARM, Precision::FP32);
template class InputSplitHelper<ARM, Precision::FP32>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, ARM, Precision::FP32);
#endif //arm

#if defined USE_X86_PLACE || defined BUILD_LITE
INSTANCE_INPUT(X86, Precision::FP32);
template class InputSplitHelper<X86, Precision::FP32>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, X86, Precision::FP32);
#endif

#ifdef AMD_GPU
INSTANCE_INPUT(AMD, Precision::FP32);
template class InputSplitHelper<AMD, Precision::FP32>;
ANAKIN_REGISTER_OP_HELPER(InputSplit, InputSplitHelper, AMD, Precision::FP32);
#endif

//! register op
ANAKIN_REGISTER_OP(Input)
.Doc("Input Split operator [ only a input data holder and reshape and split ] ")
#ifdef USE_CUDA
.__alias__<NV, Precision::FP32>("inputsplit")
#endif
#ifdef USE_MLU
.__alias__<MLU, Precision::FP32>("inputsplit")
#endif  // USE_MLU
#ifdef AMD_GPU
    .__alias__<AMD, Precision::FP32>("inputsplit")
#endif
#ifdef USE_ARM_PLACE
.__alias__<ARM, Precision::FP32>("inputsplit")
#endif
#if defined USE_X86_PLACE || defined BUILD_LITE
.__alias__<X86, Precision::FP32>("inputsplit")
#endif
.Args<PTuple<int>>("input_shape", " shape of graph input.");

} /* namespace ops */

} /* namespace anakin */


