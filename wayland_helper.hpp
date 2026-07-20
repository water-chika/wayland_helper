#pragma once

#include <stdexcept>
#include <stdio.h>
#include <iostream>
#include <map>
#include <vector>
#include <array>
#include <source_location>
#include <functional>

#include <wayland-client.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <wayland-client.h>

#include "xdg-shell-client.h"

namespace wayland_helper {

using cpp_helper::configure;

static void randname(char *buf) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long r = ts.tv_nsec;
  for (int i = 0; i < 6; ++i) {
    buf[i] = 'A' + (r & 15) + (r & 16) * 2;
    r >>= 5;
  }
}
static int create_shm_file(void) {
  int retries = 100;
  do {
    char name[] = "/wl_shm-XXXXXX";
    randname(name + sizeof(name) - 7);
    --retries;
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      return fd;
    }
  } while (retries > 0 && errno == EEXIST);
  return -1;
}

int allocate_shm_file(size_t size) {
  int fd = create_shm_file();
  if (fd < 0)
    return -1;
  int ret;
  do {
    ret = ftruncate(fd, size);
  } while (ret < 0 && errno == EINTR);
  if (ret < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

#define PROJECT_NAME "test_wayland"

inline wl_display *display_connect(const char *name) {
  return wl_display_connect(name);
}
template <class T> class set_default_display_name : public T {
public:
  using parent = T;
  set_default_display_name(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
  }
  auto get_display_name() { return nullptr; }
};
template <class T> class add_display : public T {
public:
  using parent = T;
  add_display(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
    m_display = display_connect(parent::get_display_name());
    if (m_display == nullptr) {
      throw std::runtime_error{"wayland: failed to connect display"};
    }
  }
  ~add_display() {
    std::clog << "wayland: display disconnect" << std::endl;
    wl_display_disconnect(m_display);
  }
  auto get_display() { return m_display; }

private:
  wl_display *m_display;
};

template <class T> class add_registry_listener : public T {
public:
  using parent = T;
  add_registry_listener(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
    wl_registry *registry = parent::get_registry();
    static wl_registry_listener listener = {
        .global = parent::registry_handle_global,
        .global_remove = parent::registry_handle_global_remove,
    };
    // listener is referenced but does not used immediately, so I use
    // display_roundtrip to use it.
    wl_registry_add_listener(registry, &listener,
                             parent::get_registry_listener_user_data());
    auto display = parent::get_display();
    wl_display_roundtrip(display);
  }
};
template <class T> class cache_registry : public T {
public:
  using parent = T;
  cache_registry(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
    auto display = parent::get_display();
    m_registry = wl_display_get_registry(display);
  }
  auto get_registry() { return m_registry; }

private:
  wl_registry *m_registry;
};

template<typename T>
class add_empty_registy_binds : public T {
public:
    using parent = T;
    add_empty_registy_binds(const configure auto& conf) : parent{conf} {
    }
    auto get_registry_binds() {
        return std::vector<std::tuple<void *, const wl_interface *, int, std::function<void()>>>{};
    }
};
template<typename T>
class add_wm_base : public T {
public:
    using parent = T;
    add_wm_base(const configure auto& conf) : parent{conf} {
    }
    ~add_wm_base() {
        destroy_wm_base();
    }
    auto get_registry_binds() {
        auto binds = parent::get_registry_binds();
        binds.emplace_back(&wm_base, &xdg_wm_base_interface, 1,
                [this]() {
                    add_listener();
                }
                );
        return binds;
    }
    auto add_listener() {
        if (wm_base != nullptr) {
            static const struct xdg_wm_base_listener listener = {
                .ping = xdg_wm_base_ping,
            };
            xdg_wm_base_add_listener(wm_base, &listener, this);
        }
    }
    auto destroy_wm_base() {
        if (wm_base != nullptr) {
            xdg_wm_base_destroy(wm_base);
            wm_base = nullptr;
        }
    }
    static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                                 uint32_t serial) {
      xdg_wm_base_pong(xdg_wm_base, serial);
    }
    auto get_wm_base() {
        return wm_base;
    }
private:
    xdg_wm_base* wm_base;
};
template<typename T>
class add_compositor : public T {
public:
    using parent = T;
    add_compositor(const configure auto& conf) : parent{conf} {
    }
    ~add_compositor() {
        if (compositor != nullptr) {
            wl_compositor_release(compositor);
            compositor = nullptr;
        }
    }
    auto get_registry_binds() {
        auto binds = parent::get_registry_binds();
        binds.emplace_back(&compositor, &wl_compositor_interface, 4, [](){});
        return binds;
    }
    auto get_compositor() {
        return compositor;
    }
private:
    wl_compositor* compositor;
};
template<typename T>
class add_shm : public T {
public:
    using parent = T;
    add_shm(const configure auto& conf) : parent{conf} {
    }
    ~add_shm() {
        if (shm != nullptr) {
            wl_shm_release(shm);
            shm = nullptr;
        }
    }
    auto get_registry_binds() {
        auto binds = parent::get_registry_binds();
        binds.emplace_back(&shm, &wl_shm_interface, 1, [](){});
        return binds;
    }
    auto get_shm() {
        return shm;
    }
private:
    wl_shm* shm;
};
template<typename T>
class add_pointer : public T {
public:
    using parent = T;
    using this_type = add_pointer<T>;
    add_pointer(const configure auto& conf) : parent{conf},
        pointer{}
    {}
    void create_pointer(wl_pointer* p) {
        assert(pointer == nullptr);
        pointer = p;
        static const struct wl_pointer_listener listener = {
            .enter = wl_pointer_enter,
            .leave = wl_pointer_leave,
            .motion = wl_pointer_motion,
            .button = wl_pointer_button,
            .axis = wl_pointer_axis,
            .frame = wl_pointer_frame,
            .axis_source = wl_pointer_axis_source,
            .axis_stop = wl_pointer_axis_stop,
            .axis_discrete = wl_pointer_axis_discrete,
            .axis_value120 = wl_pointer_axis_value120,
            .axis_relative_direction = wl_pointer_axis_relative_direction,
        };
        wl_pointer_add_listener(pointer, &listener, this);
    }
    void release_pointer() {
        wl_pointer_release(pointer);
        pointer = nullptr;
    }

    static void wl_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, wl_surface* surface, int surface_x, int surface_y) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, wl_surface* surface) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
        auto this_ = (this_type*)data;
        if (this_->pointer_motion_callback) {
            this_->pointer_motion_callback(wl_fixed_to_int(surface_x), wl_fixed_to_int(surface_y), this_->pointer_motion_callback_user_data);
        }
    }
    static void wl_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t button_state) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
        auto this_ = (this_type*)data;
        if (this_->pointer_button_callback) {
            this_->pointer_button_callback(button, button_state, this_->pointer_button_callback_user_data);
        }
    }
    static void wl_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
        auto this_ = (this_type*)data;
        if (this_->pointer_axis_callback) {
            this_->pointer_axis_callback(axis, wl_fixed_to_int(value), this_->pointer_axis_callback_user_data);
        }
    }
    static void wl_pointer_frame(void* data, struct wl_pointer* pointer) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_axis_source(void* data, struct wl_pointer* pointer, uint32_t axis_source) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int discrete) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_axis_value120(void* data, struct wl_pointer* pointer, uint32_t axis, int value120) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    static void wl_pointer_axis_relative_direction(void* data, struct wl_pointer* pointer, uint32_t axis, uint32_t direction) {
        //std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }
    void set_pointer_motion_callback(void (*callback)(uint32_t, uint32_t, void*), void* user_data) {
        pointer_motion_callback = callback;
        pointer_motion_callback_user_data = user_data;
    }
    void set_pointer_button_callback(void (*callback)(int, int, void*), void* user_data) {
        pointer_button_callback = callback;
        pointer_button_callback_user_data = user_data;
    }
    void set_pointer_axis_callback(void (*callback)(uint32_t, int, void*), void* user_data) {
        pointer_axis_callback = callback;
        pointer_axis_callback_user_data = user_data;
    }
private:
    wl_pointer* pointer;
    void (*pointer_motion_callback)(uint32_t x, uint32_t y, void*);
    void* pointer_motion_callback_user_data;
    void (*pointer_button_callback)(int button, int button_state, void*);
    void* pointer_button_callback_user_data;
    void (*pointer_axis_callback)(uint32_t axis, int value, void*);
    void* pointer_axis_callback_user_data;
};
template<typename T>
class add_keyboard : public T {
public:
    using parent = T;
    using this_type = add_keyboard<T>;
    add_keyboard(const configure auto& conf) : parent{conf},
        keyboard{}
    {
    }
    void create_keyboard(wl_keyboard* k) {
        keyboard = k;
        static const struct wl_keyboard_listener listener = {
            .keymap = wl_keyboard_keymap,
            .enter = wl_keyboard_enter,
            .leave = wl_keyboard_leave,
            .key = wl_keyboard_key,
            .modifiers = wl_keyboard_modifiers,
            .repeat_info = wl_keyboard_repeat_info,
        };
        wl_keyboard_add_listener(k, &listener, this);
    }
    void release_keyboard() {
        wl_keyboard_release(keyboard);
        keyboard = nullptr;
    }
    static void wl_keyboard_keymap(void* data, struct wl_keyboard* keyboard,
            uint32_t keymap_format, int fd, uint32_t size) {
        std::clog << "wayland: " << std::source_location::current().function_name() <<
            std::format("{}, {}, {}", keymap_format, fd, size)
            << std::endl;
        auto this_ = (this_type*)data;
        if (keymap_format == 1) {
            if (this_->keymap_callback) {
                this_->keymap_callback(fd, size, this_->keymap_callback_user_data);
            }
        }
        close(fd);
    }

    static void wl_keyboard_enter(void* data, struct wl_keyboard* keyboard,
            uint32_t serial, wl_surface* surface, wl_array* keys) {
        std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }

    static void wl_keyboard_leave(void* data, struct wl_keyboard* keyboard,
            uint32_t serial, wl_surface* surface) {
        std::clog << "wayland: " << std::source_location::current().function_name() << std::endl;
    }

    static void wl_keyboard_key(void* data, struct wl_keyboard* keyboard,
            uint32_t serial, uint32_t time, uint32_t key, uint32_t key_state) {
        std::clog << "wayland: keyboard: key: " <<
            std::format("{}, {}, {:#x}, {}", serial, time, key, key_state)
            << std::endl;
        auto this_ = (this_type *)data;
        if (this_->key_callback) {
            this_->key_callback(key, key_state, this_->key_callback_user_data);
        }
    }

    static void wl_keyboard_modifiers(void* data, struct wl_keyboard* keyboard,
            uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
        std::clog << "wayland: keyboard: modifiers: " <<
            std::format("{}, {:#x}, {:#x}, {:#x}, {:#x}", serial, mods_depressed, mods_latched, mods_locked, group)
            << std::endl;
        auto this_ = (this_type*)data;
        if (this_->keyboard_modifiers_callback) {
            this_->keyboard_modifiers_callback(mods_depressed, mods_latched, mods_locked, group, this_->key_callback_user_data);
        }
    }

    static void wl_keyboard_repeat_info(void* data, struct wl_keyboard* keyboard,
            int rate, int delay) {
    }
    void set_key_callback(void (*callback)(int, int, void*), void* user_data) {
        key_callback = callback;
        key_callback_user_data = user_data;
    }
    void set_keymap_callback(void (*callback)(int, int, void*), void* user_data) {
        keymap_callback = callback;
        keymap_callback_user_data = user_data;
    }
    void set_keyboard_modifiers_callback(void (*callback)(uint32_t, uint32_t, uint32_t, uint32_t, void*), void* user_data) {
        keyboard_modifiers_callback = callback;
        keyboard_modifiers_callback_user_data = user_data;
    }

    ~add_keyboard() {
        if (keyboard != nullptr) {
            wl_keyboard_release(keyboard);
            keyboard = nullptr;
        }
    }
private:
    wl_keyboard* keyboard;
    void (*keymap_callback)(int, int, void*);
    void* keymap_callback_user_data;
    void (*keyboard_modifiers_callback)(uint32_t, uint32_t, uint32_t, uint32_t, void*);
    void* keyboard_modifiers_callback_user_data;
    void (*key_callback)(int, int, void*);
    void* key_callback_user_data;
};
template<typename T>
class add_seat : public T {
public:
    using parent = T;
    using this_type = add_seat<T>;
    add_seat(const configure auto& conf) : parent{conf} {
    }
    ~add_seat() {
        if (seat != nullptr) {
            wl_seat_release(seat);
            seat = nullptr;
        }
    }
    auto get_registry_binds() {
        auto binds = parent::get_registry_binds();
        binds.emplace_back(&seat, &wl_seat_interface, 9,
                [this]() {
                    add_listener();
                });
        return binds;
    }
    auto add_listener() {
        if (seat != nullptr) {
            static const wl_seat_listener seat_listener{
                .capabilities = wl_seat_capabilities,
                .name = wl_seat_name,
            };
            wl_seat_add_listener(seat, &seat_listener, this);
        }
    }
    static void wl_seat_capabilities(void* data, struct wl_seat* seat, uint32_t capability) {
        std::clog << "wayland: seat: capabilities: " << capability << std::endl;
        auto this_ = (this_type*)data;
        this_->process_capabilities_event(seat, capability);
    }
    static void wl_seat_name(void* data, struct wl_seat* seat, const char* name) {
        std::clog << "wayland: seat: name: " << name << std::endl;
    }
    void process_capabilities_event(wl_seat* seat, uint32_t capability) {
        if (capability & WL_SEAT_CAPABILITY_KEYBOARD){
            parent::create_keyboard(wl_seat_get_keyboard(seat));
        }
        else {
            parent::release_keyboard();
        }
        if (capability & WL_SEAT_CAPABILITY_POINTER) {
            parent::create_pointer(wl_seat_get_pointer(seat));
        }
        else {
            parent::release_pointer();
        }
    }

    auto get_seat() {
        return seat;
    }
private:
    wl_seat* seat;
};
template <class T> class add_registry_listener_callbacks : public T {
public:
  using parent = T;
  using this_type = add_registry_listener_callbacks<T>;
  add_registry_listener_callbacks(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
  }
  void registry_handle_global(wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t version) {
    auto binds = parent::get_registry_binds();
    std::map<std::string, std::tuple<void *, const wl_interface *, int, std::function<void()>>>
        bind_map;
    for (auto &[state_ptr, state_interface, version, process_bind_event] : binds) {
      bind_map.emplace(state_interface->name,
                       std::tuple{state_ptr, state_interface, version, process_bind_event});
    }
    if (bind_map.contains(interface)) {
      auto &[state_ptr, state_interface, version, process_bind_event] = bind_map[interface];
      *reinterpret_cast<void **>(state_ptr) =
          wl_registry_bind(registry, name, state_interface, version);
      process_bind_event();
    }
  }
  static void registry_handle_global(void *data, wl_registry *registry,
                                     uint32_t name, const char *interface,
                                     uint32_t version) {
    reinterpret_cast<this_type *>(data)->registry_handle_global(
        registry, name, interface, version);
  }
  static void registry_handle_global_remove(void *data, wl_registry *registry,
                                            uint32_t name) {}
  void *get_registry_listener_user_data() { return this; }

private:
};
template <typename T> class add_surface : public T {
public:
  using parent = T;
  add_surface(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
    auto compositor = parent::get_compositor();
    m_surface = wl_compositor_create_surface(compositor);
  }

private:
  wl_surface *m_surface;
};

template<typename T>
using add_wayland_surface_parent =
    add_registry_listener<
    cache_registry<
    add_registry_listener_callbacks<
    add_seat<
    add_pointer<
    add_keyboard<
    add_shm<
    add_wm_base<
    add_compositor<
    add_empty_registy_binds<
    add_display<
    set_default_display_name<
    T
    >>>>>>>>>>>>
;

template <class T> class add_wayland_surface : public add_wayland_surface_parent<T> {
public:
    using parent = add_wayland_surface_parent<T>;
    using this_type = add_wayland_surface;
    static void dummy_size_changed_callback(int, int, void*) {
    }
  add_wayland_surface(const vulkan_hpp_helper::configure auto& conf) : parent{conf},
      size_changed_callback{dummy_size_changed_callback},
      width{640}, height{480}
  {
    surface = wl_compositor_create_surface(parent::get_compositor());
    surface_xdg =
        xdg_wm_base_get_xdg_surface(parent::get_wm_base(), surface);

    static const struct xdg_toplevel_listener xdg_toplevel_listener = {
        .configure = xdg_toplevel_configure,
        .close = xdg_toplevel_close,
    };

    static const struct xdg_surface_listener xdg_surface_listener = {
        .configure = xdg_surface_configure,
    };
    xdg_surface_add_listener(surface_xdg, &xdg_surface_listener, this);
    toplevel = xdg_surface_get_toplevel(surface_xdg);
    xdg_toplevel_set_title(toplevel, "Example client");
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, this);
    xdg_toplevel_set_title(toplevel, "Example client");
    wl_surface_commit(surface);

    wl_display_roundtrip(parent::get_display());
  }
  auto get_wayland_display() { return parent::get_display(); }
  auto get_wayland_surface() { return surface; }
  auto get_surface_resolution() { return std::pair{width, height}; }
  auto get_event_loop_should_exit() { return closed; }
  void set_size_changed_callback(void (*callback)(int, int, void*), void* user_data) {
      if (size_changed) {
          callback(width, height, user_data);
          size_changed = false;
      }
      size_changed_callback = callback;
      size_changed_callback_user_data = user_data;
  }
  static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                    uint32_t serial) {
    auto this_ = (this_type*)data;
    xdg_surface_ack_configure(this_->surface_xdg, serial);
  }
  static void xdg_toplevel_configure(void *data,
                                     struct xdg_toplevel *xdg_toplevel,
                                     int32_t width, int32_t height,
                                     struct wl_array *states) {
    auto this_= (this_type*)data;
    if (width == 0 || height == 0) {
      return;
    }
    if (width != this_->width || height != this_->height) {
        this_->width = width;
        this_->height = height;
        this_->size_changed = true;
        this_->size_changed_callback(width, height,
                this_->size_changed_callback_user_data);
    }
  }

  static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    auto this_= (this_type*)data;
    this_->closed = true;
  }

private:
  int width, height;
  bool closed;
  bool size_changed;
  void (*size_changed_callback)(int, int, void*);
  void* size_changed_callback_user_data;
  xdg_surface* surface_xdg;
  wl_surface* surface;
  xdg_toplevel* toplevel;
};

using cpp_helper::configure;

template <class T> class add_wayland_event_loop : public T {
public:
  using parent = T;
  add_wayland_event_loop(const configure auto& conf) : parent{conf} {
  }
  void event_loop() {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    while (!parent::get_event_loop_should_exit()) {
        // Dispath without blocking
        wl_display_dispatch_pending(parent::get_wayland_display());

        parent::draw();
    }
#endif
  }
};
template <class T> class run_wayland_event_loop : public T {
public:
  using parent = T;
  run_wayland_event_loop(const vulkan_hpp_helper::configure auto& conf) : parent{conf} {
      parent::event_loop();
  }
};

template<typename T>
class add_wayland_pollfd : public T {
public:
    using parent = T;
    add_wayland_pollfd(const configure auto& conf) : parent{conf} {
    }
    static constexpr int FDS_INDEX = parent::FDS_SIZE;
    static constexpr int FDS_SIZE = parent::FDS_SIZE+1;
    std::array<struct pollfd, FDS_SIZE> get_fds() {
        std::array<pollfd, FDS_SIZE> res{};
        auto fds = parent::get_fds();
        std::copy(fds.begin(), fds.end(), res.begin());
        res.back() = pollfd{
            .fd = wl_display_get_fd(parent::get_wayland_display()),
            .events = POLLIN,
        };
        auto display = parent::get_wayland_display();
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);
        return res;
    }
    void process_events(auto& fds) {
        auto display = parent::get_wayland_display();
        if (fds[FDS_INDEX].revents & POLLIN) {
            assert(fds[FDS_INDEX].fd == wl_display_get_fd(display));
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        }
        else {
            wl_display_cancel_read(display);
        }

        parent::process_events(fds);
    }
};

template<typename T>
class add_wayland_pollfds_loop : public T {
public:
    using parent = T;
    add_wayland_pollfds_loop(const configure auto& conf) : parent{conf} {
    }
    void event_loop() {
        while (!parent::get_event_loop_should_exit()) {
            parent::poll_events();
        }
    }
};

} // namespace wayland_helper
