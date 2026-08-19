/* ============================================================================
 * ble-gatt-server — a BLE GATT server on BlueZ, in C, over D-Bus.
 *
 * This mirrors the architecture used in production BlueZ applications:
 * you do NOT write ATT handles or build an attribute table yourself.
 * You declare an object tree over D-Bus, hand its root to BlueZ via
 * GattManager1.RegisterApplication(), and BlueZ walks your tree through
 * org.freedesktop.DBus.ObjectManager.GetManagedObjects() to build the
 * attribute database and assign the handles.
 *
 * OBJECT TREE WE EXPORT
 * ---------------------
 *   /com/nancy/ble                          <- app root, ObjectManager
 *   /com/nancy/ble/service0                 <- GattService1
 *   /com/nancy/ble/service0/char0           <- GattCharacteristic1  TEMPERATURE (read+notify)
 *   /com/nancy/ble/service0/char0/desc0     <- GattDescriptor1      CCCD 0x2902
 *   /com/nancy/ble/service0/char1           <- GattCharacteristic1  COMMAND (write)
 *   /com/nancy/ble/service0/char2           <- GattCharacteristic1  STATUS (read)
 *   /com/nancy/ble/advertisement0           <- LEAdvertisement1
 *
 * The PATH HIERARCHY *is* the GATT hierarchy: characteristics are children
 * of their service, descriptors are children of their characteristic.
 *
 * Build:  cmake -B build && cmake --build build
 * Run:    sudo ./build/ble-gatt-server
 * Watch:  sudo btmon        (in another terminal)
 * ==========================================================================*/

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>   /* g_unix_signal_add */
#include <signal.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * SECTION 1 — Names, paths and UUIDs
 *
 * UUID scheme, deliberately structured rather than random:
 *
 *     f1d0XXXX-YYYY-4a5b-9c3d-0e1f2a3b4c5d
 *          |     |
 *          |     +-- characteristic index (0000 = the service itself)
 *          +-------- service index
 *
 * Structured UUIDs make a btmon trace readable. Random ones do not.
 * -------------------------------------------------------------------------*/

#define BLUEZ_BUS_NAME      "org.bluez"
#define ADAPTER_PATH        "/org/bluez/hci0"

#define APP_PATH            "/com/nancy/ble"
#define SERVICE0_PATH       APP_PATH "/service0"
#define CHAR_TEMP_PATH      SERVICE0_PATH "/char0"
#define CCCD_TEMP_PATH      CHAR_TEMP_PATH "/desc0"
#define CHAR_CMD_PATH       SERVICE0_PATH "/char1"
#define CHAR_STATUS_PATH    SERVICE0_PATH "/char2"
#define ADVERT_PATH         APP_PATH "/advertisement0"

#define UUID_SERVICE0       "f1d00001-0000-4a5b-9c3d-0e1f2a3b4c5d"
#define UUID_CHAR_TEMP      "f1d00001-0001-4a5b-9c3d-0e1f2a3b4c5d"
#define UUID_CHAR_CMD       "f1d00001-0002-4a5b-9c3d-0e1f2a3b4c5d"
#define UUID_CHAR_STATUS    "f1d00001-0003-4a5b-9c3d-0e1f2a3b4c5d"
#define UUID_CCCD           "00002902-0000-1000-8000-00805f9b34fb"  /* SIG-adopted */

#define LOCAL_NAME          "Nancy-BLE-Lab"

/* ---------------------------------------------------------------------------
 * SECTION 2 — Runtime state
 * -------------------------------------------------------------------------*/

typedef struct {
    GDBusConnection *conn;
    GMainLoop       *loop;

    /* TEMPERATURE characteristic */
    gint16   temperature_c10;   /* tenths of a degree, e.g. 235 = 23.5 C */
    gboolean notifying;         /* did the peer write 0x0001 to the CCCD? */
    guint    notify_timer_id;

    /* STATUS characteristic */
    gchar    status[32];

    /* registration IDs, so we can unregister cleanly on exit */
    guint    reg_ids[8];
    guint    n_reg_ids;
} app_t;

static app_t app;

/* ---------------------------------------------------------------------------
 * SECTION 3 — Introspection XML
 *
 * GDBus needs to know each interface's methods, properties and signatures
 * before it can dispatch to our handlers. These strings ARE the contract
 * with BlueZ — the names and type signatures must match BlueZ's D-Bus API
 * exactly, or registration silently does nothing.
 * -------------------------------------------------------------------------*/

static const gchar objmgr_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static const gchar service_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar characteristic_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='ReadValue'>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "      <arg name='value'   type='ay'    direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg name='value'   type='ay'    direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "    <property name='UUID'      type='s'  access='read'/>"
    "    <property name='Service'   type='o'  access='read'/>"
    "    <property name='Flags'     type='as' access='read'/>"
    "    <property name='Value'     type='ay' access='read'/>"
    "    <property name='Notifying' type='b'  access='read'/>"
    "  </interface>"
    "</node>";

static const gchar descriptor_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattDescriptor1'>"
    "    <method name='ReadValue'>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "      <arg name='value'   type='ay'    direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg name='value'   type='ay'    direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <property name='UUID'           type='s'  access='read'/>"
    "    <property name='Characteristic' type='o'  access='read'/>"
    "    <property name='Flags'          type='as' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar advertisement_xml[] =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type'         type='s'  access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='LocalName'    type='s'  access='read'/>"
    "  </interface>"
    "</node>";

/* ---------------------------------------------------------------------------
 * SECTION 4 — Small helpers
 * -------------------------------------------------------------------------*/

/* Wrap raw bytes as a D-Bus 'ay' (byte array). */
static GVariant *bytes_to_ay(const void *data, gsize len)
{
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, len, sizeof(guchar));
}

/* Return a single value from a method call as the required 1-tuple. */
static void return_single(GDBusMethodInvocation *inv, GVariant *value)
{
    g_dbus_method_invocation_return_value(inv, g_variant_new_tuple(&value, 1));
}

/* Build the a{sv} property dictionary for one interface of one object. */
static void add_prop(GVariantBuilder *b, const gchar *name, GVariant *value)
{
    g_variant_builder_add(b, "{sv}", name, value);
}

/* A GVariant 'as' from a NULL-terminated array of strings. */
static GVariant *strv_to_as(const gchar *const *strv)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    for (int i = 0; strv[i] != NULL; i++)
        g_variant_builder_add(&b, "s", strv[i]);
    return g_variant_builder_end(&b);
}

/* ---------------------------------------------------------------------------
 * SECTION 5 — GattService1
 *
 * A service has no methods — only two properties. BlueZ reads them while
 * walking our tree.
 * -------------------------------------------------------------------------*/

static GVariant *service_get_property(GDBusConnection *c, const gchar *sender,
                                      const gchar *path, const gchar *iface,
                                      const gchar *prop, GError **err, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)err; (void)ud;

    if (g_strcmp0(prop, "UUID") == 0)
        return g_variant_new_string(UUID_SERVICE0);
    if (g_strcmp0(prop, "Primary") == 0)
        return g_variant_new_boolean(TRUE);
    return NULL;
}

static const GDBusInterfaceVTable service_vtable = {
    NULL, service_get_property, NULL, { 0 }
};

/* ---------------------------------------------------------------------------
 * SECTION 6 — GattCharacteristic1
 *
 * This is where the real work happens. BlueZ calls ReadValue when a peer
 * issues an ATT Read Request, WriteValue on an ATT Write, and
 * StartNotify/StopNotify when the peer writes to the CCCD.
 * -------------------------------------------------------------------------*/

static void characteristic_method_call(GDBusConnection *c, const gchar *sender,
                                       const gchar *path, const gchar *iface,
                                       const gchar *method, GVariant *params,
                                       GDBusMethodInvocation *inv, gpointer ud)
{
    (void)c; (void)sender; (void)iface; (void)ud;

    /* ---- TEMPERATURE: read ---- */
    if (g_strcmp0(path, CHAR_TEMP_PATH) == 0 && g_strcmp0(method, "ReadValue") == 0) {
        guint8 buf[2];
        buf[0] = (guint8)(app.temperature_c10 & 0xFF);          /* little-endian, */
        buf[1] = (guint8)((app.temperature_c10 >> 8) & 0xFF);   /* like BLE itself */
        g_print("[read ] TEMPERATURE -> %d.%d C\n",
                app.temperature_c10 / 10, ABS(app.temperature_c10 % 10));
        return_single(inv, bytes_to_ay(buf, sizeof buf));
        return;
    }

    /* ---- TEMPERATURE: notifications on/off ----
     * BlueZ calls these when the peer writes 0x0001 / 0x0000 to the CCCD.
     * We do NOT send ATT packets ourselves — we emit a PropertiesChanged
     * signal on 'Value' and BlueZ turns it into an ATT notification.       */
    if (g_strcmp0(path, CHAR_TEMP_PATH) == 0 && g_strcmp0(method, "StartNotify") == 0) {
        app.notifying = TRUE;
        g_print("[notify] START — peer subscribed\n");
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (g_strcmp0(path, CHAR_TEMP_PATH) == 0 && g_strcmp0(method, "StopNotify") == 0) {
        app.notifying = FALSE;
        g_print("[notify] STOP — peer unsubscribed\n");
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }

    /* ---- COMMAND: write ---- */
    if (g_strcmp0(path, CHAR_CMD_PATH) == 0 && g_strcmp0(method, "WriteValue") == 0) {
        GVariant *arr = NULL, *opts = NULL;
        g_variant_get(params, "(@ay@a{sv})", &arr, &opts);

        gsize n = 0;
        const guchar *d = g_variant_get_fixed_array(arr, &n, sizeof(guchar));

        g_print("[write] COMMAND <- %zu bytes:", n);
        for (gsize i = 0; i < n; i++) g_print(" %02X", d[i]);
        g_print("\n");

        /* trivial command handling, to show the pattern */
        if (n >= 1) {
            switch (d[0]) {
            case 0x01: g_strlcpy(app.status, "RUNNING", sizeof app.status); break;
            case 0x02: g_strlcpy(app.status, "PAUSED",  sizeof app.status); break;
            default:   g_strlcpy(app.status, "UNKNOWN", sizeof app.status); break;
            }
            g_print("[state] STATUS is now \"%s\"\n", app.status);
        }

        g_variant_unref(arr);
        g_variant_unref(opts);
        g_dbus_method_invocation_return_value(inv, NULL);   /* void reply */
        return;
    }

    /* ---- STATUS: read ---- */
    if (g_strcmp0(path, CHAR_STATUS_PATH) == 0 && g_strcmp0(method, "ReadValue") == 0) {
        g_print("[read ] STATUS -> \"%s\"\n", app.status);
        return_single(inv, bytes_to_ay(app.status, strlen(app.status)));
        return;
    }

    /* Anything else is a protocol error — tell the caller properly. */
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "Unsupported method %s on %s", method, path);
}

static GVariant *characteristic_get_property(GDBusConnection *c, const gchar *sender,
                                             const gchar *path, const gchar *iface,
                                             const gchar *prop, GError **err, gpointer ud)
{
    (void)c; (void)sender; (void)iface; (void)err; (void)ud;

    if (g_strcmp0(prop, "Service") == 0)
        return g_variant_new_object_path(SERVICE0_PATH);

    if (g_strcmp0(path, CHAR_TEMP_PATH) == 0) {
        if (g_strcmp0(prop, "UUID") == 0) return g_variant_new_string(UUID_CHAR_TEMP);
        if (g_strcmp0(prop, "Flags") == 0) {
            const gchar *f[] = { "read", "notify", NULL };
            return strv_to_as(f);
        }
        if (g_strcmp0(prop, "Notifying") == 0) return g_variant_new_boolean(app.notifying);
        if (g_strcmp0(prop, "Value") == 0) {
            guint8 buf[2] = { (guint8)(app.temperature_c10 & 0xFF),
                              (guint8)((app.temperature_c10 >> 8) & 0xFF) };
            return bytes_to_ay(buf, sizeof buf);
        }
    }

    if (g_strcmp0(path, CHAR_CMD_PATH) == 0) {
        if (g_strcmp0(prop, "UUID") == 0) return g_variant_new_string(UUID_CHAR_CMD);
        if (g_strcmp0(prop, "Flags") == 0) {
            const gchar *f[] = { "write", "write-without-response", NULL };
            return strv_to_as(f);
        }
        if (g_strcmp0(prop, "Notifying") == 0) return g_variant_new_boolean(FALSE);
        if (g_strcmp0(prop, "Value") == 0) return bytes_to_ay("", 0);
    }

    if (g_strcmp0(path, CHAR_STATUS_PATH) == 0) {
        if (g_strcmp0(prop, "UUID") == 0) return g_variant_new_string(UUID_CHAR_STATUS);
        if (g_strcmp0(prop, "Flags") == 0) {
            const gchar *f[] = { "read", NULL };
            return strv_to_as(f);
        }
        if (g_strcmp0(prop, "Notifying") == 0) return g_variant_new_boolean(FALSE);
        if (g_strcmp0(prop, "Value") == 0)
            return bytes_to_ay(app.status, strlen(app.status));
    }

    return NULL;
}

static const GDBusInterfaceVTable characteristic_vtable = {
    characteristic_method_call, characteristic_get_property, NULL, { 0 }
};

/* ---------------------------------------------------------------------------
 * SECTION 7 — GattDescriptor1 (the CCCD)
 *
 * Client Characteristic Configuration Descriptor, UUID 0x2902.
 * The peer writes 0x0001 here to subscribe to notifications.
 * BlueZ handles the CCCD write itself and calls our StartNotify, so this
 * descriptor mostly exists so the attribute appears in the database.
 * -------------------------------------------------------------------------*/

static void descriptor_method_call(GDBusConnection *c, const gchar *sender,
                                   const gchar *path, const gchar *iface,
                                   const gchar *method, GVariant *params,
                                   GDBusMethodInvocation *inv, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)params; (void)ud;

    if (g_strcmp0(method, "ReadValue") == 0) {
        guint8 v[2] = { app.notifying ? 0x01 : 0x00, 0x00 };
        return_single(inv, bytes_to_ay(v, sizeof v));
        return;
    }
    if (g_strcmp0(method, "WriteValue") == 0) {
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED, "no");
}

static GVariant *descriptor_get_property(GDBusConnection *c, const gchar *sender,
                                         const gchar *path, const gchar *iface,
                                         const gchar *prop, GError **err, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)err; (void)ud;

    if (g_strcmp0(prop, "UUID") == 0)
        return g_variant_new_string(UUID_CCCD);
    if (g_strcmp0(prop, "Characteristic") == 0)
        return g_variant_new_object_path(CHAR_TEMP_PATH);
    if (g_strcmp0(prop, "Flags") == 0) {
        const gchar *f[] = { "read", "write", NULL };
        return strv_to_as(f);
    }
    return NULL;
}

static const GDBusInterfaceVTable descriptor_vtable = {
    descriptor_method_call, descriptor_get_property, NULL, { 0 }
};

/* ---------------------------------------------------------------------------
 * SECTION 8 — LEAdvertisement1
 *
 * Legacy advertising carries at most 31 bytes. A 128-bit UUID costs 18 of
 * them (1 length + 1 type + 16 UUID), Flags costs 3, and a 13-character
 * local name costs 15. That is 36 — over budget. BlueZ resolves this by
 * moving what does not fit into the scan response.
 * -------------------------------------------------------------------------*/

static void advertisement_method_call(GDBusConnection *c, const gchar *sender,
                                      const gchar *path, const gchar *iface,
                                      const gchar *method, GVariant *params,
                                      GDBusMethodInvocation *inv, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)params; (void)ud;

    if (g_strcmp0(method, "Release") == 0) {
        g_print("[advert] BlueZ released the advertisement\n");
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED, "no");
}

static GVariant *advertisement_get_property(GDBusConnection *c, const gchar *sender,
                                            const gchar *path, const gchar *iface,
                                            const gchar *prop, GError **err, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)err; (void)ud;

    if (g_strcmp0(prop, "Type") == 0)
        return g_variant_new_string("peripheral");   /* connectable undirected */
    if (g_strcmp0(prop, "LocalName") == 0)
        return g_variant_new_string(LOCAL_NAME);
    if (g_strcmp0(prop, "ServiceUUIDs") == 0) {
        const gchar *u[] = { UUID_SERVICE0, NULL };
        return strv_to_as(u);
    }
    return NULL;
}

static const GDBusInterfaceVTable advertisement_vtable = {
    advertisement_method_call, advertisement_get_property, NULL, { 0 }
};

/* ---------------------------------------------------------------------------
 * SECTION 9 — ObjectManager.GetManagedObjects
 *
 * THE ANSWER TO "without a database, how did you register your handlers?"
 *
 * After RegisterApplication(), BlueZ calls this. We return the whole tree:
 *   a{oa{sa{sv}}}  =  path -> interface -> property -> value
 * BlueZ reads it, builds the ATT attribute database, and assigns handles.
 * -------------------------------------------------------------------------*/

static void add_object(GVariantBuilder *objects, const gchar *path,
                       const gchar *iface, GVariantBuilder *props)
{
    GVariantBuilder ifaces;
    g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_add(&ifaces, "{s@a{sv}}", iface, g_variant_builder_end(props));
    g_variant_builder_add(objects, "{o@a{sa{sv}}}", path, g_variant_builder_end(&ifaces));
}

static void add_characteristic_object(GVariantBuilder *objects, const gchar *path,
                                      const gchar *uuid, const gchar *const *flags)
{
    GVariantBuilder p;
    g_variant_builder_init(&p, G_VARIANT_TYPE("a{sv}"));
    add_prop(&p, "UUID",    g_variant_new_string(uuid));
    add_prop(&p, "Service", g_variant_new_object_path(SERVICE0_PATH));
    add_prop(&p, "Flags",   strv_to_as(flags));
    add_object(objects, path, "org.bluez.GattCharacteristic1", &p);
}

static void objmgr_method_call(GDBusConnection *c, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *method, GVariant *params,
                               GDBusMethodInvocation *inv, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)params; (void)ud;

    if (g_strcmp0(method, "GetManagedObjects") != 0) {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD, "no");
        return;
    }

    g_print("[objmgr] BlueZ called GetManagedObjects — sending the tree\n");

    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

    /* the service */
    GVariantBuilder svc;
    g_variant_builder_init(&svc, G_VARIANT_TYPE("a{sv}"));
    add_prop(&svc, "UUID",    g_variant_new_string(UUID_SERVICE0));
    add_prop(&svc, "Primary", g_variant_new_boolean(TRUE));
    add_object(&objects, SERVICE0_PATH, "org.bluez.GattService1", &svc);

    /* the three characteristics */
    const gchar *temp_flags[]   = { "read", "notify", NULL };
    const gchar *cmd_flags[]    = { "write", "write-without-response", NULL };
    const gchar *status_flags[] = { "read", NULL };
    add_characteristic_object(&objects, CHAR_TEMP_PATH,   UUID_CHAR_TEMP,   temp_flags);
    add_characteristic_object(&objects, CHAR_CMD_PATH,    UUID_CHAR_CMD,    cmd_flags);
    add_characteristic_object(&objects, CHAR_STATUS_PATH, UUID_CHAR_STATUS, status_flags);

    /* the CCCD */
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    const gchar *cccd_flags[] = { "read", "write", NULL };
    add_prop(&d, "UUID",           g_variant_new_string(UUID_CCCD));
    add_prop(&d, "Characteristic", g_variant_new_object_path(CHAR_TEMP_PATH));
    add_prop(&d, "Flags",          strv_to_as(cccd_flags));
    add_object(&objects, CCCD_TEMP_PATH, "org.bluez.GattDescriptor1", &d);

    GVariant *result = g_variant_builder_end(&objects);
    g_dbus_method_invocation_return_value(inv, g_variant_new_tuple(&result, 1));
}

static const GDBusInterfaceVTable objmgr_vtable = {
    objmgr_method_call, NULL, NULL, { 0 }
};

/* ---------------------------------------------------------------------------
 * SECTION 10 — Sending a notification
 *
 * We never build an ATT packet. We emit PropertiesChanged on the
 * characteristic's 'Value'; BlueZ sees it and, if the peer has subscribed
 * via the CCCD, sends the ATT Handle Value Notification.
 * -------------------------------------------------------------------------*/

static gboolean tick_temperature(gpointer user_data)
{
    (void)user_data;

    app.temperature_c10 += 3;                       /* pretend sensor drift */
    if (app.temperature_c10 > 300) app.temperature_c10 = 200;

    if (!app.notifying)
        return G_SOURCE_CONTINUE;                   /* nobody is listening */

    guint8 buf[2] = { (guint8)(app.temperature_c10 & 0xFF),
                      (guint8)((app.temperature_c10 >> 8) & 0xFF) };

    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "Value", bytes_to_ay(buf, sizeof buf));

    GVariantBuilder invalidated;
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

    GError *err = NULL;
    g_dbus_connection_emit_signal(app.conn, NULL, CHAR_TEMP_PATH,
                                  "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)",
                                                "org.bluez.GattCharacteristic1",
                                                &changed, &invalidated),
                                  &err);
    if (err) {
        g_printerr("emit_signal failed: %s\n", err->message);
        g_error_free(err);
    } else {
        g_print("[notif] TEMPERATURE = %d.%d C\n",
                app.temperature_c10 / 10, ABS(app.temperature_c10 % 10));
    }
    return G_SOURCE_CONTINUE;
}

/* ---------------------------------------------------------------------------
 * SECTION 11 — Registering the objects on the bus
 * -------------------------------------------------------------------------*/

static gboolean export_object(const gchar *path, const gchar *xml,
                              const GDBusInterfaceVTable *vtable)
{
    GError *err = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &err);
    if (err) {
        g_printerr("bad introspection XML for %s: %s\n", path, err->message);
        g_error_free(err);
        return FALSE;
    }

    guint id = g_dbus_connection_register_object(app.conn, path,
                                                info->interfaces[0],
                                                vtable, NULL, NULL, &err);
    g_dbus_node_info_unref(info);

    if (err) {
        g_printerr("register_object(%s) failed: %s\n", path, err->message);
        g_error_free(err);
        return FALSE;
    }

    app.reg_ids[app.n_reg_ids++] = id;
    g_print("  exported %s\n", path);
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * SECTION 12 — Handing the tree to BlueZ
 * -------------------------------------------------------------------------*/

static void on_register_app(GObject *src, GAsyncResult *res, gpointer ud)
{
    (void)ud;
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (err) {
        g_printerr("\n*** GattManager1.RegisterApplication FAILED: %s\n", err->message);
        g_printerr("    Is bluetoothd running? Is %s present?\n", ADAPTER_PATH);
        g_error_free(err);
        g_main_loop_quit(app.loop);
        return;
    }
    g_variant_unref(r);
    g_print("[bluez ] GATT application registered — BlueZ owns the handles now\n");
}

static void on_register_advert(GObject *src, GAsyncResult *res, gpointer ud)
{
    (void)ud;
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (err) {
        g_printerr("[advert] RegisterAdvertisement failed: %s\n", err->message);
        g_error_free(err);
        return;
    }
    g_variant_unref(r);
    g_print("[bluez ] Advertising as \"%s\"\n", LOCAL_NAME);
}

static void register_with_bluez(void)
{
    /* GattManager1.RegisterApplication(object_path, dict options) */
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(app.conn, BLUEZ_BUS_NAME, ADAPTER_PATH,
                           "org.bluez.GattManager1", "RegisterApplication",
                           g_variant_new("(oa{sv})", APP_PATH, &opts),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                           on_register_app, NULL);

    /* LEAdvertisingManager1.RegisterAdvertisement(object_path, dict options) */
    GVariantBuilder aopts;
    g_variant_builder_init(&aopts, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(app.conn, BLUEZ_BUS_NAME, ADAPTER_PATH,
                           "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
                           g_variant_new("(oa{sv})", ADVERT_PATH, &aopts),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                           on_register_advert, NULL);
}

/* ---------------------------------------------------------------------------
 * SECTION 13 — Shutdown
 * -------------------------------------------------------------------------*/

static gboolean on_signal(gpointer ud)
{
    (void)ud;
    g_print("\n[exit  ] unregistering...\n");

    g_dbus_connection_call_sync(app.conn, BLUEZ_BUS_NAME, ADAPTER_PATH,
                                "org.bluez.LEAdvertisingManager1",
                                "UnregisterAdvertisement",
                                g_variant_new("(o)", ADVERT_PATH),
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

    g_dbus_connection_call_sync(app.conn, BLUEZ_BUS_NAME, ADAPTER_PATH,
                                "org.bluez.GattManager1", "UnregisterApplication",
                                g_variant_new("(o)", APP_PATH),
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

    for (guint i = 0; i < app.n_reg_ids; i++)
        g_dbus_connection_unregister_object(app.conn, app.reg_ids[i]);

    g_main_loop_quit(app.loop);
    return G_SOURCE_REMOVE;
}

/* ---------------------------------------------------------------------------
 * SECTION 14 — main
 * -------------------------------------------------------------------------*/

int main(void)
{
    GError *err = NULL;

    memset(&app, 0, sizeof app);
    app.temperature_c10 = 235;                       /* 23.5 C */
    g_strlcpy(app.status, "IDLE", sizeof app.status);

    g_print("ble-gatt-server — GATT server on BlueZ over D-Bus\n");
    g_print("=================================================\n\n");

    /* 1. connect to the SYSTEM bus (BlueZ lives there, not the session bus) */
    app.conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        g_printerr("cannot reach the system bus: %s\n", err->message);
        return 1;
    }
    g_print("[dbus  ] connected to the system bus\n");

    /* 2. export our object tree */
    g_print("[export] building the object tree:\n");
    if (!export_object(APP_PATH,         objmgr_xml,        &objmgr_vtable))        return 1;
    if (!export_object(SERVICE0_PATH,    service_xml,       &service_vtable))       return 1;
    if (!export_object(CHAR_TEMP_PATH,   characteristic_xml,&characteristic_vtable))return 1;
    if (!export_object(CHAR_CMD_PATH,    characteristic_xml,&characteristic_vtable))return 1;
    if (!export_object(CHAR_STATUS_PATH, characteristic_xml,&characteristic_vtable))return 1;
    if (!export_object(CCCD_TEMP_PATH,   descriptor_xml,    &descriptor_vtable))    return 1;
    if (!export_object(ADVERT_PATH,      advertisement_xml, &advertisement_vtable)) return 1;

    /* 3. hand the root to BlueZ */
    register_with_bluez();

    /* 4. drive notifications */
    app.notify_timer_id = g_timeout_add_seconds(2, tick_temperature, NULL);

    /* 5. clean shutdown on Ctrl-C */
    g_unix_signal_add(SIGINT,  on_signal, NULL);
    g_unix_signal_add(SIGTERM, on_signal, NULL);

    g_print("\nRunning. Ctrl-C to stop.\n");
    g_print("Try:  sudo btmon        |  sudo bluetoothctl -> show, advertise on\n\n");

    app.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(app.loop);

    g_main_loop_unref(app.loop);
    g_object_unref(app.conn);
    g_print("[exit  ] done\n");
    return 0;
}
