# ble-gatt-server

A **Bluetooth Low Energy GATT server written in C on BlueZ**, using the D-Bus / GLib API.

No ATT handles are written by hand and no attribute table is built. The application
declares an **object tree over D-Bus**, hands the root to BlueZ via
`GattManager1.RegisterApplication()`, and BlueZ walks that tree through
`org.freedesktop.DBus.ObjectManager.GetManagedObjects()` to construct the attribute
database and assign the handles itself.

---

## What it exposes

```
/com/nancy/ble                        org.freedesktop.DBus.ObjectManager
└── service0                          org.bluez.GattService1
    ├── char0   TEMPERATURE           read + notify
    │   └── desc0                     CCCD, UUID 0x2902
    ├── char1   COMMAND               write
    └── char2   STATUS                read
/com/nancy/ble/advertisement0         org.bluez.LEAdvertisement1
```

**The D-Bus path hierarchy *is* the GATT hierarchy.** Characteristics are children of
their service; descriptors are children of their characteristic. BlueZ infers the
structure from the paths and the `Service` / `Characteristic` properties.

### UUID scheme

```
f1d0XXXX-YYYY-4a5b-9c3d-0e1f2a3b4c5d
     |     |
     |     +-- characteristic index (0000 = the service declaration itself)
     +-------- service index
```

Structured, not random. A `btmon` trace stays readable; a wall of random hex does not.
Custom 128-bit UUIDs are required because this is a proprietary profile — the 16-bit
space is reserved for SIG-adopted profiles (which is why the CCCD is `0x2902`, expanding
against the Bluetooth base UUID `0000xxxx-0000-1000-8000-00805f9b34fb`).

---

## Build and run

```bash
sudo apt install cmake pkg-config libglib2.0-dev bluez
cmake -B build && cmake --build build
sudo ./build/ble-gatt-server
```

In another terminal:

```bash
sudo btmon                    # watch the HCI / MGMT traffic
sudo bluetoothctl             # show, advertise on, devices
```

### Verified output

```
[dbus  ] connected to the system bus
[export] building the object tree:
  exported /com/nancy/ble
  exported /com/nancy/ble/service0
  exported /com/nancy/ble/service0/char0
  exported /com/nancy/ble/service0/char1
  exported /com/nancy/ble/service0/char2
  exported /com/nancy/ble/service0/char0/desc0
  exported /com/nancy/ble/advertisement0

[objmgr] BlueZ called GetManagedObjects — sending the tree
[bluez ] GATT application registered — BlueZ owns the handles now
[bluez ] Advertising as "Nancy-BLE-Lab"
```

---

## The registration sequence

```
1.  g_bus_get_sync(G_BUS_TYPE_SYSTEM)          connect to the system bus
2.  g_dbus_connection_register_object()  x1    the app root (ObjectManager)
3.  g_dbus_connection_register_object()  x1    the service
4.  g_dbus_connection_register_object()  x3    the characteristics
5.  g_dbus_connection_register_object()  x1    the CCCD
6.  g_dbus_connection_register_object()  x1    the advertisement
7.  GattManager1.RegisterApplication(APP_PATH)
        └─► BlueZ calls back: GetManagedObjects()
        └─► BlueZ builds the ATT attribute database and assigns handles
8.  LEAdvertisingManager1.RegisterAdvertisement(ADVERT_PATH)
```

Steps **7 and 8** are the ones that matter. `GattManager1` is where the object tree is
handed over; `ObjectManager` is how BlueZ reads it back.

### What the attribute database looks like afterwards

BlueZ builds this. The application never sees or writes a handle:

```
Handle | Type                 | Value           | Permissions
0x0001 | Primary Service      | f1d00001-0000-… | Read
0x0002 | Characteristic Decl  | TEMPERATURE     | Read
0x0003 | Characteristic Value | {int16 tenths}  | Read, Notify
0x0004 | CCCD (0x2902)        | 0x0000 / 0x0001 | Read, Write
0x0005 | Characteristic Decl  | COMMAND         | Read
0x0006 | Characteristic Value | {command byte}  | Write
0x0007 | Characteristic Decl  | STATUS          | Read
0x0008 | Characteristic Value | {string}        | Read
```

---

## The 31-byte advertising budget — measured, not asserted

Captured with `btmon` while this server registered its advertisement:

```
Add Extended Advertising Parameters — Status: Success
  Available adv data len: 31        Available scan rsp data len: 31

Add Extended Advertising Data
  Advertising data length: 18
    128-bit Service UUIDs (complete): 1 entry
  Scan response length: 15
    Name (complete): Nancy-BLE-Lab
```

A legacy advertising PDU carries **at most 31 bytes** of payload, built from AD structures
of the form `length | AD type | data`.

| Item | Cost |
|---|---|
| 128-bit service UUID | **18 bytes** — 1 length + 1 type + 16 UUID |
| Local name "Nancy-BLE-Lab" | **15 bytes** — 1 length + 1 type + 13 chars |
| Total | **33 bytes — over budget** |

So BlueZ **split them**: the UUID went in the advertisement, the name in the **scan
response**, which gives another 31 bytes to devices that actively scan.

Knowing you cannot fit everything into one packet is what shows you have actually built
one. The trace is in [`docs/hci-trace.txt`](docs/hci-trace.txt).

---

## How notifications actually work

The application never builds an ATT packet.

1. The peer writes `0x0001` to the **CCCD** to subscribe.
2. BlueZ handles that write and calls the characteristic's **`StartNotify`**.
3. When the value changes, the application emits a
   **`org.freedesktop.DBus.Properties.PropertiesChanged`** signal on the characteristic
   object, with `Value` in the changed-properties dictionary.
4. BlueZ sees the signal and, if a peer is subscribed, sends the
   **ATT Handle Value Notification**.

```c
g_dbus_connection_emit_signal(conn, NULL, CHAR_TEMP_PATH,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1",
                      &changed, &invalidated), &err);
```

**Notify vs indicate**: a notification is fire-and-forget; an indication is acknowledged
at the ATT layer, so it is slower but confirmed.

---

## Walking the source

`src/main.c` is one file, in fourteen numbered sections.

| Section | What it covers |
|---|---|
| 1 | Object paths and the structured UUID scheme |
| 2 | Runtime state — temperature, notify flag, registration IDs |
| 3 | **Introspection XML** — the contract with BlueZ. Method names and type signatures must match its API exactly or registration silently does nothing |
| 4 | Helpers — wrapping bytes as `ay`, building `as`, returning 1-tuples |
| 5 | `GattService1` — two properties, no methods |
| 6 | `GattCharacteristic1` — `ReadValue`, `WriteValue`, `StartNotify`, `StopNotify` |
| 7 | `GattDescriptor1` — the CCCD |
| 8 | `LEAdvertisement1` — Type, LocalName, ServiceUUIDs |
| 9 | **`GetManagedObjects`** — building `a{oa{sa{sv}}}`, the whole tree |
| 10 | Notifications via `PropertiesChanged` |
| 11 | `g_dbus_connection_register_object()` for each node |
| 12 | `RegisterApplication` and `RegisterAdvertisement` |
| 13 | Clean shutdown — unregister in reverse order on SIGINT |
| 14 | `main()` |

### The D-Bus type signatures worth knowing

| Signature | Meaning |
|---|---|
| `s` | string · `o` object path · `b` boolean · `ay` byte array |
| `as` | array of strings — used for `Flags` |
| `a{sv}` | dictionary of string → variant — a property map |
| `a{sa{sv}}` | interface name → property map |
| **`a{oa{sa{sv}}}`** | **object path → interface → property map** — the `GetManagedObjects` reply |

That last one looks intimidating and is just three nested dictionaries.

---

## Where this sits in the stack

```
  this application            ← declares D-Bus objects
        │  GLib / GIO D-Bus
        ▼
  bluetoothd (userspace)      ← builds the GATT attribute database
        │  AF_BLUETOOTH sockets: MGMT for adapter control, L2CAP for data
        ▼
  kernel Bluetooth core       ← net/bluetooth: HCI core, L2CAP, SMP
        │
        ▼
  HCI transport driver        ← hci_uart (H4/H5) or btusb
        │
        ▼
  the controller              ← radio, link layer, baseband
```

`btmon` attaches to the kernel's **monitor channel**, which is why it sees all HCI traffic
system-wide rather than only this process's.

---

## Testing without hardware

Developed and verified against a **virtual Bluetooth controller** — BlueZ's `btvirt`
driving the kernel's `hci_vhci` module. No radio required:

```bash
sudo modprobe hci_vhci
sudo btvirt -l -L          # one LE controller;  -l2 gives two, which can connect
```

With `btvirt -l2 -L` two virtual controllers can connect to each other and complete real
SMP pairing, because the emulator implements SMP itself.

---

## Extending it

- **Pairing agent** — implement `org.bluez.Agent1` (`RequestPasskey`,
  `RequestConfirmation`, `AuthorizeService`) and register it with `AgentManager1`
- **Per-characteristic security** — add `encrypt-read` / `encrypt-authenticated-write` to
  the `Flags` array to require pairing
- **Session management** — track connected peers by MAC with an inactivity timeout
- **Beacon scanning** — `Adapter1.SetDiscoveryFilter` + `StartDiscovery`, then watch
  `InterfacesAdded` for `org.bluez.Device1` objects

---

## Reference

- BlueZ D-Bus API: `doc/org.bluez.GattManager.rst`, `GattCharacteristic.rst`,
  `LEAdvertisement.rst` in the BlueZ source tree
- `src/gatt-database.c` in BlueZ — how the attribute database is actually built
- Bluetooth Core Specification — free from bluetooth.com
