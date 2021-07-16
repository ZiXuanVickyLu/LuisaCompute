//
// Created by Mike Smith on 2021/7/4.
//

#pragma once

#include <core/basic_types.h>
#include <ast/function_builder.h>
#include <runtime/device.h>
#include <runtime/texture_heap.h>

namespace luisa::compute {

namespace detail {

template<typename T>
struct prototype_to_shader_invocation {
    using type = T;
};

template<>
struct prototype_to_shader_invocation<TextureHeap> {
    using type = const TextureHeap &;
};

template<typename T>
struct prototype_to_shader_invocation<Buffer<T>> {
    using type = BufferView<T>;
};

template<typename T>
struct prototype_to_shader_invocation<Image<T>> {
    using type = ImageView<T>;
};

template<typename T>
struct prototype_to_shader_invocation<Volume<T>> {
    using type = VolumeView<T>;
};

template<typename T>
using prototype_to_shader_invocation_t = typename prototype_to_shader_invocation<T>::type;

class ShaderInvokeBase {

private:
    CommandHandle _command;
    Function _kernel;
    size_t _argument_index{0u};

private:
    [[nodiscard]] auto _dispatch_command() noexcept {
        return static_cast<ShaderDispatchCommand *>(_command.get());
    }

public:
    explicit ShaderInvokeBase(uint64_t handle, Function kernel) noexcept
        : _command{ShaderDispatchCommand::create(handle, kernel)},
          _kernel{kernel} {

        for (auto buffer : _kernel.captured_buffers()) {
            _dispatch_command()->encode_buffer(
                buffer.variable.uid(), buffer.handle, buffer.offset_bytes,
                _kernel.variable_usage(buffer.variable.uid()));
        }

        for (auto texture : _kernel.captured_textures()) {
            _dispatch_command()->encode_texture(
                texture.variable.uid(), texture.handle,
                _kernel.variable_usage(texture.variable.uid()));
        }

        for (auto heap : _kernel.captured_texture_heaps()) {
            _dispatch_command()->encode_texture_heap(
                heap.variable.uid(), heap.handle);
        }
    }

    template<typename T>
    ShaderInvokeBase &operator<<(BufferView<T> buffer) noexcept {
        auto variable_uid = _kernel.arguments()[_argument_index++].uid();
        auto usage = _kernel.variable_usage(variable_uid);
        _dispatch_command()->encode_buffer(
            variable_uid, buffer.handle(), buffer.offset_bytes(), usage);
        return *this;
    }

    template<typename T>
    ShaderInvokeBase &operator<<(ImageView<T> image) noexcept {
        auto variable_uid = _kernel.arguments()[_argument_index++].uid();
        auto usage = _kernel.variable_usage(variable_uid);
        _dispatch_command()->encode_texture(variable_uid, image.handle(), usage);
        return *this << image.offset();
    }

    template<typename T>
    ShaderInvokeBase &operator<<(VolumeView<T> volume) noexcept {
        auto variable_uid = _kernel.arguments()[_argument_index++].uid();
        auto usage = _kernel.variable_usage(variable_uid);
        _dispatch_command()->encode_texture(variable_uid, volume.handle(), usage);
        return *this << volume.offset();
    }

    template<typename T>
    ShaderInvokeBase &operator<<(T data) noexcept {
        auto variable_uid = _kernel.arguments()[_argument_index++].uid();
        _dispatch_command()->encode_uniform(variable_uid, &data, sizeof(T), alignof(T));
        return *this;
    }

    ShaderInvokeBase &operator<<(const TextureHeap &heap) noexcept {
        auto v = _kernel.arguments()[_argument_index++].uid();
        _dispatch_command()->encode_texture_heap(v, heap.handle());
        return *this;
    }

protected:
    [[nodiscard]] auto _parallelize(uint3 dispatch_size) noexcept {
        _dispatch_command()->set_dispatch_size(dispatch_size);
        CommandHandle command{nullptr};
        command.swap(_command);
        return command;
    }
};

template<size_t dim>
struct ShaderInvoke {
    static_assert(always_false_v<std::index_sequence<dim>>);
};

template<>
struct ShaderInvoke<1> : public ShaderInvokeBase {
    explicit ShaderInvoke(uint64_t handle, Function kernel) noexcept : ShaderInvokeBase{handle, kernel} {}
    [[nodiscard]] auto dispatch(uint size_x) noexcept {
        return _parallelize(uint3{size_x, 1u, 1u});
    }
};

template<>
struct ShaderInvoke<2> : public ShaderInvokeBase {
    explicit ShaderInvoke(uint64_t handle, Function kernel) noexcept : ShaderInvokeBase{handle, kernel} {}
    [[nodiscard]] auto dispatch(uint size_x, uint size_y) noexcept {
        return _parallelize(uint3{size_x, size_y, 1u});
    }
    [[nodiscard]] auto dispatch(uint2 size) noexcept {
        return dispatch(size.x, size.y);
    }
};

template<>
struct ShaderInvoke<3> : public ShaderInvokeBase {
    explicit ShaderInvoke(uint64_t handle, Function kernel) noexcept : ShaderInvokeBase{handle, kernel} {}
    [[nodiscard]] auto dispatch(uint size_x, uint size_y, uint size_z) noexcept {
        return _parallelize(uint3{size_x, size_y, size_z});
    }
    [[nodiscard]] auto dispatch(uint3 size) noexcept {
        return dispatch(size.x, size.y, size.z);
    }
};

}// namespace detail

template<size_t dimension, typename... Args>
class Shader : concepts::Noncopyable {

    static_assert(dimension == 1u || dimension == 2u || dimension == 3u);

private:
    Device::Handle _device;
    uint64_t _handle{};
    std::shared_ptr<const detail::FunctionBuilder> _kernel;

private:
    friend class Device;
    Shader(Device::Handle device, std::shared_ptr<const detail::FunctionBuilder> kernel) noexcept
        : _device{std::move(device)},
          _handle{_device->create_shader(kernel.get())},
          _kernel{std::move(kernel)} {}

    void _destroy() noexcept {
        if (*this) { _device->destroy_shader(_handle); }
    }

public:
    Shader() noexcept = default;
    ~Shader() noexcept { _destroy(); }

    Shader(Shader &&another) noexcept
        : _device{std::move(another._device)},
          _handle{another._handle},
          _kernel{std::move(another._kernel)} {}

    Shader &operator=(Shader &&rhs) noexcept {
        if (this != &rhs) {
            _destroy();
            _device = std::move(rhs._device);
            _handle = rhs._handle;
            _kernel = std::move(rhs._kernel);
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return _device != nullptr; }
    [[nodiscard]] auto operator()(detail::prototype_to_shader_invocation_t<Args>... args) const noexcept {
        detail::ShaderInvoke<dimension> invoke{_handle, _kernel.get()};
        (invoke << ... << args);
        return invoke;
    }
};

template<typename ...Args>
using Shader1D = Shader<1, Args...>;

template<typename ...Args>
using Shader2D = Shader<2, Args...>;

template<typename ...Args>
using Shader3D = Shader<3, Args...>;

}// namespace luisa::compute
