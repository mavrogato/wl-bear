
#include <iostream>
#include <memory>
#include <filesystem>
#include <span>
#include <map>
#include <list>
#include <charconv>
#include <source_location>
#include <string_view>

#include <cstdlib>

#include <ffi.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <aux/io.hh>
#include <wayland-client-protocol.h>

namespace wayland
{
    struct interface;
    struct display;

    struct message {
        char const* name;
        char const* signature;
        interface const** types;
    };
    struct interface {
        char const* name;
        int version;
        std::span<message const*> methods;
        std::span<message const*> events;
    };
    struct object {
        interface const* interface;
        void (* const *implementation)(void);
        uint32_t id;
    };
    struct proxy {
        object object;
        display* display;
        void* user_data;
    };
    struct buffer {
        char data[4096];
        int head, tail;
    };
    struct closure {
        int count;
        message const* message;
        ffi_type* types[20];
        ffi_cif cif;
        void* args[20];
        uint32_t* start;
        uint32_t buffer[0];
    };

    struct connection {
        typedef int (*update_function)(connection*, uint32_t, void*);

    public:
        buffer in;
        buffer out;
        buffer fds_in;
        buffer fds_out;
        int fd;
        void* data;
        update_function update;
        closure receive_closure;
        closure send_closure;
        int write_signalled;
    };

    struct display {
        typedef int (*update_function)(uint32_t, void*);
        typedef int (*global_function)(display*, uint32_t, char const*, uint32_t, void*);

    public:
        static inline bool debug = false;

    public:
        display(char const* /*name*/ = "") {
            if (getenv("WAYLAND_DEBUG")) {
                debug = true;
            }
            if (auto tmp = std::getenv("WAYLAND_SOCKET")) {
                char* end;
                this->fd = strtol(tmp, &end, 0);
                if (*end != '\0') {
                    throw std::source_location::current();
                }
            }

        }
    public:
        proxy proxy;
        connection* connection;
        int fd;
        uint32_t mask;
        std::map<uint32_t, object> objects;
        std::list<object> global_listener_list;
        std::list<object> global_list;
        update_function update;
        void* update_data;
        global_function global_handler;
        void* global_handler_data;
    };
} // namespace wayland

struct application_error : public std::runtime_error {
public:
    application_error(char const* msg = "",
                      std::source_location loc = std::source_location::current()) noexcept
        : std::runtime_error{msg}
        , loc{loc}
        {
        }

    virtual std::ostream& print(std::ostream& output) const noexcept {
        return output << loc.file_name() << ':'
                      << loc.line() << ':'
                      << loc.column() << ':'
                      << loc.function_name() << ':'
                      << std::runtime_error::what();
    }

private:
    std::source_location loc;
};

inline auto& operator<<(std::ostream& output, application_error const& err) {
    return err.print(output);
}

template <size_t N>
void assign(char (&dst)[N], std::string_view src) {
    if (N <= src.size()) {
        throw application_error("Cannot assign to fixed array.");
    }
    size_t i;
    for (i = 0; i < src.size(); ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

#include <wayland-client.h>

int main() {
    // if (auto display = wl_display_connect(nullptr)) {
    //     if (auto registry = wl_display_get_registry(display)) {
    //         wl_registry_destroy(registry);
    //     }
    //     wl_display_disconnect(display);
    // }
    
    try {
        if (std::filesystem::path path = std::getenv("XDG_RUNTIME_DIR"); !path.empty()) {
            if (std::filesystem::path name = std::getenv("WAYLAND_DISPLAY"); !name.empty()) {
                path /= name;
            }
            else {
                path /= "wayland-0";
            }
            if (auto fd = aux::unique_fd{::socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0)}) {
                sockaddr_un addr{
                    .sun_family = AF_LOCAL,
                };
                assign(addr.sun_path, path.native());
                if (auto ret = ::connect(fd,
                                         reinterpret_cast<sockaddr const*>(&addr),
                                         sizeof (addr));
                    ret < 0)
                {
                    throw application_error("Failed to connect");
                }
                
            }
        }
        return 0;
    }
    catch (application_error& ex) {
        std::cout << ex << std::endl;
    }
}
