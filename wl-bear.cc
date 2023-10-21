
#include <string_view>
#include <optional>
#include <charconv>
#include <array>

namespace aux
{
    template <class T>
    constexpr std::optional<T> from_text(char const* text) noexcept {
        if (text) {
            std::string_view view(text);
            T result;
            if (auto [p, e] = std::from_chars(view.begin(), view.end(), result); e == std::errc{}) {
                return result;
            }
        }
        return std::nullopt;
    }

    template <size_t N>
    constexpr size_t concat(char (&buf)[N], auto... args) noexcept {
        size_t i = 0;
        for (auto arg : {std::string_view(args)...}) {
            for (auto ch : arg) {
                buf[i++] = ch;
                if (i == N) {
                    break;
                }
            }
        }
        return i;
    }

} // ::aux

/////////////////////////////////////////////////////////////////////////////
#include <string_view>
#include <optional>
#include <iostream> // WIP: logger must be considered

#include <sys/socket.h>
#include <sys/un.h>

#include <aux/io.hh>

namespace wayland
{
    auto& logger = std::clog;

    aux::unique_fd& set_cloexec_or_close(aux::unique_fd& fd) noexcept {
        if (fd) {
            if (int flags = ::fcntl(fd, F_GETFD); flags == -1) {
                fd.reset();
            }
            else if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
                fd.reset();
            }
        }
        return fd;
    }

    struct display {
        aux::unique_fd fd;

        static std::optional<display> connect(char const* name = nullptr) noexcept {
            static constexpr auto get_prepared_sock = []() noexcept -> aux::unique_fd {
                auto fd = aux::from_text<int>(std::getenv("WAYLAND_SOCKET"));
                if (!fd) {
                    return {};
                }
                if (int flags = ::fcntl(fd.value(), F_GETFD); flags == -1 && errno == EBADF) {
                    return {};
                }
                else if (flags != -1) {
                    ::fcntl(fd.value(), F_SETFD, flags | FD_CLOEXEC);
                }
                ::unsetenv("WAYLAND_SOCKET");
                return fd.value();
            };
            static constexpr auto create_new_socket = []() noexcept -> aux::unique_fd {
                if (auto fd = ::socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
                    return fd; // ::socket supports SOCK_CLOEXEC
                }

                // Failed because ::socket does not support SOCK_CLOEXEC
                if (errno == EINVAL) {
                    if (aux::unique_fd fd = ::socket(PF_LOCAL, SOCK_STREAM, 0)) {
                        // append FD_CLOEXEC or close automatically
                        int flags = ::fcntl(fd, F_GETFD);
                        if (flags == -1) {
                            return {};
                        }
                        if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
                            return {};
                        }
                        return fd;
                    }
                }
                return {};
            };

            // At first, Try for getting prepared sockets.
            auto fd = get_prepared_sock();

            // If there is no prepared socket, open anew
            if (!fd) {
                // check path name configurations
                if (name == nullptr) {
                    name = std::getenv("WAYLAND_DISPLAY");
                }
                if (name == nullptr) {
                    name = "wayland-0";
                }
                bool path_is_absolute = name[0] == '/';
                char const* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
                if (((!runtime_dir || runtime_dir[0] != '/') && !path_is_absolute)) {
                    logger << "error: XDG_RUNTIME_DIR is invalid or not set in tthe environment.\n";
                    errno = ENOENT;
                    return std::nullopt;
                }

                fd = create_new_socket();
                if (fd) {
                    ::sockaddr_un addr = {
                        .sun_family = AF_LOCAL,
						.sun_path = {},
                    };
                    size_t bytes = 0;
                    if (path_is_absolute) {
                        bytes = aux::concat(addr.sun_path, name);
                    }
                    else {
                        bytes = aux::concat(addr.sun_path, runtime_dir, "/", name);
                    }
                    if (bytes == sizeof (addr.sun_path)) {
                        logger << "error: socket path exceeds sockaddr_un::sun_path...\n";
                        errno = ENAMETOOLONG;
                        return std::nullopt;
                    }
                    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr),
                                  offsetof(::sockaddr_un, sun_path) + bytes) < 0)
                    {
                        return std::nullopt;
                    }
                }
            }

            if (fd) {
                return display {
                    .fd = std::move(fd),
                };
            }
            return std::nullopt;
        }
    };
}

/////////////////////////////////////////////////////////////////////////////
#include <iostream>

int main() {
    if (auto display = wayland::display::connect()) {
        std::cout << display.value().fd << std::endl;
    }
    else {
        std::cerr << "NG" << std::endl;
    }
    return 0;
}
