# ble-gatt-server

A **Bluetooth Low Energy GATT server** written in **C**, running on **BlueZ** (the Linux
Bluetooth stack), talking to BlueZ over **D-Bus**.

In plain terms: this program makes a Linux machine advertise itself over Bluetooth and
offer three pieces of data that a phone can read, write and subscribe to.

---

## 1. Project structure

```
ble-gatt-server/
├── README.md            this file
├── CMakeLists.txt       build instructions
├── .gitignore           tells git to ignore build/
├── src/
│   └── main.c           the entire program — 745 lines, 14 sections
└── docs/
    └── hci-trace.txt    a real Bluetooth trace captured while it ran
```

One source file on purpose, so you can read it top to bottom. In production you would
split it — one file per D-Bus interface — but that makes it harder to follow first time.

---

## 2. Build and run it

```bash
sudo apt install cmake pkg-config libglib2.0-dev bluez

cmake -B build            # generate the build files
cmake --build build       # compile
sudo ./build/ble-gatt-server
```

`sudo` is needed because BlueZ lives on the **system** D-Bus, which is privileged.

### What you should see

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

Open a second terminal and run `sudo btmon` to watch the Bluetooth traffic.

---

## 3. What is a GATT server?

Bluetooth LE devices share data as a small set of named values. That collection is called
**GATT** — the Generic Attribute Profile. Three levels:

| Level | What it is | Example here |
|---|---|---|
| **Service** | A group of related values | "Sensor Service" |
| **Characteristic** | One value you can read, write or subscribe to | Temperature |
| **Descriptor** | Extra information about a characteristic | The CCCD, which turns notifications on |

Each one has a **UUID** (a unique ID) and a **handle** (a number the Bluetooth protocol
uses to address it).

**The important part:** this program never creates handles and never builds the attribute
table. It describes what it wants in D-Bus objects, hands that description to BlueZ, and
**BlueZ builds the real table and assigns the handles.**

---

## 4. What this server offers

```
Sensor Service                                  (a service)
├── TEMPERATURE   read + notify                 (a characteristic)
│   └── CCCD                                    (a descriptor)
├── COMMAND       write
└── STATUS        read
```

| Characteristic | What it does |
|---|---|
| **TEMPERATURE** | Returns a temperature. A phone can read it once, or subscribe and get updates every 2 seconds |
| **COMMAND** | A phone writes a byte. `0x01` sets status to RUNNING, `0x02` to PAUSED |
| **STATUS** | Returns the current status as text |

---

## 5. The object tree

The program creates D-Bus **objects**. A D-Bus object has a path, like a file path:

```
/com/nancy/ble                            the application root
├── /service0                             the service
│   ├── /char0                            TEMPERATURE
│   │   └── /desc0                        the CCCD
│   ├── /char1                            COMMAND
│   └── /char2                            STATUS
└── /advertisement0                       the advertisement
```

**The path structure is the GATT structure.** A characteristic sits underneath its
service. A descriptor sits underneath its characteristic. BlueZ reads the tree and
understands the relationships from the paths.

---

## 6. Every field, explained

Each object announces one or more **interfaces**. An interface is a named set of
properties and methods. These names are fixed by BlueZ — you must use them exactly.

### `org.bluez.GattService1` — on the service object

| Property | Type | Meaning | Our value |
|---|---|---|---|
| `UUID` | string | The unique ID of this service | `f1d00001-0000-4a5b-...` |
| `Primary` | boolean | Is this a top-level service? (vs. one included inside another) | `true` |

### `org.bluez.GattCharacteristic1` — on each characteristic object

| Property | Type | Meaning |
|---|---|---|
| `UUID` | string | The unique ID of this characteristic |
| `Service` | object path | Which service owns it — points back to `/service0` |
| `Flags` | array of strings | **What operations are allowed.** See below |
| `Value` | byte array | The current value |
| `Notifying` | boolean | Is a phone currently subscribed? |

**`Flags` values** — this is how you say what a characteristic can do:

| Flag | Meaning |
|---|---|
| `read` | A phone may read it |
| `write` | A phone may write it, and gets a confirmation |
| `write-without-response` | A phone may write it with no confirmation — faster |
| `notify` | The server can push updates; the phone does not have to ask |
| `indicate` | Like notify, but the phone acknowledges each one |
| `encrypt-read` | Reading requires the devices to be paired |

Our three: TEMPERATURE is `["read", "notify"]`, COMMAND is
`["write", "write-without-response"]`, STATUS is `["read"]`.

**Methods** BlueZ calls on a characteristic:

| Method | When BlueZ calls it |
|---|---|
| `ReadValue` | A phone read the characteristic |
| `WriteValue` | A phone wrote to it |
| `StartNotify` | A phone subscribed to updates |
| `StopNotify` | A phone unsubscribed |

### `org.bluez.GattDescriptor1` — on the CCCD object

| Property | Type | Meaning |
|---|---|---|
| `UUID` | string | `00002902-...` — the standard Client Characteristic Configuration Descriptor |
| `Characteristic` | object path | Which characteristic it belongs to |
| `Flags` | array of strings | `["read", "write"]` |

The **CCCD** is the switch a phone uses to turn notifications on. It writes `0x0001` to
subscribe and `0x0000` to unsubscribe.

### `org.bluez.LEAdvertisement1` — on the advertisement object

| Property | Type | Meaning | Our value |
|---|---|---|---|
| `Type` | string | `peripheral` = connectable, `broadcast` = beacon only | `peripheral` |
| `LocalName` | string | The name a phone shows in its scan list | `Nancy-BLE-Lab` |
| `ServiceUUIDs` | array of strings | Which services to advertise, so phones can filter | our service UUID |

### `org.freedesktop.DBus.ObjectManager` — on the application root

One method, `GetManagedObjects`, which returns the whole tree. **This is how BlueZ
discovers everything above.** See section 8.

---

## 7. About the UUIDs

A GATT UUID is **128 bits**, written as `8-4-4-4-12` hex digits.

The Bluetooth SIG publishes short 16-bit UUIDs for standard things. They are shorthand
that expands against a base UUID:

```
0x2902  →  00002902-0000-1000-8000-00805f9b34fb
```

That is why the CCCD in this project is written out in full — it is a standard one.

Our own service and characteristics are **custom**, so they need full 128-bit UUIDs. They
follow a deliberate pattern rather than being random:

```
f1d0XXXX-YYYY-4a5b-9c3d-0e1f2a3b4c5d
     |     |
     |     +--- characteristic number (0000 means the service itself)
     +--------- service number
```

So `f1d00001-0002-...` is service 1, characteristic 2. In a Bluetooth trace you can read
the tree at a glance. Random UUIDs would work equally well but be unreadable.

---

## 8. What happens when you run it

```
STEP 1   Connect to the system D-Bus
STEP 2   Create the 7 objects, each announcing its interfaces
STEP 3   Call GattManager1.RegisterApplication("/com/nancy/ble")
              ↓
STEP 4   BlueZ calls BACK into our program: GetManagedObjects()
              ↓
STEP 5   We return the whole tree as one big dictionary
              ↓
STEP 6   BlueZ builds the attribute table and assigns handles
STEP 7   Call LEAdvertisingManager1.RegisterAdvertisement()
STEP 8   BlueZ starts advertising — a phone can now find us
```

**Steps 3 to 6 are the heart of it.** We do not build a database. We describe a tree,
hand over its root, and BlueZ pulls the details out of us.

### The table BlueZ builds

We never see or write this — it is what BlueZ produces from our tree:

```
Handle | Type                 | Value           | Permissions
0x0001 | Primary Service      | Sensor Service  | Read
0x0002 | Characteristic Decl  | TEMPERATURE     | Read
0x0003 | Characteristic Value | {temperature}   | Read, Notify
0x0004 | CCCD                 | 0x0000 / 0x0001 | Read, Write
0x0005 | Characteristic Decl  | COMMAND         | Read
0x0006 | Characteristic Value | {command byte}  | Write
0x0007 | Characteristic Decl  | STATUS          | Read
0x0008 | Characteristic Value | {status text}   | Read
```

---

## 9. How notifications work

The program never sends a Bluetooth packet directly.

```
1. Phone writes 0x0001 to the CCCD          "subscribe me"
2. BlueZ calls our StartNotify()            we set notifying = true
3. Every 2 seconds our timer fires
4. We emit a PropertiesChanged signal on the characteristic's Value
5. BlueZ sees the signal and sends the actual Bluetooth notification
```

Step 4 in code:

```c
g_dbus_connection_emit_signal(conn, NULL, CHAR_TEMP_PATH,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1",
                      &changed, &invalidated), &err);
```

We announce "this value changed." BlueZ does the Bluetooth part.

---

## 10. The advertising size limit — shown in the real trace

A Bluetooth advertisement can carry **only 31 bytes**. From `docs/hci-trace.txt`, captured
while this program ran:

```
Available adv data len: 31        Available scan rsp data len: 31

Advertising data length: 18
  128-bit Service UUIDs (complete): 1 entry
Scan response length: 15
  Name (complete): Nancy-BLE-Lab
```

| Item | Bytes | Why |
|---|---|---|
| Service UUID | **18** | 1 length + 1 type + 16 for the UUID |
| Name "Nancy-BLE-Lab" | **15** | 1 length + 1 type + 13 characters |
| Total | **33** | over the 31-byte limit |

So BlueZ **split them**: the UUID went into the advertisement, the name into the **scan
response** — a second 31-byte packet sent only to phones that actively ask for more.

This is a real constraint you hit on any BLE device, and the trace shows it happening.

---

## 11. Reading the source

`src/main.c` is in fourteen numbered sections. Read them in order.

| Section | What it is |
|---|---|
| 1 | Object paths and UUIDs |
| 2 | Program state — temperature, notify flag |
| 3 | **Introspection XML** — describes each interface to D-Bus. Names and types must match BlueZ exactly |
| 4 | Small helper functions |
| 5 | The service — its two properties |
| 6 | The characteristics — read, write, notify handlers |
| 7 | The CCCD descriptor |
| 8 | The advertisement |
| 9 | **`GetManagedObjects`** — building the tree BlueZ asks for |
| 10 | Sending notifications |
| 11 | Registering each object on the bus |
| 12 | Handing everything to BlueZ |
| 13 | Clean shutdown on Ctrl-C |
| 14 | `main()` |

**If you only read two, read 9 and 12.** That is where the whole design lives.

### D-Bus type signatures

D-Bus uses short letters for types. You will see these in the XML and in the code:

| Signature | Means |
|---|---|
| `s` | string |
| `o` | object path |
| `b` | boolean |
| `ay` | array of bytes |
| `as` | array of strings |
| `a{sv}` | dictionary: string → any value |
| `a{sa{sv}}` | dictionary: interface name → its properties |
| `a{oa{sa{sv}}}` | dictionary: object path → interfaces → properties |

That last one is the `GetManagedObjects` reply. It looks frightening and is just three
dictionaries inside each other.

---

## 12. Testing without any Bluetooth hardware

Developed and verified against a **virtual Bluetooth controller** — no radio needed:

```bash
sudo modprobe hci_vhci        # kernel virtual HCI device
sudo btvirt -l -L             # BlueZ's controller emulator, one LE controller
```

`btvirt -l2 -L` creates two virtual controllers that can connect to each other and
complete real pairing, because the emulator implements the security protocol itself.

---

## 13. Where this sits in the Linux Bluetooth stack

```
  this program              declares D-Bus objects
        │
        │  D-Bus (GLib / GIO)
        ▼
  bluetoothd                builds the GATT attribute table
        │
        │  kernel sockets — MGMT for control, L2CAP for data
        ▼
  kernel Bluetooth core     net/bluetooth — HCI, L2CAP, security
        │
        ▼
  HCI transport driver      hci_uart (serial) or btusb (USB)
        │
        ▼
  the controller            the radio chip itself
```

`btmon` watches at the kernel level, which is why it sees everything, not just this
program's traffic.

---

## 14. Ideas for extending it

- **Pairing** — implement `org.bluez.Agent1` and register it with `AgentManager1`, so the
  phone must enter a passkey
- **Security per characteristic** — add `encrypt-read` to a characteristic's `Flags` so it
  only works once paired
- **Scanning** — use `Adapter1.StartDiscovery()` and watch for `org.bluez.Device1` objects
  appearing, to find nearby devices
- **Real sensor data** — replace the fake temperature with a reading from an I2C sensor

---

## Reference

- BlueZ D-Bus API docs — `doc/org.bluez.GattManager.rst` and friends in the BlueZ source
- `src/gatt-database.c` in BlueZ — how the attribute table is really built
- Bluetooth Core Specification — free from bluetooth.com
