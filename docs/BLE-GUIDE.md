# Bluetooth Low Energy — from zero to interview-ready

Written to be read on its own. No prior Bluetooth knowledge assumed. By the end you should
be able to answer the questions in the last section without looking anything up.

The [main project](../README.md) is the worked example for everything here.

---

## Contents

1. [What BLE is](#1-what-ble-is)
2. [The radio](#2-the-radio)
3. [Roles](#3-roles)
4. [The protocol stack](#4-the-protocol-stack)
5. [Advertising](#5-advertising)
6. [Connections](#6-connections)
7. [ATT — the attribute protocol](#7-att--the-attribute-protocol)
8. [GATT — services and characteristics](#8-gatt--services-and-characteristics)
9. [UUIDs](#9-uuids)
10. [Security — pairing and bonding](#10-security--pairing-and-bonding)
11. [Privacy and addresses](#11-privacy-and-addresses)
12. [HCI — where host meets controller](#12-hci--where-host-meets-controller)
13. [BlueZ on Linux](#13-bluez-on-linux)
14. [Debugging](#14-debugging)
15. [Interview questions and answers](#15-interview-questions-and-answers)

---

## 1. What BLE is

Bluetooth Low Energy is a **short-range wireless protocol designed for devices that send
small amounts of data infrequently and must run for months on a coin cell**.

It shares a name and a frequency band with classic Bluetooth and almost nothing else. They
are separate protocols with separate radios' worth of rules.

| | Bluetooth Classic (BR/EDR) | Bluetooth Low Energy |
|---|---|---|
| Designed for | Continuous streams — audio, file transfer | Small, occasional exchanges |
| Channels | 79, each 1 MHz | 40, each 2 MHz |
| Connection | Stays up, constantly active | Connect briefly, then sleep |
| Power | Milliamps | Microamps between events |
| Typical use | Headphones, car audio | Sensors, wearables, beacons, keys |
| Data rate | 1–3 Mbps | 125 kbps – 2 Mbps |

A device can support both — a phone does. That is called **dual-mode**.

**The core idea behind BLE's power saving:** the radio is off almost all the time. A
peripheral wakes briefly, sends or receives, and sleeps again. Everything in the protocol —
advertising intervals, connection intervals, peripheral latency — exists to let a device
stay asleep longer.

---

## 2. The radio

BLE uses the 2.4 GHz ISM band, divided into **40 channels of 2 MHz**, numbered 0–39.

| Channels | Purpose |
|---|---|
| **37, 38, 39** | **Advertising** — deliberately spaced to dodge the busiest Wi-Fi channels (1, 6, 11) |
| 0–36 | **Data** — used once connected |

Once two devices connect they use **adaptive frequency hopping**: they agree a hop sequence
and a channel map, changing channel on every connection event. This is why BLE survives in a
crowded 2.4 GHz room — a jammed channel costs one event, not the link.

---

## 3. Roles

BLE has two independent sets of roles. Confusing them is a common interview stumble.

### GAP roles — about connections

| Role | Does |
|---|---|
| **Peripheral** | Advertises, accepts connections. Usually the small device — a sensor |
| **Central** | Scans, initiates connections. Usually the phone or gateway |
| **Broadcaster** | Advertises only, never connects — a beacon |
| **Observer** | Scans only, never connects |

### GATT roles — about data

| Role | Does |
|---|---|
| **Server** | **Holds the data.** Responds to reads and writes |
| **Client** | **Requests the data.** Sends reads and writes |

**They are independent.** A peripheral is usually a GATT server, but a phone (central) can
also be a server, and both sides can be both. This project is a **peripheral and a GATT
server**.

---

## 4. The protocol stack

```
┌──────────────────────────────────────────┐
│  Application / Profiles                  │  your code
├──────────────────────────────────────────┤
│  GATT      generic attribute profile     │  ┐
│  ATT       attribute protocol            │  │
│  SMP       security manager              │  │  HOST
│  GAP       discovery, connection         │  │
│  L2CAP     multiplexing, fragmentation   │  ┘
├──────────────────────────────────────────┤
│  HCI       host controller interface     │  the boundary
├──────────────────────────────────────────┤
│  Link Layer                              │  ┐  CONTROLLER
│  Physical Layer (radio)                  │  ┘
└──────────────────────────────────────────┘
```

**The host** is software — on Linux that is the kernel's Bluetooth core plus the
`bluetoothd` daemon. **The controller** is the chip: radio, link layer, timing.

**HCI is the standard interface between them**, which is why any host stack can drive any
controller.

### L2CAP

The multiplexing layer. Everything above it rides on an L2CAP channel, identified by a CID:

| CID | Carries |
|---|---|
| `0x0004` | **ATT** — all GATT traffic |
| `0x0005` | LE signalling — connection parameter updates |
| `0x0006` | **SMP** — pairing |

L2CAP also fragments large packets to fit the link layer and reassembles them.

---

## 5. Advertising

A peripheral announces itself by broadcasting on the three advertising channels at a fixed
interval.

### Advertising PDU types

| Type | Connectable | Scannable | Use |
|---|---|---|---|
| **`ADV_IND`** | Yes | Yes | The normal one — "I'm here, connect to me" |
| `ADV_DIRECT_IND` | Yes, one peer | No | Fast reconnect to a known device |
| `ADV_NONCONN_IND` | No | No | Pure beacon |
| `ADV_SCAN_IND` | No | Yes | Beacon with extra data on request |

### The 31-byte limit

An advertising payload is **at most 31 bytes**, built from **AD structures**:

```
[ length | AD type | data ][ length | AD type | data ]...
```

`length` counts the AD type byte plus the data.

| AD type | Meaning |
|---|---|
| `0x01` | Flags — discoverable mode, BR/EDR support |
| `0x02` / `0x03` | 16-bit service UUIDs, incomplete / complete |
| `0x06` / `0x07` | 128-bit service UUIDs |
| `0x08` / `0x09` | Local name, shortened / complete |
| `0x0A` | TX power level |
| `0x16` | Service data |
| `0x19` | Appearance |
| `0xFF` | Manufacturer specific data — how iBeacon and Eddystone work |

**A 128-bit UUID costs 18 bytes** (1 length + 1 type + 16 UUID). Add Flags at 3 bytes and a
13-character name at 15 and you are at 36 — over budget.

### Scan response

The solution: a **second 31-byte packet**. A scanning central sends `SCAN_REQ`, and the
peripheral replies with `SCAN_RSP` carrying more AD structures. Only devices actively
scanning pay the cost.

**This project demonstrates it live** — see [the captured trace](hci-trace.txt): the service
UUID (18 bytes) went in the advertisement, the name (15 bytes) in the scan response.

### Advertising interval

20 ms to 10.24 s. Shorter means faster discovery and more power. A random delay of 0–10 ms
is added to each interval so two devices don't collide forever.

---

## 6. Connections

The central sends `CONNECT_IND` and the link is up. Three parameters then govern everything:

| Parameter | Range | Meaning |
|---|---|---|
| **Connection interval** | 7.5 ms – 4 s (units of 1.25 ms) | How often the two devices wake and talk |
| **Peripheral latency** | 0 – 499 | How many connection events the peripheral may **skip** if it has nothing to say |
| **Supervision timeout** | 100 ms – 32 s (units of 10 ms) | How long without a successful exchange before declaring the link dead |

**The trade-off:** a short interval gives low latency and high throughput but drains
battery. Peripheral latency gives you both — a mouse can respond in 15 ms when moving and
skip 30 events when still.

**The constraint you must satisfy:**

```
supervision_timeout > (1 + peripheral_latency) × connection_interval × 2
```

Break it and the link drops as soon as the peripheral exercises its latency. Getting this
wrong is a classic field bug: works on the bench, disconnects in the wild.

Only the **central** sets these. A peripheral can request a change via an
L2CAP Connection Parameter Update Request, and the central may refuse.

---

## 7. ATT — the attribute protocol

Everything a GATT server exposes is an **attribute**:

| Field | Meaning |
|---|---|
| **Handle** | 16-bit address, `0x0001`–`0xFFFF`. How the client refers to it |
| **Type** | A UUID saying what kind of attribute it is |
| **Value** | The data |
| **Permissions** | Read/write, and what security is required |

The whole set is the **attribute database** — a flat table. GATT is a set of conventions
layered on top of it.

### ATT operations

| Operation | Direction | Acknowledged |
|---|---|---|
| Read Request / Response | client → server | yes |
| Write Request / Response | client → server | yes |
| **Write Command** | client → server | **no** — "write without response" |
| **Notification** | server → client | **no** |
| **Indication / Confirmation** | server → client | **yes** |
| Read Blob | client → server | yes — for values longer than the MTU |

**Notify vs indicate** is a standard question: notification is fire-and-forget and fast;
indication is confirmed at the ATT layer, so it is slower but you know it arrived. Only one
indication may be outstanding at a time.

### MTU

The default **ATT MTU is 23 bytes**. Three go to the ATT header, so **a notification carries
20 bytes of payload**.

Both sides can negotiate more with an MTU Exchange — commonly 185 or 247. Below that, the
link layer has its own limit; **Data Length Extension** (Bluetooth 4.2+) raises the link-layer
PDU from 27 to 251 bytes. For real throughput you need both.

---

## 8. GATT — services and characteristics

GATT organises the flat attribute table into a hierarchy.

```
Service
├── Characteristic
│   ├── Value
│   └── Descriptor(s)
└── Characteristic
    └── Value
```

### How it actually looks in the table

A characteristic is **two attributes**, not one:

```
Handle | Type                     | Value
0x0001 | 0x2800 Primary Service   | <service UUID>
0x0002 | 0x2803 Characteristic    | properties | value handle | <char UUID>
0x0003 | <char UUID>              | the actual data          ← the value
0x0004 | 0x2902 CCCD              | 0x0000 or 0x0001
```

The **declaration** (`0x2803`) describes the characteristic; the **value** attribute holds
the data. That is why handles jump in twos, and why a characteristic has both a declaration
handle and a value handle.

### Characteristic properties

The properties byte in the declaration:

| Bit | Property | Meaning |
|---|---|---|
| `0x02` | Read | Client may read |
| `0x04` | Write Without Response | Write, unacknowledged |
| `0x08` | Write | Write, acknowledged |
| `0x10` | **Notify** | Server may push, unacknowledged |
| `0x20` | **Indicate** | Server may push, acknowledged |
| `0x40` | Authenticated Signed Writes | Signed with the CSRK |

### Descriptors

| UUID | Name | Purpose |
|---|---|---|
| **`0x2902`** | **CCCD** — Client Characteristic Configuration | **The subscribe switch** |
| `0x2901` | Characteristic User Description | Human-readable label |
| `0x2904` | Characteristic Presentation Format | Units, exponent, type |

**The CCCD is how subscription works** and it comes up constantly. The client writes:

- `0x0001` → enable notifications
- `0x0002` → enable indications
- `0x0000` → disable

The server sends nothing until the client writes that value. **A server that "isn't sending
notifications" has usually not had its CCCD written.** It is per-client and, for bonded
devices, must persist across reconnections.

### Discovery

A client that knows nothing walks the database: read by group type for services, read by
type for characteristics within each service's handle range, find information for
descriptors. This is why first connections are slow, and why **caching** the handles for a
bonded peer matters.

---

## 9. UUIDs

Every attribute type is a UUID — **128 bits**, written `8-4-4-4-12` in hex.

The Bluetooth SIG assigns short **16-bit UUIDs** for standard things. They are shorthand
that expands against the **Bluetooth Base UUID**:

```
0000xxxx-0000-1000-8000-00805F9B34FB

0x180D  →  0000180D-0000-1000-8000-00805F9B34FB   (Heart Rate Service)
0x2A37  →  00002A37-0000-1000-8000-00805F9B34FB   (Heart Rate Measurement)
0x2902  →  00002902-0000-1000-8000-00805F9B34FB   (CCCD)
```

**Your own services must use full 128-bit UUIDs** — the 16-bit space belongs to the SIG.
The practical cost is the 18 bytes in an advertisement.

---

## 10. Security — pairing and bonding

**Pairing** = establishing keys for this session.
**Bonding** = storing them so you don't repeat it next time.

### The three phases

```
Phase 1   Pairing Feature Exchange
          IO capabilities, OOB availability, AuthReq
          (bonding wanted? MITM required? Secure Connections?),
          max key size, which keys to distribute

Phase 2   Key generation
          LE Legacy       → TK → STK
          LE Secure Conn  → ECDH P-256 exchange → DHKey → LTK

Phase 3   Key distribution (only if bonding)
          LTK   long-term key, for encryption
          IRK   identity resolving key, to resolve private addresses
          CSRK  signing key, for signed writes
```

### Association models — how the method is chosen

The two devices' **IO capabilities** decide it:

| IO capability | Can do |
|---|---|
| DisplayOnly | Show a number |
| DisplayYesNo | Show a number, confirm |
| KeyboardOnly | Enter a number |
| NoInputNoOutput | Neither |
| KeyboardDisplay | Both |

| Model | When | MITM protected |
|---|---|---|
| **Just Works** | One side has no IO | **No** |
| **Passkey Entry** | One displays, other enters a 6-digit passkey | Yes |
| **Numeric Comparison** | Both display 6 digits, user confirms they match | Yes — **LE Secure Connections only** |
| **Out of Band** | Keys exchanged over NFC, QR, etc. | Depends on the OOB channel |

**MITM = man in the middle.** Just Works has no protection because nothing ties the exchange
to the physical devices — an attacker in the middle can pair with both.

### Legacy vs LE Secure Connections

| | LE Legacy (4.0/4.1) | LE Secure Connections (4.2+) |
|---|---|---|
| Key agreement | TK-based, weak | **ECDH P-256** |
| Passive eavesdropping | **Vulnerable** | Protected |
| Numeric Comparison | Not available | Available |

Always require Secure Connections on new designs.

### Security levels (LE Security Mode 1)

| Level | Meaning |
|---|---|
| 1 | No security |
| 2 | Encryption, unauthenticated (Just Works) |
| 3 | Encryption, authenticated (MITM protection) |
| 4 | Authenticated **LE Secure Connections**, 128-bit key |

You require these per characteristic — that is what `encrypt-read` and
`encrypt-authenticated-write` mean in the BlueZ flags.

### SMP failure codes

You will read these in a `btmon` trace when pairing fails:

| Code | Meaning |
|---|---|
| `0x01` | Passkey Entry Failed |
| `0x02` | OOB Not Available |
| **`0x03`** | **Authentication Requirements not met** — the two sides couldn't agree on a method |
| `0x04` | Confirm Value Failed — wrong passkey |
| `0x05` | Pairing Not Supported |
| `0x06` | Encryption Key Size too small |
| **`0x09`** | **Repeated Attempts** — locked out after too many failures |
| `0x0B` | DHKey Check Failed |
| `0x0C` | Numeric Comparison Failed |

---

## 11. Privacy and addresses

A fixed MAC broadcast every 100 ms is a tracking beacon. BLE solves this with
**Resolvable Private Addresses**.

| Address type | Meaning |
|---|---|
| Public | A real IEEE-assigned MAC, permanent |
| Random Static | Fixed until reboot |
| **Resolvable Private (RPA)** | **Changes every ~15 minutes** |
| Non-resolvable Private | Random, resolvable by nobody |

An RPA is generated from the **IRK** plus a random number. A bonded peer holding your IRK
can resolve it back to your identity; anyone else sees an address that keeps changing.

**This is why the IRK is exchanged during bonding.** Without it your phone couldn't
recognise your own earbuds after their address rotated.

---

## 12. HCI — where host meets controller

Four packet types cross the HCI boundary:

| Type | Direction |
|---|---|
| **Command** | Host → Controller |
| **Event** | Controller → Host |
| **ACL Data** | Both — normal data |
| **ISO Data** | Both — LE Audio isochronous streams |

### Transports

| Transport | Driver | Where |
|---|---|---|
| **H4** | `hci_uart` | Plain UART, 4 wires. Most embedded SoCs |
| **H5 (3-wire)** | `hci_uart` | UART with retransmission, for unreliable lines |
| **USB** | `btusb` | Dongles, laptops |
| SDIO | `btsdio` | Some combo Wi-Fi/BT chips |

### Events worth recognising

| Event | Tells you |
|---|---|
| LE Connection Complete | Status, handle, role, peer address |
| **Disconnection Complete** | **Reason code** — `0x08` supervision timeout, `0x13` remote terminated, `0x16` local terminated, `0x3E` failed to establish |
| Encryption Change | Link is now encrypted |
| LE Advertising Report | A scan result |

`0x08` versus `0x13` is worth knowing instantly: `0x08` means the link died — range,
interference, or a bad supervision timeout. `0x13` means the peer chose to disconnect.

---

## 13. BlueZ on Linux

```
  your application            declares D-Bus objects
        │  D-Bus (GLib/GIO)
        ▼
  bluetoothd                  userspace daemon — GATT database, profiles
        │  AF_BLUETOOTH sockets — MGMT for control, L2CAP for data
        ▼
  kernel Bluetooth core       net/bluetooth — HCI, L2CAP, SMP
        │
        ▼
  hci_uart / btusb            HCI transport driver
        │
        ▼
  the controller
```

### The D-Bus API

**Interfaces BlueZ provides — you call these:**

| Interface | On | Key method |
|---|---|---|
| `org.bluez.GattManager1` | `/org/bluez/hci0` | **`RegisterApplication()`** |
| `org.bluez.LEAdvertisingManager1` | `/org/bluez/hci0` | `RegisterAdvertisement()` |
| `org.bluez.AgentManager1` | `/org/bluez` | `RegisterAgent()` |
| `org.bluez.Adapter1` | `/org/bluez/hci0` | `StartDiscovery()`, `Powered` |
| `org.bluez.Device1` | per remote device | `Connect()`, `Pair()` |

**Interfaces you provide — BlueZ calls these:**

| Interface | Purpose |
|---|---|
| **`org.freedesktop.DBus.ObjectManager`** | **`GetManagedObjects()` — how BlueZ learns your tree** |
| `org.bluez.GattService1` | UUID, Primary |
| `org.bluez.GattCharacteristic1` | ReadValue, WriteValue, StartNotify, StopNotify |
| `org.bluez.GattDescriptor1` | The descriptors |
| `org.bluez.LEAdvertisement1` | Type, LocalName, ServiceUUIDs |
| `org.bluez.Agent1` | RequestPasskey, RequestConfirmation |

### The registration flow

```
1. Connect to the SYSTEM bus (BlueZ is not on the session bus)
2. Register a D-Bus object for each service, characteristic, descriptor
3. Register the app root, implementing ObjectManager
4. GattManager1.RegisterApplication(root_path)
       ↓
5. BlueZ calls YOUR GetManagedObjects()
       ↓
6. BlueZ builds the ATT attribute database and assigns handles
7. LEAdvertisingManager1.RegisterAdvertisement(advert_path)
```

**You never write a handle.** That is the single most important thing to understand about
BlueZ, and the [project in this repo](../README.md) does exactly this.

### Notifications on BlueZ

You do not send ATT packets. You emit a
`org.freedesktop.DBus.Properties.PropertiesChanged` signal on the characteristic object with
the new `Value`, and BlueZ turns it into an ATT notification for subscribed clients.

---

## 14. Debugging

| Tool | What it gives you |
|---|---|
| **`btmon`** | **The HCI trace** — every command, event, ATT and SMP operation, decoded. The single most useful tool |
| `bluetoothctl` | Interactive — scan, pair, connect, list attributes |
| `hciconfig -a` | Adapter state, address, features |
| `btmgmt` | Adapter management — power, advertising, bonding |
| `nRF Connect` | Phone app — inspect a GATT server from the client side |
| `dmesg` | Driver-level messages from `hci_uart` / `btusb` |

### Reading a btmon trace

```
>   host to controller (command)
<   controller to host (event)
@   management (MGMT) command or event
ATT:  a GATT operation
SMP:  a pairing operation
```

### btmon vs an air sniffer

`btmon` shows everything crossing the **HCI boundary** — your host and your controller. It
cannot show what is actually on the air: the peer's transmissions, retransmissions,
link-layer control PDUs, channel map updates.

For that you need an **air sniffer** — Ellisys, Teledyne LeCroy/Frontline, TI SmartRF, or
the free Nordic nRF Sniffer with an nRF52840 dongle feeding Wireshark. You reach for one
when the *other* device is misbehaving, or for interop and timing problems.

---

## 15. Interview questions and answers

### Fundamentals

**Q: Bluetooth Classic vs BLE?**
Different protocols sharing a band. Classic uses 79 × 1 MHz channels and keeps a link
continuously active for streaming — that's why A2DP audio runs on it. BLE uses 40 × 2 MHz
channels, three of them for advertising, and is built around connecting briefly and
sleeping. Classic burns milliamps; BLE microamps between events.

**Q: Describe the BLE stack.**
Bottom up: physical layer and link layer in the controller. HCI is the standard boundary.
Above it the host — L2CAP for multiplexing, then ATT, GATT, SMP and GAP. Then profiles and
your application. HCI's existence is why any host stack can drive any controller.

**Q: GAP vs GATT?**
GAP is discovery and connection — advertising, scanning, roles. GATT is data organisation —
services containing characteristics containing descriptors. GAP gets you connected; GATT is
what you do once you are.

**Q: Peripheral vs central, server vs client?**
Peripheral/central are GAP roles about who advertises and who initiates. Server/client are
GATT roles about who holds the data. They are independent — a peripheral is usually a server
but doesn't have to be.

### Advertising

**Q: How much data fits in an advertisement, and what if you need more?**
31 bytes, in AD structures of `length | type | data`. A 128-bit UUID alone costs 18 of them.
If you need more you use the scan response — a second 31 bytes sent when an active scanner
issues `SCAN_REQ`. Beyond that, Bluetooth 5 extended advertising on the data channels.

**Q: What's in an advertising packet?**
Flags, service UUIDs, local name, TX power, manufacturer-specific data. `ADV_IND` is the
normal connectable undirected type; `ADV_NONCONN_IND` is a pure beacon.

### Connections

**Q: Explain connection interval, peripheral latency and supervision timeout.**
Interval is how often the two devices wake and exchange — 7.5 ms to 4 s. Peripheral latency
is how many events the peripheral may skip when it has nothing to say, which is how a device
gets both low latency when active and low power when idle. Supervision timeout is how long
without a successful exchange before the link is declared dead. The constraint is
`timeout > (1 + latency) × interval × 2` — violate it and the link drops the moment the
peripheral uses its latency.

**Q: What is the ATT MTU and why does it matter?**
Default 23 bytes, of which 3 are ATT header — so 20 bytes of payload per notification.
You negotiate higher with an MTU exchange, and pair it with Data Length Extension at the
link layer (27 → 251 byte PDUs) for real throughput.

### GATT

**Q: Service, characteristic, descriptor?**
A service groups related characteristics. A characteristic is a single value with properties
saying what may be done to it. A descriptor is metadata about a characteristic — most
importantly the CCCD.

**Q: What is the CCCD and why does it exist?**
Client Characteristic Configuration Descriptor, UUID `0x2902`. A client writes `0x0001` to
subscribe to notifications or `0x0002` for indications. It exists because subscription is
per-client — the server must know who wants updates. If notifications aren't arriving, the
CCCD is the first thing to check.

**Q: Notify vs indicate?**
Notification is unacknowledged and fast. Indication is confirmed at ATT level, so you know it
arrived, but only one may be outstanding at a time. Use notify for streaming sensor data,
indicate when delivery must be certain.

**Q: How many attributes does one characteristic use?**
At least two — a declaration attribute (`0x2803`) holding the properties, value handle and
UUID, and the value attribute itself. Plus one per descriptor. That is why handles advance in
twos.

**Q: How does a client discover what a server offers?**
Read by group type for primary services, then read by type within each service's handle
range for characteristics, then find information for descriptors. It is slow, which is why
handle caching for bonded devices matters.

### Security

**Q: Walk me through pairing.**
Three phases. Feature exchange — IO capabilities, whether bonding is wanted, MITM
requirement, Secure Connections support. Key generation — Legacy uses a TK to derive an STK;
LE Secure Connections does ECDH P-256 to produce an LTK. Then key distribution if bonding:
LTK for encryption, IRK for address resolution, CSRK for signing.

**Q: What are the association models?**
Just Works, Passkey Entry, Numeric Comparison and Out of Band. The two devices' IO
capabilities decide which is used. Just Works has no MITM protection because nothing binds
the exchange to the physical devices. Numeric Comparison requires Secure Connections.

**Q: Pairing vs bonding?**
Pairing establishes keys for the session. Bonding stores them so the next connection can
encrypt immediately without repeating the process.

**Q: What is a Resolvable Private Address?**
A periodically changing address so a device can't be tracked by its MAC. Generated from the
IRK plus a random value; a bonded peer holding the IRK can resolve it back to the real
identity. That is why the IRK is distributed during bonding.

**Q: Legacy pairing vs LE Secure Connections?**
Legacy is vulnerable to passive eavesdropping. Secure Connections (4.2+) uses ECDH P-256 key
agreement and adds Numeric Comparison. Always require it on new designs.

### Linux and BlueZ

**Q: How does BlueZ connect to the hardware?**
Your application talks D-Bus to `bluetoothd`. `bluetoothd` talks to the kernel over
`AF_BLUETOOTH` sockets — the MGMT channel for adapter control, L2CAP for data. The kernel
Bluetooth core implements HCI, L2CAP and SMP. Below that the HCI transport driver —
`hci_uart` for UART-attached controllers, `btusb` for USB — talks to the chip.

**Q: How do you register a GATT server with BlueZ?**
Export D-Bus objects for the service, characteristics and descriptors, with the app root
implementing `org.freedesktop.DBus.ObjectManager`. Then call
`GattManager1.RegisterApplication()` with the root path. BlueZ calls back into your process
with `GetManagedObjects()`, enumerates the tree, and builds the ATT attribute database
itself. You never assign a handle.

**Q: Does your device have its own database?**
Not a SQL database — the "database" is the GATT attribute table, and on BlueZ it is
maintained in `bluetoothd`. I declare the structure as D-Bus objects and BlueZ builds the
table from it. On MCU stacks like Zephyr or Nordic's it is different — there you declare a
static attribute array and the stack assigns handles at registration.

**Q: How do you send a notification on BlueZ?**
Emit a `PropertiesChanged` signal on the characteristic object with the new `Value`. BlueZ
sees it and sends the ATT notification to any subscribed client. You never build the packet.

### Debugging

**Q: How do you debug a BLE problem?**
Start with `btmon` to see the HCI trace — whether the connection completed, what the
disconnect reason was, whether encryption was enabled, and any SMP failure code. `0x08` on a
disconnect is supervision timeout, meaning the link died; `0x13` means the peer chose to
leave. For pairing failures the SMP reason code tells you directly — `0x03` is an
authentication-requirements mismatch. If the problem is on the air rather than in my stack, I
would need an air sniffer, since `btmon` only shows what crosses HCI.

**Q: Have you used an air sniffer?**
Be honest. Explain the distinction: `btmon` shows the HCI boundary — my host and my
controller. An air sniffer captures the RF link itself, including the peer's transmissions,
retransmissions and link-layer control PDUs. Tools are Ellisys, Frontline, TI SmartRF, and
the free Nordic nRF Sniffer with an nRF52840 dongle into Wireshark.

---

## Where to go next

- **The Bluetooth Core Specification** — free from bluetooth.com. Dense, but definitive
- **Nordic DevAcademy — BLE Fundamentals** — free, well-paced
- **BlueZ source** — `src/gatt-database.c` shows how the attribute table is really built
- **[This project](../README.md)** — every concept above, in working C
