// Minimal private Wayland server used only to observe Qt's xdg-shell size hints.
// It does not render or connect to the user's compositor.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include "xdg-shell-server.h"

struct surface_state {
    struct wl_display *display;
    struct wl_resource *xdg_surface;
    struct wl_resource *toplevel;
    int min_w, min_h, max_w, max_h;
    int configured, attached, reported;
    char app_id[128];
};

static void destroy(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}
static void surface_destroyed(struct wl_resource *resource) {
    free(wl_resource_get_user_data(resource));
}
static void surface_attach(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *buffer, int32_t x, int32_t y) {
    (void)client; (void)x; (void)y;
    struct surface_state *s = wl_resource_get_user_data(resource);
    s->attached = buffer != NULL;
}
static void surface_damage(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}
static void surface_region(struct wl_client *c, struct wl_resource *r, struct wl_resource *region) {
    (void)c; (void)r; (void)region;
}
static void surface_transform(struct wl_client *c, struct wl_resource *r, int32_t value) {
    (void)c; (void)r; (void)value;
}
static void surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    (void)resource;
    struct wl_resource *callback = wl_resource_create(client, &wl_callback_interface, 1, id);
    wl_callback_send_done(callback, 0);
    wl_resource_destroy(callback);
}
static void surface_commit(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    struct surface_state *s = wl_resource_get_user_data(resource);
    if (!s->xdg_surface || !s->toplevel) return;
    if (!s->configured) {
        s->configured = 1;
        struct wl_array states;
        wl_array_init(&states);
        xdg_toplevel_send_configure(s->toplevel, 480, 320, &states);
        xdg_surface_send_configure(s->xdg_surface, wl_display_next_serial(s->display));
        wl_array_release(&states);
    }
    if (s->attached && !s->reported && strcmp(s->app_id, "jot-native-prototype") == 0) {
        s->reported = 1;
        printf("mapped app=%s min=%dx%d max=%dx%d\n", s->app_id, s->min_w, s->min_h, s->max_w, s->max_h);
        fflush(stdout);
    }
}
static const struct wl_surface_interface surface_impl = {
    .destroy = destroy, .attach = surface_attach, .damage = surface_damage, .frame = surface_frame,
    .set_opaque_region = surface_region, .set_input_region = surface_region,
    .commit = surface_commit, .set_buffer_transform = surface_transform,
    .set_buffer_scale = surface_transform, .damage_buffer = surface_damage,
};
static void region_change(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}
static const struct wl_region_interface region_impl = {
    .destroy = destroy, .add = region_change, .subtract = region_change
};
static void create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct surface_state *s = calloc(1, sizeof(*s));
    s->display = wl_client_get_display(client);
    struct wl_resource *r = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(r, &surface_impl, s, surface_destroyed);
}
static void create_region(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    (void)resource;
    struct wl_resource *r = wl_resource_create(client, &wl_region_interface, 1, id);
    wl_resource_set_implementation(r, &region_impl, NULL, NULL);
}
static const struct wl_compositor_interface compositor_impl = {
    .create_surface = create_surface, .create_region = create_region
};
static void bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(client, &wl_compositor_interface, version < 4 ? version : 4, id);
    wl_resource_set_implementation(r, &compositor_impl, NULL, NULL);
}

static void top_text(struct wl_client *c, struct wl_resource *r, const char *text) {
    (void)c; (void)r; (void)text;
}
static void top_app_id(struct wl_client *c, struct wl_resource *r, const char *text) {
    (void)c;
    struct surface_state *s = wl_resource_get_user_data(r);
    snprintf(s->app_id, sizeof(s->app_id), "%s", text);
}
static void top_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *parent) {
    (void)c; (void)r; (void)parent;
}
static void top_min(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c;
    struct surface_state *s = wl_resource_get_user_data(r);
    s->min_w = w; s->min_h = h;
}
static void top_max(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c;
    struct surface_state *s = wl_resource_get_user_data(r);
    s->max_w = w; s->max_h = h;
}
static const struct xdg_toplevel_interface top_impl = {
    .destroy = destroy, .set_parent = top_parent, .set_title = top_text, .set_app_id = top_app_id,
    .set_min_size = top_min, .set_max_size = top_max,
};
static void get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct surface_state *s = wl_resource_get_user_data(resource);
    s->toplevel = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(s->toplevel, &top_impl, s, NULL);
}
static void geometry(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}
static void ack(struct wl_client *c, struct wl_resource *r, uint32_t serial) {
    (void)c; (void)r; (void)serial;
}
static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = destroy, .get_toplevel = get_toplevel, .set_window_geometry = geometry, .ack_configure = ack
};
static void get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface) {
    struct surface_state *s = wl_resource_get_user_data(surface);
    s->xdg_surface = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(s->xdg_surface, &xdg_surface_impl, s, NULL);
}
static void pong(struct wl_client *c, struct wl_resource *r, uint32_t serial) {
    (void)c; (void)r; (void)serial;
}
static const struct xdg_wm_base_interface wm_impl = {
    .destroy = destroy, .get_xdg_surface = get_xdg_surface, .pong = pong
};
static void bind_wm(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(client, &xdg_wm_base_interface, version < 2 ? version : 2, id);
    wl_resource_set_implementation(r, &wm_impl, NULL, NULL);
}
static void bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *r = wl_resource_create(client, &wl_output_interface, version < 2 ? version : 2, id);
    wl_output_send_geometry(r, 0, 0, 300, 190, WL_OUTPUT_SUBPIXEL_UNKNOWN, "Jot", "test", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, 1440, 900, 60000);
    if (wl_resource_get_version(r) >= 2) {
        wl_output_send_scale(r, 1);
        wl_output_send_done(r);
    }
}
int main(void) {
    struct wl_display *display = wl_display_create();
    if (!display || wl_display_init_shm(display) != 0 ||
        !wl_global_create(display, &wl_compositor_interface, 4, NULL, bind_compositor) ||
        !wl_global_create(display, &xdg_wm_base_interface, 2, NULL, bind_wm) ||
        !wl_global_create(display, &wl_output_interface, 2, NULL, bind_output) ||
        wl_display_add_socket(display, "jot-test-wayland") != 0) return 1;
    puts("ready");
    fflush(stdout);
    wl_display_run(display);
    // The process exits immediately; avoid destroying parent surface state
    // before its xdg children while clients are still connected.
    return 0;
}
