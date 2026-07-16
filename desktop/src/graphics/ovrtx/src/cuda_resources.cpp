#include "toi/ovrtx/cuda_resources.hpp"

#include <string>
#include <utility>

namespace toi::ovrtx {

Result<void> require_cuda_success(cudaError_t result, std::string_view context)
{
    if (result != cudaSuccess) {
        return std::unexpected(make_error(std::string(context) + ": " + cudaGetErrorString(result)));
    }
    return {};
}

CudaStream::CudaStream(cudaStream_t stream)
    : stream_(stream)
{
}

CudaStream::CudaStream(CudaStream&& other) noexcept
    : stream_(std::exchange(other.stream_, nullptr))
{
}

CudaStream& CudaStream::operator=(CudaStream&& other) noexcept
{
    if (this != &other) {
        reset();
        stream_ = std::exchange(other.stream_, nullptr);
    }
    return *this;
}

CudaStream::~CudaStream()
{
    reset();
}

Result<CudaStream> CudaStream::create(int device)
{
    auto set_device = require_cuda_success(cudaSetDevice(device), "cudaSetDevice");
    if (!set_device) {
        return std::unexpected(set_device.error());
    }

    cudaStream_t stream = nullptr;
    auto created =
        require_cuda_success(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
    if (!created) {
        return std::unexpected(created.error());
    }
    return CudaStream(stream);
}

std::uintptr_t CudaStream::ovrtx_handle() const
{
    if (stream_ == nullptr) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(stream_);
}

Result<void> CudaStream::synchronize() const
{
    if (stream_ == nullptr) {
        return {};
    }
    return require_cuda_success(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");
}

void CudaStream::reset()
{
    if (stream_ != nullptr) {
        cudaStreamSynchronize(stream_);
        cudaStreamDestroy(stream_);
        stream_ = nullptr;
    }
}

} // namespace toi::ovrtx
