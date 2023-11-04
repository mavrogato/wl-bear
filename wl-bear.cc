
#include <cstdlib>
#include <string_view>
#include <optional>
#include <charconv>

namespace aux
{
    inline std::string_view getenv(char const* key) noexcept {
        if (auto raw_value = std::getenv(key)) {
            return raw_value;
        }
        return ""; // for std::string_view from nullptr not allowed.
    }

    template <class T>
    std::optional<T> from_getenv(char const* key) noexcept {
        if (auto value = aux::getenv(key); !value.empty()) {
            T tmp;
            if (auto [p, e] = std::from_chars(value.begin(), value.end(), tmp); e == std::errc{}) {
                return tmp;
            }
        }
        return std::nullopt;
    }

} // ::aux

/////////////////////////////////////////////////////////////////////////////
#include <iosfwd>
#include <stdexcept>
#include <source_location>
#include <array>
#include <spanstream>
#include <tuple>

namespace aux
{
    template <class Ch, class Tr>
    decltype (auto) operator<<(std::basic_ostream<Ch, Tr>& output, std::source_location const& loc) {
        return output << loc.file_name() << ':'
                      << loc.line() << ':'
                      << loc.column() << ':'
                      << loc.function_name();
        return output;
    }

    struct exception : public std::domain_error {
    public:
        exception(char const* msg,
                  std::source_location loc = std::source_location::current()) noexcept
            : std::domain_error{msg}
            , loc{loc}
            {
            }

    public:
        auto const& location() const noexcept { return this->loc; }

    private:
        std::source_location loc;
    };

    template <class Ch, class Tr>
    decltype (auto) operator<<(std::basic_ostream<Ch, Tr>& output, exception const& ex) {
        return output << ex.location() << ':' << ex.what();
    }

} // ::aux


/////////////////////////////////////////////////////////////////////////////
#include <cstdlib>

#include <charconv>
#include <optional>
#include <filesystem>

#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <aux/io.hh>

#include <iostream>
namespace wayland
{
    auto error = [](auto...) noexcept {
    };

    void what(auto...) noexcept {
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    struct display {
        aux::unique_fd fd;
    };

    std::optional<display> display_connect(std::optional<std::filesystem::path> name = std::nullopt) {
        aux::unique_fd fd;
        if (auto raw_fd = aux::from_getenv<int>("WAYLAND_SOCKET")) {
            if (int flags = ::fcntl(raw_fd.value(), F_GETFD); flags == -1 && errno == EBADF) {
                return {};
            }
            else if (flags != -1) {
                ::fcntl(raw_fd.value(), F_SETFD, flags | FD_CLOEXEC);
            }
            ::unsetenv("WAYLAND_SOCKET");
            fd.reset(raw_fd.value());
        }
        else {
            if (!name) {
                name = aux::getenv("WAYLAND_DISPLAY");
            }
            if (name.value().empty()) {
                name = "wayland-0";
            }

            std::filesystem::path dir = aux::getenv("XDG_RUNTIME_DIR");
            if ((dir.empty() || dir.is_relative()) && name.value().is_relative()) {
                error("XDG_RUNTIME_DIR is invalid or not set in the environtment.\n");
                return {};
            }

            if (int raw_fd = ::socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
                fd.reset(raw_fd);
            }
            else if (errno != EINVAL) {
                return {};
            }
            else {
                if (aux::unique_fd tmp_fd = ::socket(PF_LOCAL, SOCK_STREAM, 0)) {
                    int flags = ::fcntl(tmp_fd, F_GETFD);
                    if (flags == -1) {
                        return {};
                    }
                    if (::fcntl(tmp_fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
                        return {};
                    }
                    fd = std::move(tmp_fd);
                }
                return {};
            }

            std::string native_path = name.value().is_absolute()
                ? name.value().native()
                : (dir / name.value()).native();

            sockaddr_un addr = {
                .sun_family = AF_LOCAL,
            };
            if (sizeof (addr.sun_path) < native_path.size() + 1) {
                error("error: socket path ", native_path, " plus null terminator exceeds\n" );
                return {};
            }
            std::copy(native_path.begin(), native_path.end(), addr.sun_path);
            std::cout << "Check" << std::endl;
            std::cout << addr.sun_path << std::endl;
            if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr),
                          offsetof (sockaddr_un, sun_path) + native_path.size()) < 0) {
                return {};
            }
            std::cout << "OK" << std::endl;
        }
        return display {
            .fd = std::move(fd),
        };
    }

} // ::wayland


/////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <string_view>

#include <aux/tuple-support.hh>

int main() {
    try {
        if (auto disp = wayland::display_connect()) {
            std::cout << disp.value().fd << std::endl;
        }
        else {
            std::cerr << "Error!" << std::endl;
        }
    }
    catch (aux::exception& ex) {
        std::cout << ex << std::endl;
    }
    return 0;
}


#if 0
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
#endif
