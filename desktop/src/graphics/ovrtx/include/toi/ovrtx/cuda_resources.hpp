#pragma once

#include "toi/ovrtx/error.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace toi::ovrtx {

[[nodiscard]] Result<void> require_cuda_success(cudaError_t result, std::string_view context);

class CudaDeviceBuffer {
public:
    CudaDeviceBuffer() = default;
    CudaDeviceBuffer(const CudaDeviceBuffer&) = delete;
    CudaDeviceBuffer& operator=(const CudaDeviceBuffer&) = delete;
    CudaDeviceBuffer(CudaDeviceBuffer&& other) noexcept;
    CudaDeviceBuffer& operator=(CudaDeviceBuffer&& other) noexcept;
    ~CudaDeviceBuffer();

    [[nodiscard]] static Result<CudaDeviceBuffer> create(std::size_t byte_count);
    [[nodiscard]] void* data() const;
    void set_before_destroy_stream(std::uintptr_t stream);

private:
    explicit CudaDeviceBuffer(void* data);
    void reset();

    void* data_ = nullptr;
    std::uintptr_t before_destroy_stream_ = 0;
};

class CudaStream {
public:
    CudaStream() = default;
    explicit CudaStream(cudaStream_t stream);
    CudaStream(const CudaStream&) = delete;
    CudaStream& operator=(const CudaStream&) = delete;
    CudaStream(CudaStream&& other) noexcept;
    CudaStream& operator=(CudaStream&& other) noexcept;
    ~CudaStream();

    [[nodiscard]] static Result<CudaStream> create(int device);
    [[nodiscard]] std::uintptr_t ovrtx_handle() const;
    [[nodiscard]] Result<void> synchronize() const;

private:
    void reset();

    cudaStream_t stream_ = nullptr;
};

} // namespace toi::ovrtx
