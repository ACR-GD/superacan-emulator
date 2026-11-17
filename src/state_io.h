#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <type_traits>

// Minimal symmetric binary (de)serializer: the same serialize() code saves and loads.
struct StateIO {
    bool loading = false;
    bool ok = true;
    std::vector<uint8_t> buf;
    size_t pos = 0;

    void bytes(void* p, size_t n) {
        if (loading) {
            if (!ok || pos + n > buf.size()) { ok = false; return; }
            std::memcpy(p, buf.data() + pos, n);
            pos += n;
        } else {
            const uint8_t* c = static_cast<const uint8_t*>(p);
            buf.insert(buf.end(), c, c + n);
        }
    }
    template <class T> void pod(T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod() requires trivially copyable data");
        bytes(&v, sizeof(T));
    }
};
