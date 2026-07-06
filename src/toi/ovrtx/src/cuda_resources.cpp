#include "toi/ovrtx/cuda_resources.hpp"

#include <string>
#include <utility>

namespace toi::ovrtx {
namespace {

[[nodiscard]] cudaStream_t cuda_stream_from_ovrtx(std::uintptr_t stream)
{
    if (stream <= std::uintptr_t{1}) {
        return nullptr;
    }
    return reinterpret_cast<cudaStream_t>(stream);
}

} // namespace

Result<void> require_cuda_success(cudaError_t result, std::string_view context)
{
    if (result != cudaSuccess) {
        return std::unexpected(make_error(std::string(context) + ": " + cudaGetErrorString(result)));
    }
    return {};
}

CudaDeviceBuffer::CudaDeviceBuffer(void* data)
    : data_(data)
{
}

CudaDeviceBuffer::CudaDeviceBuffer(CudaDeviceBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr))
    , before_destroy_stream_(std::exchange(other.before_destroy_stream_, 0))
{
}

CudaDeviceBuffer& CudaDeviceBuffer::operator=(CudaDeviceBuffer&& other) noexcept
{
    if (this != &other) {
        reset();
        data_ = std::exchange(other.data_, nullptr);
        before_destroy_stream_ = std::exchange(other.before_destroy_stream_, 0);
    }
    return *this;
}

CudaDeviceBuffer::~CudaDeviceBuffer()
{
    reset();
}

Result<CudaDeviceBuffer> CudaDeviceBuffer::create(std::size_t byte_count)
{
    void* data = nullptr;
    auto allocated = require_cuda_success(cudaMalloc(&data, byte_count), "cudaMalloc");
    if (!allocated) {
        return std::unexpected(allocated.error());
    }
    return CudaDeviceBuffer(data);
}

void* CudaDeviceBuffer::data() const
{
    return data_;
}

void CudaDeviceBuffer::set_before_destroy_stream(std::uintptr_t stream)
{
    before_destroy_stream_ = stream;
}

void CudaDeviceBuffer::reset()
{
    if (data_ == nullptr) {
        return;
    }
    if (before_destroy_stream_ == 0) {
        cudaFree(data_);
    } else {
        cudaFreeAsync(data_, cuda_stream_from_ovrtx(before_destroy_stream_));
    }
    data_ = nullptr;
    before_destroy_stream_ = 0;
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
