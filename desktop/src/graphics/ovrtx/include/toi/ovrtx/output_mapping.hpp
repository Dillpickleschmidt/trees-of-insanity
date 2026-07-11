#pragma once

#include "toi/ovrtx/error.hpp"

#include <ovrtx/ovrtx_types.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace toi::ovrtx {

struct Rgba8CudaArrayTensorView {
    const void* cuda_array = nullptr;
    int width = 0;
    int height = 0;
    int channel_count = 0;
};

struct FloatCudaArrayTensorView {
    const DLTensor* tensor = nullptr;
    const void* cuda_array = nullptr;
    int width = 0;
    int height = 0;
};

[[nodiscard]] ovrtx_render_var_output_handle_t find_render_var_output(const ovrtx_render_product_set_outputs_t& outputs,
                                                                      std::string_view render_var_name);
[[nodiscard]] Result<Rgba8CudaArrayTensorView>
require_rgba8_cuda_array_tensor_view(const ovrtx_render_var_output_t& mapped, std::string_view render_var_name);
[[nodiscard]] Result<FloatCudaArrayTensorView>
require_float_cuda_array_tensor_view(const ovrtx_render_var_output_t& mapped, std::string_view render_var_name);

} // namespace toi::ovrtx
