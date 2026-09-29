
#include <vk2s/Device.hpp>
#include <Scene.hpp>

#include <nlohmann/json.hpp>
#include <ng-log/logging.h>

#include <Options.hpp>
#include <SceneBuilder.hpp>
#include <Progress.hpp>
#include <ConsoleLogging.hpp>

#include <Integrators/MotionPathIntegrator.hpp>
#include <Integrators/EventIntegrator.hpp>
#include <Integrators/EfficientEventIntegrator.hpp>
#include <Mesh.hpp>
#include <Material.hpp>
#include <Transform.hpp>
#include <Emitter.hpp>
#include <AnimatedTransform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <tinyexr.h>

#if defined(WITH_OIDN)
#include <OpenImageDenoise/oidn.hpp>

static oidn::DeviceRef oidnDevice = nullptr;
static oidn::FilterRef oidnFilter = nullptr;
#endif  // defined(WITH_OIDN)

#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/string_cast.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <locale>
#include <cstdlib>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <limits>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <random>
#include <optional>

struct Event
{
    constexpr static uint32_t kPositive = 1;
    constexpr static uint32_t kNegative = 0;

    uint32_t x;
    uint32_t y;
    uint32_t p;  // polarity
    double t;    // timestamp [us]

    // for event sorting
    bool operator<(const Event& right) const
    {
        if (t == right.t)
        {
            if (x == right.x)
            {
                return y < right.y;
            }

            return x < right.x;
        }

        return t < right.t;
    }
};

struct EventStats
{
    uint64_t allEventHappenedPixelNum           = 0;
    uint64_t allFloodedPixelNum                 = 0;
    std::pair<double, double> happenedTimeRange = std::make_pair(1., 0.);
    double avgHappenedTime                      = 0.;
    uint32_t previousFrameEventNum              = 0;
};

struct RowResult
{
    std::vector<Event> events;
    uint32_t eventHappenedPixelNum = 0;
    uint32_t floodedPixelNum       = 0;
    std::pair<double, double> timeRange{ 1.0, 0.0 };
    double happenedTime = 0.0;
};

namespace
{

    std::string elapsedSeconds(const std::chrono::steady_clock::time_point start)
    {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(1) << seconds << " s";
        return out.str();
    }

    void makeClearedImagesAvailable(vk2s::Command& command)
    {
        vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
                                  vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        command.globalPipelineBarrier(barrier, vk::PipelineStageFlagBits::eTransfer,
                                      vk::PipelineStageFlagBits::eRayTracingShaderKHR);
    }

    void makeEventStateAvailable(vk2s::Command& command)
    {
        // Includes initial luminance and discrete-mode pool writes for the next frame.
        vk::MemoryBarrier barrier(vk::AccessFlagBits::eShaderWrite,
                                  vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        command.globalPipelineBarrier(barrier, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                                      vk::PipelineStageFlagBits::eRayTracingShaderKHR);
    }

    size_t checkedImageByteSize(const vk::Extent3D& extent, const size_t bytesPerPixel)
    {
        if (extent.width == 0 || extent.height == 0 || extent.depth == 0 || bytesPerPixel == 0)
        {
            throw std::invalid_argument("invalid image dimensions or pixel size");
        }

        const auto checkedMultiply = [](const size_t a, const size_t b)
        {
            if (b != 0 && a > std::numeric_limits<size_t>::max() / b)
            {
                throw std::overflow_error("image byte size overflow");
            }
            return a * b;
        };

        size_t size = checkedMultiply(extent.width, extent.height);
        size        = checkedMultiply(size, extent.depth);
        return checkedMultiply(size, bytesPerPixel);
    }

    size_t checkedPixelCount2D(const vk::Extent3D& extent)
    {
        return checkedImageByteSize(vk::Extent3D(extent.width, extent.height, 1), 1);
    }

    struct ImageSize
    {
        int width;
        int height;
    };

    ImageSize checkedOutputImageSize(const vk::Extent3D& extent)
    {
        const auto limit = static_cast<uint64_t>(std::numeric_limits<int>::max());

        if (extent.width == 0 || extent.height == 0 || extent.width > limit || extent.height > limit)
        {
            throw std::invalid_argument("image output dimensions out of range");
        }

        return {
            static_cast<int>(extent.width),
            static_cast<int>(extent.height),
        };
    }

    int checkedRgbPngStride(const ImageSize& dimensions)
    {
        if (dimensions.width > std::numeric_limits<int>::max() / 3)
        {
            throw std::overflow_error("PNG row stride overflow");
        }

        return dimensions.width * 3;
    }

    class ScopedReadback
    {
    public:
        ScopedReadback(vk2s::Device& device, Handle<vk2s::Buffer> buffer, const size_t size)
            : mDevice(device)
        {
            if (size == 0 || size > buffer->getSize())
            {
                throw std::out_of_range("invalid readback mapping size");
            }

            mMemory = buffer->getVkDeviceMemory().get();
            mData   = mDevice.getVkDevice()->mapMemory(mMemory, 0, size);
        }

        ~ScopedReadback()
        {
            mDevice.getVkDevice()->unmapMemory(mMemory);
        }

        ScopedReadback(const ScopedReadback&)            = delete;
        ScopedReadback& operator=(const ScopedReadback&) = delete;
        ScopedReadback(ScopedReadback&&)                 = delete;
        ScopedReadback& operator=(ScopedReadback&&)      = delete;

        template <typename T>
        const T* data() const
        {
            return static_cast<const T*>(mData);
        }

    private:
        vk2s::Device& mDevice;
        vk::DeviceMemory mMemory{};
        const void* mData = nullptr;
    };

}  // anonymous namespace

// WARN: At this point, outputImage must be ready to be read.
void saveImage(std::string_view path, Handle<vk2s::Buffer>& stagingBuffer, vk2s::Device& device,
               const vk::Extent3D extent, const size_t size)
{
    const std::string filename(path);
    const auto dimen = checkedOutputImageSize(extent);
    const int stride = checkedRgbPngStride(dimen);

    std::vector<uint8_t> output(checkedImageByteSize(extent, 3), 0);
    {
        ScopedReadback mapping(device, stagingBuffer, size);
        const auto* p = mapping.data<std::uint8_t>();

#ifdef _OPENMP
        omp_set_num_threads(omp_get_max_threads());
#endif

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                output[index * 3 + 0] = p[index * 4 + 0];
                output[index * 3 + 1] = p[index * 4 + 1];
                output[index * 3 + 2] = p[index * 4 + 2];
            }
        }
    }

    const int res = stbi_write_png(filename.c_str(), dimen.width, dimen.height, 3, output.data(), stride);
    if (res == 0)
    {
        const int error = errno;
        throw std::runtime_error("failed to write PNG '" + filename + "': " +
                                 (error != 0 ? std::string(std::strerror(error)) : std::string("unknown error")));
    }
}

void saveEXRImage(std::string_view path, Handle<vk2s::Buffer>& stagingBuffer, Handle<vk2s::Buffer>& albedoStagingBuffer,
                  Handle<vk2s::Buffer>& normalStagingBuffer, vk2s::Device& device, const vk::Extent3D extent,
                  const size_t size, bool denoise = false)
{
    const auto dimen        = checkedOutputImageSize(extent);
    const size_t pixelCount = checkedPixelCount2D(extent);

    std::vector<float> output[4];
    output[0] = std::vector<float>(pixelCount);
    output[1] = std::vector<float>(pixelCount);
    output[2] = std::vector<float>(pixelCount);
    output[3] = std::vector<float>(pixelCount);

    std::vector<float> albedo[3];
    std::vector<float> normal[3];

    if (denoise)
    {
        albedo[0] = std::vector<float>(pixelCount);
        albedo[1] = std::vector<float>(pixelCount);
        albedo[2] = std::vector<float>(pixelCount);

        normal[0] = std::vector<float>(pixelCount);
        normal[1] = std::vector<float>(pixelCount);
        normal[2] = std::vector<float>(pixelCount);
    }

    {
        ScopedReadback colorMapping(device, stagingBuffer, size);
        const auto* pColor = colorMapping.data<float>();

        std::optional<ScopedReadback> albedoMapping;
        std::optional<ScopedReadback> normalMapping;

        const float* pAlbedo = nullptr;
        const float* pNormal = nullptr;

        if (denoise)
        {
            albedoMapping.emplace(device, albedoStagingBuffer, size);
            normalMapping.emplace(device, normalStagingBuffer, size);
            pAlbedo = albedoMapping->data<float>();
            pNormal = normalMapping->data<float>();
        }

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                output[0][index] = pColor[index * 4 + 0];
                output[1][index] = pColor[index * 4 + 1];
                output[2][index] = pColor[index * 4 + 2];
                output[3][index] = pColor[index * 4 + 3];

                if (denoise)
                {
                    albedo[0][index] = pAlbedo[index * 4 + 0];
                    albedo[1][index] = pAlbedo[index * 4 + 1];
                    albedo[2][index] = pAlbedo[index * 4 + 2];

                    normal[0][index] = pNormal[index * 4 + 0];
                    normal[1][index] = pNormal[index * 4 + 1];
                    normal[2][index] = pNormal[index * 4 + 2];
                }
            }
        }
    }

    if (denoise)
    {
#if defined(WITH_OIDN)
        auto& filter = oidnFilter.value();

        const size_t denoiseBytes =
            checkedImageByteSize(vk::Extent3D(extent.width, extent.height, 1), 3 * sizeof(float));

        oidn::BufferRef colorBuf  = oidnDevice.newBuffer(denoiseBytes);
        oidn::BufferRef albedoBuf = oidnDevice.newBuffer(denoiseBytes);
        oidn::BufferRef normalBuf = oidnDevice.newBuffer(denoiseBytes);

        filter.setImage("color", colorBuf, oidn::Format::Float3, extent.width, extent.height);
        filter.setImage("albedo", albedoBuf, oidn::Format::Float3, extent.width, extent.height);
        filter.setImage("normal", normalBuf, oidn::Format::Float3, extent.width, extent.height);
        filter.setImage("output", colorBuf, oidn::Format::Float3, extent.width, extent.height);
        filter.set("hdr", true);
        filter.commit();

        float* colorPtr  = reinterpret_cast<float*>(colorBuf.getData());
        float* albedoPtr = reinterpret_cast<float*>(albedoBuf.getData());
        float* normalPtr = reinterpret_cast<float*>(normalBuf.getData());
#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                colorPtr[index * 3 + 0] = output[0][index];
                colorPtr[index * 3 + 1] = output[1][index];
                colorPtr[index * 3 + 2] = output[2][index];

                albedoPtr[index * 3 + 0] = albedo[0][index];
                albedoPtr[index * 3 + 1] = albedo[1][index];
                albedoPtr[index * 3 + 2] = albedo[2][index];

                normalPtr[index * 3 + 0] = normal[0][index];
                normalPtr[index * 3 + 1] = normal[1][index];
                normalPtr[index * 3 + 2] = normal[2][index];
            }
        }

        // execute denoising
        filter.execute();

        // read back denoised image
#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                output[0][index] = colorPtr[index * 3 + 0];
                output[1][index] = colorPtr[index * 3 + 1];
                output[2][index] = colorPtr[index * 3 + 2];
            }
        }
#else
        throw std::runtime_error("The current executable is not compiled with OIDN support!");
#endif
    }

    // save EXR image via tinyEXR (almost same for tinyEXR's tutorial)
    {
        std::array<EXRChannelInfo, 4> channels{};
        std::array<int, 4> pixelTypes{};
        std::array<int, 4> requestedPixelTypes{};

        EXRHeader header;
        InitEXRHeader(&header);

        EXRImage image;
        InitEXRImage(&image);

        image.num_channels = 4;

        float* image_ptr[4];
        image_ptr[0] = &(output[3].at(0));  // A
        image_ptr[1] = &(output[2].at(0));  // B
        image_ptr[2] = &(output[1].at(0));  // G
        image_ptr[3] = &(output[0].at(0));  // R

        image.images = (unsigned char**)image_ptr;
        image.width  = dimen.width;
        image.height = dimen.height;

        header.num_channels = 4;
        header.channels     = channels.data();
        // Must be (A)BGR order, since most of EXR viewers expect this channel order.
        strncpy(header.channels[0].name, "A", 255);
        header.channels[0].name[strlen("A")] = '\0';
        strncpy(header.channels[1].name, "B", 255);
        header.channels[1].name[strlen("B")] = '\0';
        strncpy(header.channels[2].name, "G", 255);
        header.channels[2].name[strlen("G")] = '\0';
        strncpy(header.channels[3].name, "R", 255);
        header.channels[3].name[strlen("R")] = '\0';

        header.pixel_types           = pixelTypes.data();
        header.requested_pixel_types = requestedPixelTypes.data();
        for (int i = 0; i < header.num_channels; i++)
        {
            header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;  // pixel type of input image
            header.requested_pixel_types[i] =
                TINYEXR_PIXELTYPE_HALF;  // pixel type of output image to be stored in .EXR
        }

        const std::string filename(path);
        const char* err = nullptr;

        const int ret = SaveEXRImageToFile(&image, &header, filename.c_str(), &err);

        const std::unique_ptr<const char, decltype(&FreeEXRErrorMessage)> errorMessage(err, &FreeEXRErrorMessage);

        if (ret != TINYEXR_SUCCESS)
        {
            const std::string detail = errorMessage ? errorMessage.get() : "unknown error";

            throw std::runtime_error("tinyEXR error: " + detail);
        }
    }
}

void saveEXRImageEventDebug(std::string_view path, Handle<vk2s::Buffer>& stagingBuffer, vk2s::Device& device,
                            const vk::Extent3D extent, const size_t size)
{
    const auto dimen        = checkedOutputImageSize(extent);
    const size_t pixelCount = checkedPixelCount2D(extent);

    std::vector<float> output[4];
    output[0] = std::vector<float>(pixelCount);
    output[1] = std::vector<float>(pixelCount);
    output[2] = std::vector<float>(pixelCount);
    output[3] = std::vector<float>(pixelCount);

    {
        ScopedReadback mapping(device, stagingBuffer, size);
        const auto* p = mapping.data<float>();

#ifdef _OPENMP
        omp_set_num_threads(omp_get_max_threads());
#endif

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                output[0][index] = p[index * 4 + 0];
                output[1][index] = p[index * 4 + 1];
                output[2][index] = p[index * 4 + 2];
                output[3][index] = p[index * 4 + 3];
            }
        }
    }

    {
        std::array<EXRChannelInfo, 4> channels{};
        std::array<int, 4> pixelTypes{};
        std::array<int, 4> requestedPixelTypes{};

        EXRHeader header;
        InitEXRHeader(&header);

        EXRImage image;
        InitEXRImage(&image);

        image.num_channels = 4;

        float* image_ptr[4];
        image_ptr[0] = &(output[3].at(0));
        image_ptr[1] = &(output[2].at(0));
        image_ptr[2] = &(output[1].at(0));
        image_ptr[3] = &(output[0].at(0));

        image.images = (unsigned char**)image_ptr;
        image.width  = dimen.width;
        image.height = dimen.height;

        header.num_channels = 4;
        header.channels     = channels.data();

        // Must be (A)BGR order, since most of EXR viewers expect this channel order.
        strncpy(header.channels[3].name, "YPos", 255);
        header.channels[3].name[strlen("YPos")] = '\0';

        strncpy(header.channels[2].name, "XPos", 255);
        header.channels[2].name[strlen("XPos")] = '\0';

        strncpy(header.channels[1].name, "var", 255);
        header.channels[1].name[strlen("var")] = '\0';

        strncpy(header.channels[0].name, "L", 255);
        header.channels[0].name[strlen("L")] = '\0';

        header.pixel_types           = pixelTypes.data();
        header.requested_pixel_types = requestedPixelTypes.data();
        for (int i = 0; i < header.num_channels; i++)
        {
            header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;  // pixel type of input image
            header.requested_pixel_types[i] =
                TINYEXR_PIXELTYPE_HALF;  // pixel type of output image to be stored in .EXR
        }

        const std::string filename(path);
        const char* err = nullptr;

        const int ret = SaveEXRImageToFile(&image, &header, filename.c_str(), &err);

        const std::unique_ptr<const char, decltype(&FreeEXRErrorMessage)> errorMessage(err, &FreeEXRErrorMessage);

        if (ret != TINYEXR_SUCCESS)
        {
            const std::string detail = errorMessage ? errorMessage.get() : "unknown error";
            throw std::runtime_error("tinyEXR error: " + detail);
        }
    }
}

void saveEventImage(std::string_view path, Handle<vk2s::Buffer>& stagingBuffer, vk2s::Device& device,
                    const vk::Extent3D extent, const size_t size)
{
    constexpr uint8_t plus[]  = { 255, 255, 255 };
    constexpr uint8_t none[]  = { 128, 128, 128 };
    constexpr uint8_t minus[] = { 0, 0, 0 };

    const std::string filename(path);
    const auto dimen = checkedOutputImageSize(extent);
    const int stride = checkedRgbPngStride(dimen);

    // only see depth 0 image (first event)
    std::vector<uint8_t> output(checkedImageByteSize(vk::Extent3D(extent.width, extent.height, 1), 3), 0);
    {
        ScopedReadback mapping(device, stagingBuffer, size);
        const auto* p = mapping.data<uint32_t>();

#ifdef _OPENMP
        omp_set_num_threads(omp_get_max_threads());
#endif

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            for (size_t w = 0; w < extent.width; ++w)
            {
                const size_t index = static_cast<size_t>(h) * extent.width + w;

                // check only the first event
                const uint32_t nativeVal = p[index * 4];
                const uint16_t val       = static_cast<uint16_t>(nativeVal >> 16);

                if (val == 0)
                {
                    output[index * 3 + 0] = none[0];
                    output[index * 3 + 1] = none[1];
                    output[index * 3 + 2] = none[2];
                }
                else if ((val & 0x8000) == 0)  // minus event
                {
                    output[index * 3 + 0] = minus[0];
                    output[index * 3 + 1] = minus[1];
                    output[index * 3 + 2] = minus[2];
                }
                else  // plus event
                {
                    output[index * 3 + 0] = plus[0];
                    output[index * 3 + 1] = plus[1];
                    output[index * 3 + 2] = plus[2];
                }
            }
        }
    }

    const int res = stbi_write_png(filename.c_str(), dimen.width, dimen.height, 3, output.data(), stride);
    if (res == 0)
    {
        throw std::runtime_error("failed to output!\n");
    }
}

void recordEvents(Handle<vk2s::Buffer> stagingBuffer, const double t0, const Options& options,
                  const vk::Extent3D extent, const size_t size, std::vector<Event>& allEvents, EventStats& stats,
                  vk2s::Device& device)
{
    constexpr uint32_t kTBitNum       = 15;
    constexpr uint32_t kTimestampMask = (1u << kTBitNum) - 1;
    constexpr uint32_t kTimestampMax  = kTimestampMask - 1;

    std::vector<RowResult> rows(extent.height);

    {
        ScopedReadback mapping(device, stagingBuffer, size);
        const auto* pEvents = mapping.data<uint32_t>();

#ifdef _OPENMP
        omp_set_num_threads(omp_get_max_threads());
#endif

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (int h = 0; h < extent.height; ++h)  // int for OpenMP
        {
            auto& row                   = rows[h];
            auto& events                = row.events;
            auto& eventHappenedPixelNum = row.eventHappenedPixelNum;
            auto& floodedPixelNum       = row.floodedPixelNum;
            auto& timeRange             = row.timeRange;
            auto& happenedTime          = row.happenedTime;

            const auto lmdDetectEvent = [&](const uint16_t val, const int w, const size_t h, const size_t d,
                                            const size_t texChannel) -> Event
            {
                const uint32_t timestamp = val & kTimestampMask;
                const double t           = static_cast<double>(timestamp - 1) / static_cast<double>(kTimestampMax);

                Event e{ .x = static_cast<uint32_t>(w), .y = static_cast<uint32_t>(h) };

                // first bit == 1 -> positive, == 0 -> negative
                e.p = (val & 0x8000) == 0 ? Event::kNegative : Event::kPositive;
                e.t = t0 + (*options.deltaTime) * t;

                happenedTime += t;
                if (d == 0 && texChannel == 0)
                {
                    ++eventHappenedPixelNum;
                }

                if (texChannel == 7 && d == extent.depth - 1)
                {
                    ++floodedPixelNum;
                }

                if (timeRange.first > t)
                {
                    timeRange.first = t;
                }

                if (timeRange.second < t)
                {
                    timeRange.second = t;
                }

                return e;
            };

            for (size_t w = 0; w < extent.width; ++w)
            {
                // WARN: abnormal texture loading (event texture is 3D)
                for (size_t d = 0; d < extent.depth; ++d)
                {
                    const size_t index = d * extent.height * extent.width + static_cast<size_t>(h) * extent.width + w;

                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        const uint32_t u32val = pEvents[index * 4 + channel];

                        const uint16_t val1 = static_cast<uint16_t>(u32val >> 16);
                        const uint16_t val2 = static_cast<uint16_t>(u32val & 0x0000FFFF);

                        if (val1 == 0)
                        {
                            break;
                        }
                        events.emplace_back(lmdDetectEvent(val1, w, h, d, channel * 2));

                        if (val2 == 0)
                        {
                            break;
                        }
                        events.emplace_back(lmdDetectEvent(val2, w, h, d, channel * 2 + 1));
                    }
                }
            }
        }
    }

    const size_t originalSize   = allEvents.size();
    const size_t appendCapacity = allEvents.max_size() - originalSize;

    std::vector<size_t> rowOffsets(rows.size() + 1, 0);
    for (size_t h = 0; h < rows.size(); ++h)
    {
        const size_t count = rows[h].events.size();
        if (count > appendCapacity - rowOffsets[h])
        {
            throw std::runtime_error("too many decoded events");
        }

        rowOffsets[h + 1] = rowOffsets[h] + count;
    }

    allEvents.resize(originalSize + rowOffsets.back());

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int h = 0; h < extent.height; ++h)
    {
        const auto& events = rows[h].events;
        std::copy(events.begin(), events.end(), allEvents.begin() + originalSize + rowOffsets[h]);
    }

    for (const auto& row : rows)
    {
        stats.allEventHappenedPixelNum += row.eventHappenedPixelNum;
        stats.allFloodedPixelNum += row.floodedPixelNum;
        stats.avgHappenedTime += row.happenedTime;

        stats.happenedTimeRange.first  = std::min(stats.happenedTimeRange.first, row.timeRange.first);
        stats.happenedTimeRange.second = std::max(stats.happenedTimeRange.second, row.timeRange.second);
    }
}

void saveEvents(const std::vector<Event>& allEvents, const Options& options, const EventStats& stats,
                std::string_view path)
{
    const std::string filename(path);
    const double avgEventPerPixel = stats.allEventHappenedPixelNum > 0
                                        ? static_cast<double>(allEvents.size()) / stats.allEventHappenedPixelNum
                                        : 0.0;
    const double avgHappenedTime =
        !allEvents.empty() ? stats.avgHappenedTime / static_cast<double>(allEvents.size()) : 0.0;

    // build stats text
    std::ostringstream oss;
    oss << "thresholds: " << (*options.thresholds).positive << ", "  //
        << (*options.thresholds).negative                            //
        << " sigma: " << (*options.thresholds).sigma << "\n";
    oss << "all events num : " << allEvents.size() << "\n";
    oss << "average events at the pixel where the event occurred : "  //
        << avgEventPerPixel << "\n";
    oss << "events flooded pixel num : " << stats.allFloodedPixelNum << "\n";
    oss << "average happened time : " << avgHappenedTime << "\n";

    if (allEvents.empty())
    {
        oss << "time range : N/A\n";
    }
    else
    {
        oss << "time range : " << stats.happenedTimeRange.first << ", " << stats.happenedTimeRange.second << "\n";
    }

    const std::string statsStr = oss.str();

    // showing stats
    VLOG(1) << "Detailed event statistics (times normalized within frame intervals):\n" << statsStr;
    LOG(INFO) << "Detected events: " << ProgressBar::formatCount(allEvents.size());
    if (stats.allFloodedPixelNum > 0)
    {
        LOG(WARNING) << "Event storage limit reached in "  //
                     << stats.allFloodedPixelNum
                     << " pixel-frame observations; additional events may not have been recorded";
    }

    // saving stats to text file
    {
        constexpr auto kEventStatsTextFileName = "event_stats.txt";
        const auto txtPath = std::filesystem::path(filename.c_str()).parent_path().append(kEventStatsTextFileName);

        std::ofstream ofs(txtPath.string());
        if (!ofs)
        {
            throw std::runtime_error("failed to open text file!");
        }

        ofs << statsStr;
        ofs.close();

        if (!ofs)
        {
            throw std::runtime_error("failed to write event stats: " + txtPath.string());
        }
    }

    // output to CSV
    LOG(INFO) << "Writing events CSV: " << std::filesystem::absolute(filename).string();

    std::ofstream ofs(filename);
    if (!ofs)
    {
        throw std::runtime_error("failed to open CSV file!");
    }

    ofs << std::setprecision(17);
    for (const auto& ev : allEvents)
    {
        ofs << ev.t << "," << ev.x << "," << ev.y << ", " << ev.p << "\n";
    }

    ofs.close();

    if (!ofs)
    {
        throw std::runtime_error("failed to write events CSV: " + filename);
    }

    VLOG(1) << "Saved events CSV: " << path;
}

void renderMotionRGBFrame(vk2s::Device& device, evr::Scene& scene, const Options& options)
{
    constexpr auto kRGBDirName        = "RGB";
    constexpr auto kImageBaseFileName = "RGB_";
    constexpr auto kTimeFileName      = "RGB_RenderTime.txt";

    const auto zeroNum    = std::to_string(*options.frames).length();
    const auto extent     = vk::Extent3D(*options.width, *options.height, 1);
    const auto format     = (*options.useEXR ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eR8G8B8A8Unorm);
    const size_t size     = checkedImageByteSize(extent, vk2s::Compiler::getSizeOfFormat(format));
    const bool useGBuffer = *options.useEXR && *options.denoise;

    // create RGB directory if not exists
    const auto rgbDir = std::filesystem::path(options.output).append(kRGBDirName);
    if (!std::filesystem::exists(rgbDir))
    {
        std::filesystem::create_directory(rgbDir);
        VLOG(1) << "Created directory: " << rgbDir;
    }

    if (*options.useEXR)
    {
        LOG(INFO) << "RGB output: EXR";
    }
    else
    {
        LOG(INFO) << "RGB output: PNG";
    }

    UniqueHandle<vk2s::Image> outputImage;
    UniqueHandle<vk2s::Image> albedoImage;
    UniqueHandle<vk2s::Image> normalImage;
    {
        const size_t outSize = size;

        vk::ImageCreateInfo ci;
        ci.arrayLayers   = 1;
        ci.extent        = extent;
        ci.format        = format;
        ci.imageType     = vk::ImageType::e2D;
        ci.mipLevels     = 1;
        ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                           vk::ImageUsageFlagBits::eStorage;
        ci.initialLayout = vk::ImageLayout::eUndefined;

        outputImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, outSize,
                                                 vk::ImageAspectFlagBits::eColor);
        ci.format   = vk::Format::eR32G32B32A32Sfloat;
        albedoImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, outSize,
                                                 vk::ImageAspectFlagBits::eColor);
        normalImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, outSize,
                                                 vk::ImageAspectFlagBits::eColor);
    }

    // create staging buffers
    std::array<Handle<vk2s::Buffer>, 2> outputStagingBuffers;
    std::array<Handle<vk2s::Buffer>, 2> albedoStagingBuffers;
    std::array<Handle<vk2s::Buffer>, 2> normalStagingBuffers;

    for (int i = 0; i < 2; ++i)
    {
        const size_t floatSize =
            checkedImageByteSize(extent, vk2s::Compiler::getSizeOfFormat(vk::Format::eR32G32B32A32Sfloat));
        outputStagingBuffers[i] = device.create<vk2s::Buffer>(
            vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst),
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        if (useGBuffer)
        {
            albedoStagingBuffers[i] = device.create<vk2s::Buffer>(
                vk::BufferCreateInfo({}, floatSize, vk::BufferUsageFlagBits::eTransferDst),
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            normalStagingBuffers[i] = device.create<vk2s::Buffer>(
                vk::BufferCreateInfo({}, floatSize, vk::BufferUsageFlagBits::eTransferDst),
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        }
    }

    // transition layouts
    {
        UniqueHandle<vk2s::Command> cmd = device.createUnique<vk2s::Command>();
        cmd->begin(true);
        cmd->transitionImageLayout(outputImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        cmd->transitionImageLayout(albedoImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        cmd->transitionImageLayout(normalImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        cmd->end();
        cmd->executeAndWait();
    }

    // image copy region
    const auto copyRegion = vk::BufferImageCopy()
                                .setBufferOffset(0)
                                .setBufferRowLength(0)
                                .setBufferImageHeight(0)
                                .setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
                                .setImageOffset({ 0, 0, 0 })
                                .setImageExtent(extent);

    // fence
    UniqueHandle<vk2s::Fence> fence = device.createUnique<vk2s::Fence>();

    // build integrator
    evr::MotionPathIntegrator integrator(device, scene, outputImage, albedoImage, normalImage);
    UniqueHandle<vk2s::Command> command = device.createUnique<vk2s::Command>();

    if (useGBuffer)
    {
#ifdef WITH_OIDN
        LOG(INFO) << "RGB denoising: OIDN";
#else
        LOG(WARNING) << "RGB denoising requested, but this build does not include OIDN";
#endif
    }

    // save lambda
    ProgressBar progress(*options.frames, "RGB");
    const auto lmdSaveFrame = [&](const size_t i)
    {
        auto timeStr = std::to_string(i - 1);
        timeStr.insert(0, zeroNum - timeStr.length(), '0');
        const auto file =
            std::filesystem::path(kImageBaseFileName).stem().string() + timeStr + (*options.useEXR ? ".exr" : ".png");
        const auto path = std::filesystem::path(rgbDir).append(file);

        auto& osb = outputStagingBuffers[(i + 1) % 2];
        auto& asb = albedoStagingBuffers[(i + 1) % 2];
        auto& nsb = normalStagingBuffers[(i + 1) % 2];

        if (*options.useEXR)
        {
            saveEXRImage(path.string(), osb, asb, nsb, device, extent, size, useGBuffer);
        }
        else
        {
            saveImage(path.string(), osb, device, extent, size);
        }
    };

    integrator.getParamsRef().spp            = *options.spp;
    integrator.getParamsRef().accumulatedSpp = 0;
    integrator.getParamsRef().useEXR         = *options.useEXR;
    integrator.getParamsRef().uselinLogL     = *options.linLogL;
    integrator.getParamsRef().useMotionBlur  = *options.motion;

    std::chrono::high_resolution_clock::time_point start, end;
    start = std::chrono::high_resolution_clock::now();

    integrator.getParamsRef().generateGBuffer = useGBuffer;
    progress.start();

    for (uint32_t i = 0; i < *options.frames; ++i)
    {
        const double time     = static_cast<double>(i) * *options.deltaTime;
        const double nextTime = static_cast<double>(i + 1) * *options.deltaTime;

        fence->wait();

        integrator.getParamsRef().timeSeed = i;
        integrator.update(time, nextTime, i);
        fence->reset();

        command->begin();

        {  // clear output image
            constexpr auto colorClearValue = vk::ClearValue(std::array{ 0.f, 0.f, 0.f, 1.0f });
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            command->clearImage(outputImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
            if (useGBuffer)
            {
                command->clearImage(albedoImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
                command->clearImage(normalImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
            }
        }

        makeClearedImagesAvailable(command.get());
        integrator.sample(command);

        command->transitionImageLayout(outputImage.get(), vk::ImageLayout::eGeneral,
                                       vk::ImageLayout::eTransferSrcOptimal);
        command->copyImageToBuffer(outputImage.get(), outputStagingBuffers[i % 2].get(), copyRegion);
        command->transitionImageLayout(outputImage.get(), vk::ImageLayout::eTransferSrcOptimal,
                                       vk::ImageLayout::eGeneral);

        if (useGBuffer)
        {
            command->transitionImageLayout(albedoImage.get(), vk::ImageLayout::eGeneral,
                                           vk::ImageLayout::eTransferSrcOptimal);
            command->copyImageToBuffer(albedoImage.get(), albedoStagingBuffers[i % 2].get(), copyRegion);
            command->transitionImageLayout(albedoImage.get(), vk::ImageLayout::eTransferSrcOptimal,
                                           vk::ImageLayout::eGeneral);

            command->transitionImageLayout(normalImage.get(), vk::ImageLayout::eGeneral,
                                           vk::ImageLayout::eTransferSrcOptimal);

            command->copyImageToBuffer(normalImage.get(), normalStagingBuffers[i % 2].get(), copyRegion);

            command->transitionImageLayout(normalImage.get(), vk::ImageLayout::eTransferSrcOptimal,
                                           vk::ImageLayout::eGeneral);
        }

        command->end();
        command->execute(fence);

        // output previous frame
        if (i > 0)
        {
            lmdSaveFrame(i);
            progress.step();
        }
    }

    // output final frame
    {
        fence->wait();

        lmdSaveFrame(*options.frames);
        progress.step();

        fence->reset();
    }

    end = std::chrono::high_resolution_clock::now();

    const double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LOG(INFO) << "RGB frame processing completed in "  //
              << elapsed / 1e3                         //
              << " s (includes readback and image saving)";

    // save rendering time to txt
    {
        const auto path = std::filesystem::path(options.output).append(kTimeFileName);
        // for end time output
        const auto durationSinceEpoch =
            std::chrono::duration_cast<std::chrono::system_clock::duration>(end.time_since_epoch());
        const std::chrono::system_clock::time_point systemTp(durationSinceEpoch);
        const auto time = std::chrono::system_clock::to_time_t(systemTp);
        const auto tm   = *std::localtime(&time);

        std::ofstream ofs(path.string());
        if (!ofs)
        {
            throw std::runtime_error("failed to open text file!");
        }

        ofs << "elapsed time: " << elapsed / 1e3 << " [sec]\n";
        ofs << "finished rendering at: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        ofs.close();
    }

    LOG(INFO) << "RGB images saved: " << std::filesystem::absolute(rgbDir).string();
    device.destroy(outputImage);
    for (int i = 0; i < outputStagingBuffers.size(); ++i)
    {
        device.destroy(outputStagingBuffers[i]);
        if (useGBuffer)
        {
            device.destroy(albedoStagingBuffers[i]);
            device.destroy(normalStagingBuffers[i]);
        }
    }
}

static void logEventSearchStats(const uint32_t frameIndex,
                                const std::vector<evr::EfficientEventIntegrator::EventSearchStats>& allStats)
{
    using Stats = evr::EfficientEventIntegrator::EventSearchStats;

    std::vector<Stats> active;
    active.reserve(allStats.size());

    uint64_t detected  = 0;
    uint64_t noEvent   = 0;
    uint64_t stackFull = 0;

    for (const auto& s : allStats)
    {
        if (s.status == 0)
        {
            continue;
        }

        active.push_back(s);
        detected += s.status == 1;
        noEvent += s.status == 2;
        stackFull += s.status == 3;
    }

    if (active.empty())
    {
        VLOG(1) << "Frame " << frameIndex << " event-0 search: processed=0";
        return;
    }

    const auto describe = [&](uint32_t Stats::* field)
    {
        std::vector<uint32_t> values;
        values.reserve(active.size());

        uint64_t total = 0;
        for (const auto& s : active)
        {
            const uint32_t value = s.*field;
            values.push_back(value);
            total += value;
        }

        std::sort(values.begin(), values.end());

        // Nearest-rank percentiles over processed candidates only.
        const auto percentile = [&](const size_t p)
        {
            const size_t rank = (values.size() * p + 99) / 100;
            return values[rank - 1];
        };

        const size_t topCount = (values.size() + 99) / 100;
        uint64_t topTotal     = 0;
        for (size_t j = 0; j < topCount; ++j)
        {
            topTotal += values[values.size() - 1 - j];
        }

        const double topShare = total > 0 ? 100.0 * double(topTotal) / double(total) : 0.0;

        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << "total=" << total
            << ", mean=" << double(total) / double(values.size()) << ", p50=" << percentile(50)
            << ", p95=" << percentile(95) << ", p99=" << percentile(99) << ", max=" << values.back()
            << ", top1%-share=" << topShare << "%";
        return out.str();
    };

    VLOG(1) << "Frame " << frameIndex << " event-0 search: processed=" << active.size() << ", detected=" << detected
            << ", no-event=" << noEvent << ", stack-full=" << stackFull;

    VLOG(1) << "Frame " << frameIndex << " event-0 visited-ranges: " << describe(&Stats::visitedRanges);
    VLOG(1) << "Frame " << frameIndex << " event-0 luminance-evaluations: " << describe(&Stats::luminanceEvaluations);
    VLOG(1) << "Frame " << frameIndex << " event-0 path-samples: " << describe(&Stats::pathSamples);
}

void renderEvents(vk2s::Device& device, evr::Scene& scene, const Options& options)
{
    constexpr uint32_t kEventTexDepth = 2;

    constexpr auto kEventsDirName = "events";

    const auto zeroNum     = std::to_string(*options.frames).length();
    const auto extent      = vk::Extent3D(*options.width, *options.height, kEventTexDepth);
    const auto eventFormat = vk::Format::eR32G32B32A32Uint;
    const size_t eventSize = checkedImageByteSize(extent, vk2s::Compiler::getSizeOfFormat(eventFormat));

    // create events directory if not exist
    const auto eventsDir = std::filesystem::path(options.output).append(kEventsDirName);
    if (!std::filesystem::exists(eventsDir))
    {
        std::filesystem::create_directories(eventsDir);
        VLOG(1) << "Created directory: " << eventsDir;
    }

    std::vector<Event> events;
    EventStats stats;

    UniqueHandle<vk2s::Image> eventOutputImage;
    {
        vk::ImageCreateInfo ci;
        ci.arrayLayers   = 1;
        ci.extent        = extent;
        ci.format        = eventFormat;
        ci.imageType     = vk::ImageType::e3D;
        ci.mipLevels     = 1;
        ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                           vk::ImageUsageFlagBits::eStorage;
        ci.initialLayout = vk::ImageLayout::eUndefined;

        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        eventOutputImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, eventSize,
                                                      vk::ImageViewType::e3D, range);
    }

    std::array<Handle<vk2s::Buffer>, 2> stagingBuffers;
    for (auto& stagingBuffer : stagingBuffers)
    {
        stagingBuffer = device.create<vk2s::Buffer>(
            vk::BufferCreateInfo({}, eventSize, vk::BufferUsageFlagBits::eTransferDst),
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    const auto copyRegion = vk::BufferImageCopy()
                                .setBufferOffset(0)
                                .setBufferRowLength(0)
                                .setBufferImageHeight(0)
                                .setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
                                .setImageOffset({ 0, 0, 0 })
                                .setImageExtent(extent);

    {
        UniqueHandle<vk2s::Command> cmd = device.createUnique<vk2s::Command>();
        cmd->begin(true);
        cmd->transitionImageLayout(eventOutputImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        cmd->end();
        cmd->executeAndWait();
    }

    // image copy region
    UniqueHandle<vk2s::Fence> fence     = device.createUnique<vk2s::Fence>();
    UniqueHandle<vk2s::Command> command = device.createUnique<vk2s::Command>();
    const double duration               = *options.deltaTime * static_cast<double>(*options.frames);
    ProgressBar progress(*options.frames, "Events", true);
    std::chrono::high_resolution_clock::time_point start, end;

    const auto recordEventsAndSaveFrame = [&](const size_t i, const double time)
    {
        auto timeStr = std::to_string(i - 1);
        timeStr.insert(0, zeroNum - timeStr.length(), '0');

        recordEvents(stagingBuffers[(i + 1) % 2], time, options, extent, eventSize, events, stats, device);
        if (*options.eventDebug)
        {
            constexpr auto kDebugImageFileName = "debug_image.exr";

            const auto file = std::filesystem::path(kDebugImageFileName).stem().string() + "_" + timeStr + ".exr";
            const auto path = std::filesystem::path(eventsDir).append(file);
            saveEXRImageEventDebug(path.string(), stagingBuffers[(i + 1) % 2], device, extent, eventSize);
        }
        else
        {
            constexpr auto kImageFileName = "events.png";

            const auto file = std::filesystem::path(kImageFileName).stem().string() + "_" + timeStr + ".png";
            const auto path = std::filesystem::path(eventsDir).append(file);
            saveEventImage(path.string(), stagingBuffers[(i + 1) % 2], device, extent, eventSize);
        }
    };

    evr::EfficientEventIntegrator* efficientIntegrator = nullptr;
    std::unique_ptr<evr::MotionIntegrator> integrator;
    if (*options.optimize)
    {
        LOG(INFO) << "Event engine: efficient";
        auto efficient = std::make_unique<evr::EfficientEventIntegrator>(device, scene, eventOutputImage);
        efficient->setSearchStatsEnabled(options.verbose);
        efficientIntegrator = efficient.get();
        integrator          = std::move(efficient);
    }
    else
    {
        if (*options.discrete)
        {
            LOG(INFO) << "Event engine: discrete";
        }
        else
        {
            LOG(INFO) << "Event engine: baseline continuous";
        }
        integrator = std::make_unique<evr::EventIntegrator>(device, scene, eventOutputImage);
    }

    integrator->getParamsRef().spp        = *options.spp;
    integrator->getParamsRef().thresholds = *options.thresholds;
    integrator->getParamsRef().discrete   = *options.discrete;

    if (*options.scOnly)
    {
        integrator->getParamsRef().streamCompaction = true;
        integrator->getParamsRef().test             = false;
    }

    if (*options.testOnly)
    {
        integrator->getParamsRef().streamCompaction = false;
        integrator->getParamsRef().test             = true;
    }

    if (options.eventBatchSize > 0)
    {
        if (!efficientIntegrator)
        {
            throw std::invalid_argument("--event-batch-size requires the efficient event engine");
        }

        if (!integrator->getParamsRef().streamCompaction)
        {
            throw std::invalid_argument("--event-batch-size requires stream compaction");
        }
    }

    std::optional<evr::MotionIntegrator::GpuTiming> referenceGpuTiming;
    std::vector<std::pair<uint32_t, evr::MotionIntegrator::GpuTiming>> frameGpuTimings;

    start = std::chrono::high_resolution_clock::now();

    // pool initialize
    if (*options.optimize)
    {
        LOG(INFO) << "Event acceleration: Welch test "                             //
                  << (integrator->getParamsRef().test ? "on" : "off")              //
                  << ", stream compaction "                                        //
                  << (integrator->getParamsRef().streamCompaction ? "on" : "off")  //
                  << ", batch size " << options.eventBatchSize;                    //
    }
    const auto initializationStart = std::chrono::steady_clock::now();
    LOG(INFO) << "Initializing event reference luminance...";
    {
        integrator->getParamsRef().sampleInitialLuminances = true;

        fence->wait();
        integrator->update(0, 0, 0);
        fence->reset();
        command->begin();

        {  // clear output image
            constexpr auto colorClearValue = vk::ClearValue(std::array{ 0, 0, 0, 0 });
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            command->clearImage(eventOutputImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
        }

        makeClearedImagesAvailable(command.get());
        integrator->sample(command);
        makeEventStateAvailable(command.get());
        command->end();
        command->execute(fence);

        integrator->getParamsRef().sampleInitialLuminances = false;
    }
    fence->wait();

    LOG(INFO) << "Event reference initialized in " << elapsedSeconds(initializationStart);

    referenceGpuTiming = integrator->consumeGpuTiming();
    if (referenceGpuTiming)
    {
        LOG(INFO) << "Event reference GPU time: "  //
                  << referenceGpuTiming->primaryRayTracingMs << " ms";
    }

    LOG(INFO) << "Processing event frames...";
    progress.start();
    for (uint32_t i = 0; i < *options.frames; ++i)
    {
        const double time     = static_cast<double>(i) * *options.deltaTime;
        const double nextTime = static_cast<double>(i + 1) * *options.deltaTime;

        integrator->update(time, nextTime, i);
        fence->reset();

        command->begin();

        {  // clear output image
            constexpr auto colorClearValue = vk::ClearValue(std::array{ 0, 0, 0, 0 });
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            command->clearImage(eventOutputImage.get(), vk::ImageLayout::eGeneral, colorClearValue, range);
        }

        makeClearedImagesAvailable(command.get());

        if (options.eventBatchSize > 0)
        {
            efficientIntegrator->sampleBatched(command, options.eventBatchSize);
        }
        else
        {
            integrator->sample(command);
        }

        makeEventStateAvailable(command.get());

        command->transitionImageLayout(eventOutputImage.get(), vk::ImageLayout::eGeneral,
                                       vk::ImageLayout::eTransferSrcOptimal);
        command->copyImageToBuffer(eventOutputImage.get(), stagingBuffers[i % 2].get(), copyRegion);
        command->transitionImageLayout(eventOutputImage.get(), vk::ImageLayout::eTransferSrcOptimal,
                                       vk::ImageLayout::eGeneral);

        command->end();
        command->execute(fence);

        if (i > 0)
        {
            const double prevTime = static_cast<double>(i - 1) * *options.deltaTime;
            recordEventsAndSaveFrame(i, prevTime);
            progress.step(events.size());
        }

        if (!fence->wait())
        {
            throw std::runtime_error("failed to wait for event frame");
        }

        if (auto timing = integrator->consumeGpuTiming())
        {
            VLOG(1) << "Frame " << i                                           //
                    << " GPU: primary=" << timing->primaryRayTracingMs         //
                    << " ms, compaction=" << timing->streamCompactionMs        //
                    << " ms, event-ray-tracing=" << timing->eventRayTracingMs  //
                    << " ms, total=" << timing->totalMs << " ms";

            std::ostringstream candidateCounts;
            std::ostringstream rayTracingTimes;
            std::ostringstream rayTracingCostPerCandidate;

            rayTracingTimes << std::fixed << std::setprecision(3);
            rayTracingCostPerCandidate << std::fixed << std::setprecision(3);

            uint64_t totalCandidatePixels = 0;

            for (size_t eventID = 0; eventID < timing->candidatePixelsPerEvent.size(); ++eventID)
            {
                if (eventID > 0)
                {
                    candidateCounts << ", ";
                    rayTracingTimes << ", ";
                    rayTracingCostPerCandidate << ", ";
                }

                const uint32_t count = timing->candidatePixelsPerEvent[eventID];
                const double timeMs  = timing->rayTracingPerEventMs[eventID];

                candidateCounts << count;
                rayTracingTimes << timeMs;

                if (count > 0)
                {
                    const double usPerCandidate = timeMs * 1.0e3 / static_cast<double>(count);
                    rayTracingCostPerCandidate << usPerCandidate;
                }
                else
                {
                    rayTracingCostPerCandidate << "n/a";
                }

                totalCandidatePixels += count;
            }

            VLOG(1) << "Frame " << i                //
                    << " candidate pixels: total="  //
                    << totalCandidatePixels         //
                    << ", per-event["               //
                    << candidateCounts.str()        //
                    << "]";

            VLOG(1) << "Frame " << i                        //
                    << " event ray tracing: per-event-ms["  //
                    << rayTracingTimes.str()                //
                    << "], per-candidate-us["               //
                    << rayTracingCostPerCandidate.str()     //
                    << "]";

            frameGpuTimings.emplace_back(i, std::move(*timing));
        }

        // The existing fence wait above completes the statistics copy as well.
        // Do not tie readback to availability of GPU timestamp queries.
        if (efficientIntegrator && options.verbose)
        {
            const auto searchStats = efficientIntegrator->consumeEventSearchStats();
            logEventSearchStats(static_cast<uint32_t>(i), searchStats);
        }
    }

    // save final frame
    {
        recordEventsAndSaveFrame(*options.frames, duration - *options.deltaTime);
        progress.step(events.size());
        fence->reset();
    }

    end                  = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LOG(INFO) << "Event initialization and frame processing completed in "  //
              << elapsed / 1e3                                              //
              << " s (includes readback, decoding, and image saving)";

    // save rendering time to txt
    {
        constexpr auto kTimeFileName = "event_render_time.txt";

        const auto path = std::filesystem::path(options.output).append(kTimeFileName);
        std::ofstream ofs(path.string());
        if (!ofs)
        {
            throw std::runtime_error("failed to open text file!");
        }

        ofs << "elapsed time: " << elapsed / 1e3 << " [sec]\n";
        ofs.close();
    }

    if (referenceGpuTiming || !frameGpuTimings.empty())
    {
        constexpr auto kGpuTimeFileName = "event_gpu_time.csv";

        const auto path = std::filesystem::path(options.output).append(kGpuTimeFileName);

        std::ofstream ofs(path.string());
        if (!ofs)
        {
            throw std::runtime_error("failed to GPU timing file");
        }

        ofs.imbue(std::locale::classic());
        ofs << std::fixed << std::setprecision(6);
        ofs << "frame,phase,event_id,time_ms,candidate_pixels\n";

        if (referenceGpuTiming)
        {
            ofs << "-1,reference,-1," << referenceGpuTiming->primaryRayTracingMs << ",\n";
        }

        for (const auto& [frameIndex, timing] : frameGpuTimings)
        {
            ofs << frameIndex << ",primary,-1," << timing.primaryRayTracingMs << ",\n";

            for (size_t eventID = 0; eventID < timing.rayTracingPerEventMs.size(); ++eventID)
            {
                ofs << frameIndex << ",compaction," << eventID << ','  //
                    << timing.compactionPerEventMs[eventID] << ",\n";

                ofs << frameIndex << ",event_ray_tracing," << eventID << ','  //
                    << timing.rayTracingPerEventMs[eventID] << "," << timing.candidatePixelsPerEvent[eventID] << "\n";
            }

            ofs << frameIndex << ",total,-1," << timing.totalMs << ",\n";
        }
    }

    // save events
    {
        // sort events
        const auto sortingStart = std::chrono::steady_clock::now();
        LOG(INFO) << "Sorting " << ProgressBar::formatCount(events.size()) << " events by timestamp...";
        std::sort(events.begin(), events.end());

        LOG(INFO) << "Event sorting completed in " << elapsedSeconds(sortingStart);

        const auto savingStart = std::chrono::steady_clock::now();

        constexpr auto kCSVFileName = "events.csv";
        const auto path             = std::filesystem::path(eventsDir).append(kCSVFileName);

        saveEvents(events, options, stats, path.string());

        LOG(INFO) << "Event files saved in " << elapsedSeconds(savingStart);
    }

    LOG(INFO) << "Event output saved: " << std::filesystem::absolute(eventsDir).string();

    device.destroy(eventOutputImage);
    for (auto& stagingBuffer : stagingBuffers)
    {
        device.destroy(stagingBuffer);
    }
}

void initOidnFilter()
{
#if defined(WITH_OIDN)
    oidnDevice = oidn::newDevice(oidn::DeviceType::CPU);
    oidnDevice.commit();

    oidnFilter = oidnDevice.newFilter("RT");
#endif
}

int main(int argc, char** argv)
{
    ConsoleLogging logging(argv[0]);
    const auto applicationStart = std::chrono::steady_clock::now();

    try
    {
        Options options;
        const auto result = options.parse(argc, argv);
        if (result)
        {
            std::cout << *result;
            return EXIT_SUCCESS;
        }
        logging.configure(options.verbose);
        ConsoleOutput::instance().configure(
            options.progress != Options::ProgressMode::eOff,
            options.progress == Options::ProgressMode::eOn ||
                (options.progress == Options::ProgressMode::eAuto && ConsoleOutput::isTerminal()));

#ifdef _OPENMP
        const uint32_t maxThreads = omp_get_max_threads();
        VLOG(1) << "OpenMP threads: " << maxThreads;
        omp_set_num_threads(maxThreads);
#else
        VLOG(1) << "OpenMP disabled";
#endif

        LOG(INFO) << "Scene configuration: " << std::filesystem::absolute(options.input).string();
        LOG(INFO) << "Initializing rendering device...";
        initOidnFilter();

        vk2s::Device::Extensions ext{ .useRayTracingExt = true, .useNVMotionBlurExt = true };
        vk2s::Device device(ext, false);
        LOG(INFO) << "GPU: " << device.getPhysicalDeviceName();

        const auto sceneStart = std::chrono::steady_clock::now();
        LOG(INFO) << "Loading scene and building acceleration structures...";
        auto sceneRes = evr::buildScene(device, options);
        if (!sceneRes)
        {
            throw std::runtime_error("failed to build scene!");
        }

        evr::Scene& scene = *sceneRes;
        LOG(INFO) << "Scene ready in " << elapsedSeconds(sceneStart);
        const char* mode = *options.mode == Options::Mode::eRGB      ? "rgb"
                           : *options.mode == Options::Mode::eEvents ? "events"
                                                                     : "full";
        LOG(INFO) << "Mode: " << mode;
        LOG(INFO) << "Film: " << *options.width      //
                  << 'x' << *options.height << ", "  //
                  << *options.frames << " frames, "  //
                  << *options.spp << " spp, "        //
                  << "dt=" << *options.deltaTime / 1000.0 << " ms";

        if (*options.mode != Options::Mode::eRGB)
        {
            LOG(INFO) << "Thresholds: positive=" << options.thresholds->positive  //
                      << ", negative=" << options.thresholds->negative            //
                      << ", sigma=" << options.thresholds->sigma;
        }
        if (*options.mode != Options::Mode::eEvents)
        {
            LOG(INFO) << "RGB settings: lin-log "           //
                      << (*options.linLogL ? "on" : "off")  //
                      << ", motion blur " << (*options.motion ? "on" : "off");
        }
        LOG(INFO) << "Output directory: " << std::filesystem::absolute(options.output).string();

        // create result directory if not exist
        if (!std::filesystem::exists(options.output))
        {
            std::filesystem::create_directories(options.output);
            VLOG(1) << "Created directory: " << options.output;
        }

        // rendering depends on the mode
        switch (*options.mode)
        {
            using enum Options::Mode;

        case eEvents:
            renderEvents(device, scene, options);
            break;
        case eRGB:
            LOG(INFO) << "Processing RGB frames...";
            renderMotionRGBFrame(device, scene, options);
            break;
        case eFull:
            LOG(INFO) << "Processing RGB frames...";
            renderMotionRGBFrame(device, scene, options);
            renderEvents(device, scene, options);
            break;
        default:
            throw std::runtime_error("invalid rendering mode!");
        }
        LOG(INFO) << "Completed in " << elapsedSeconds(applicationStart);
        LOG(INFO) << "Output: " << std::filesystem::absolute(options.output).string();
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "Rendering failed: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
