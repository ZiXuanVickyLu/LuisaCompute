//
// Created by Mike Smith on 2021/9/3.
//

#include <iostream>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

template<typename T>
struct Pool {
    int a;
};

template<typename T>
struct Allocator {
    Pool<T> pool;
    using value_type = T;
    using size_type = size_t;
    using pointer = T *;
    using const_pointer = const T *;

    template<typename U>
    struct rebind {
        using other = Allocator<U>;
    };
//    using propagate_on_container_copy_assignment = std::true_type;
//    using propagate_on_container_move_assignment = std::true_type;
//    using propagate_on_container_swap = std::true_type;
    [[nodiscard]] static auto type_name() noexcept {
        return std::string{"Allocator<"}.append(typeid(T).name()).append(">");
    }
    Allocator() noexcept {
        std::cout << type_name() << "()" << std::endl;
    }
//    Allocator(Allocator &&another) noexcept : pool{std::move(another.pool)} {
//        std::cout << type_name() << "(Allocator &&)" << std::endl;
//    }
    Allocator(const Allocator &another) noexcept : pool{another.pool} {
        std::cout << type_name() << "(const Allocator &)" << std::endl;
    }
    ~Allocator() noexcept {
        std::cout << "~" << type_name() << std::endl;
    }
    [[nodiscard]] auto allocate(std::size_t n) const noexcept {
        auto p = malloc(n * sizeof(T));
        std::cout << type_name() << "::allocate(" << n << ") -> " << p << std::endl;
        return static_cast<T *>(p);
    }
    void deallocate(T *p, size_t n) const noexcept {
        std::cout << type_name() << "::deallocate(" << static_cast<void *>(p) << ", " << n << ")" << std::endl;
        free(p);
    }
    template<typename R>
    explicit Allocator(const Allocator<R> &) noexcept {
        std::cout << type_name() << "(const " << Allocator<R>::type_name() << " &)" << std::endl;
    }
};

int main() {
    using Alloc = Allocator<std::pair<const int, int>>;
    std::map<int, int, std::less<>, Alloc> map;
    std::cout << "after constructor..." << std::endl;
}
