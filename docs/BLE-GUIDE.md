# Bluetooth Low Energy — a complete guide

This explains BLE from the very beginning. No prior Bluetooth knowledge is needed. Every
term is defined the first time it appears.

Read it in order. Each part builds on the one before.

---

## Contents

**Part 1 — The basics**
1. [What problem BLE solves](#1-what-problem-ble-solves)
2. [The radio](#2-the-radio)
3. [Who talks to whom](#3-who-talks-to-whom)

**Part 2 — Getting connected**
4. [Advertising](#4-advertising)
5. [Scanning](#5-scanning)
6. [Connecting](#6-connecting)
7. [Connection parameters](#7-connection-parameters)

**Part 3 — Exchanging data**
8. [The layer cake](#8-the-layer-cake)
9. [L2CAP](#9-l2cap)
10. [ATT — attributes and handles](#10-att--attributes-and-handles)
11. [GATT — giving it structure](#11-gatt--giving-it-structure)
12. [UUIDs](#12-uuids)
13. [The five ways to move data](#13-the-five-ways-to-move-data)
14. [MTU — how much fits in one message](#14-mtu--how-much-fits-in-one-message)

**Part 4 — Security**
15. [Why radio security is hard](#15-why-radio-security-is-hard)
16. [Pairing, step by step](#16-pairing-step-by-step)
17. [Bonding](#17-bonding)
18. [Privacy and changing addresses](#18-privacy-and-changing-addresses)

**Part 5 — Under the hood**
19. [HCI](#19-hci)
20. [BlueZ on Linux](#20-bluez-on-linux)
21. [Debugging](#21-debugging)

**Part 6 — Putting it together**
22. [A complete walkthrough](#22-a-complete-walkthrough)

**Part 7**
23. [Interview questions and answers](#23-interview-questions-and-answers)

---
---

# Part 1 — The basics

## 1. What problem BLE solves

Imagine a temperature sensor stuck to a wall. It has one small coin cell battery. It needs
to run for two years without anyone touching it, and every few seconds it must tell a phone
what the temperature is.

Classic Bluetooth cannot do this. It keeps a radio link alive all the time, which is fine
for headphones plugged into a battery you charge nightly, but it would flatten a coin cell
in days.

So Bluetooth Low Energy was designed around one idea:

> **Keep the radio switched off almost all of the time.**

That single goal explains nearly every design decision in BLE. When something looks strange
or over-complicated, the reason is usually "so the device can go back to sleep sooner."

### How different is it from Classic Bluetooth?

They share a name and a radio band. Almost nothing else.

| | Classic Bluetooth | Bluetooth Low Energy |
|---|---|---|
| Built for | Continuous streams — audio, files | Small, occasional messages |
| Radio channels | 79, each 1 MHz wide | 40, each 2 MHz wide |
| Connection | Stays awake continuously | Wakes briefly, then sleeps |
| Power while idle | Milliamps | Microamps |
| Speed | 1–3 Mbps | 125 kbps to 2 Mbps |
| Typical devices | Headphones, car stereo | Sensors, watches, beacons, car keys |

A device can support both. Your phone does. That is called **dual-mode**.

**They are not compatible with each other.** A BLE-only sensor cannot talk to a
Classic-only speaker. They are two different protocols that happen to live in the same box.

---

## 2. The radio

BLE uses the 2.4 GHz band — the same crowded space as Wi-Fi and microwave ovens.

It divides that band into **40 channels**, numbered 0 to 39, each 2 MHz wide.

```
Channel:  37        0  1  2 ... 10        38        11 ... 36        39
          ▲                                ▲                          ▲
       advertising                    advertising               advertising
```

Three of those forty are special:

| Channels | Used for |
|---|---|
| **37, 38, 39** | **Advertising** — announcing "I exist" |
| 0 to 36 | **Data** — used once two devices are connected |

### Why exactly those three?

Look at where they sit: 37 is near the bottom of the band, 38 in the middle, 39 at the top.
They are deliberately placed in the gaps between **Wi-Fi channels 1, 6 and 11**, which are
the three Wi-Fi channels almost everyone uses.

So even in an office full of Wi-Fi, at least one advertising channel is usually clear.

### Frequency hopping

Once two devices connect, they stop using the advertising channels and start using the 37
data channels. But they do not stay on one channel. They **hop** — agreeing in advance on a
sequence, and moving to a new channel at every exchange.

This is called **adaptive frequency hopping**. "Adaptive" because if a channel is
consistently bad, they mark it as unusable and skip it.

**Why this matters:** if a microwave oven jams one channel, you lose one message, not the
whole connection. It is the reason BLE works at all in a crowded room.

---

## 3. Who talks to whom

BLE has **two separate sets of roles**. People confuse them constantly, and interviewers
know it. Learn them as two different questions.

### Question 1: who starts the connection?

These are called **GAP roles**. GAP stands for Generic Access Profile — the part of the
specification that deals with finding devices and connecting to them.

| Role | What it does | Typical device |
|---|---|---|
| **Peripheral** | Advertises. Waits. Accepts connections. | The sensor, the watch, the tag |
| **Central** | Scans. Chooses. Starts the connection. | The phone, the gateway |
| **Broadcaster** | Advertises but never accepts connections | A beacon |
| **Observer** | Scans but never connects | A beacon listener |

A useful way to remember it: **the peripheral is usually the small battery-powered thing.**
It advertises because advertising is cheap. The central does the expensive work of scanning.

### Question 2: who holds the data?

These are called **GATT roles**. GATT stands for Generic Attribute Profile — the part that
deals with exchanging data.

| Role | What it does |
|---|---|
| **Server** | **Holds** the data. Answers requests. |
| **Client** | **Asks for** the data. Sends requests. |

### They are independent

This is the part people get wrong.

A peripheral is *usually* a server — the sensor holds the temperature, the phone asks for
it. But it does not have to be. A phone can be a server too, and both sides can be both at
once.

> **The project in this repository is a peripheral and a GATT server.** It advertises, waits
> for a phone to connect, and holds three values the phone can read or write.

---
---

# Part 2 — Getting connected

## 4. Advertising

A peripheral cannot know if anyone is listening. So it simply shouts, at regular intervals,
on the three advertising channels.

Each shout is called an **advertising packet**.

```
      ┌──── 100 ms ────┐┌──── 100 ms ────┐┌──── 100 ms ────┐
      ▼                 ▼                 ▼
   [ADV on 37,38,39] [ADV on 37,38,39] [ADV on 37,38,39]
      │                 │                 │
      └── radio off ────┴── radio off ────┘
```

Between packets the radio is off. That is where the power saving comes from. Advertising
every 100 ms means the radio is on for a fraction of a percent of the time.

### The advertising interval

You can choose anything from **20 ms to 10.24 seconds**.

| Shorter interval | Longer interval |
|---|---|
| Found faster | Found slower |
| More battery used | Less battery used |

A car key fob advertises fast, because you want the door to open immediately. A temperature
sensor in a warehouse can advertise once a second and nobody minds.

> A small random delay of 0 to 10 ms is added to every interval. Without it, two devices
> that happened to start at the same moment would collide on every single packet forever.

### Types of advertising packet

| Type | Can you connect? | Can you ask for more info? | Used for |
|---|---|---|---|
| **ADV_IND** | Yes | Yes | **The normal one.** "I'm here, connect to me" |
| ADV_DIRECT_IND | Yes, one specific device | No | Reconnecting fast to a known device |
| ADV_NONCONN_IND | No | No | A pure beacon — broadcast only |
| ADV_SCAN_IND | No | Yes | A beacon that can give extra data on request |

### What goes inside an advertising packet

Here is the constraint that shapes everything: **an advertising packet can carry only 31
bytes of your data.**

Those 31 bytes are filled with **AD structures**. Each one looks like this:

```
[ length ][ type ][ data.................. ]
   1 byte   1 byte
```

`length` counts the type byte plus the data.

Common types:

| Type value | Meaning |
|---|---|
| `0x01` | Flags — what kind of device this is |
| `0x03` | Complete list of 16-bit service UUIDs |
| `0x07` | Complete list of 128-bit service UUIDs |
| `0x09` | Complete local name |
| `0x0A` | Transmit power level |
| `0x16` | Service data |
| `0xFF` | Manufacturer specific data — how iBeacon works |

### Doing the arithmetic

This is worth working through, because it is a favourite interview question.

Suppose you want to advertise:

- Flags → 1 + 1 + 1 = **3 bytes**
- A 128-bit service UUID → 1 + 1 + 16 = **18 bytes**
- The name "Nancy-BLE-Lab" (13 characters) → 1 + 1 + 13 = **15 bytes**

Total: **36 bytes.** You have 31. It does not fit.

### The scan response — a second packet

The solution is a second 31-byte packet, sent only when someone asks.

```
Peripheral  ──── ADV_IND ────►  Central
Peripheral  ◄─── SCAN_REQ ────  Central     "tell me more"
Peripheral  ──── SCAN_RSP ───►  Central     another 31 bytes
```

A central that only listens is doing a **passive scan** and sees just the advertisement. A
central that asks for more is doing an **active scan** and gets both.

> **This project demonstrates it for real.** In `docs/hci-trace.txt`, captured while the
> server was running:
>
> ```
> Advertising data length: 18     ← the 128-bit UUID
> Scan response length: 15        ← the name "Nancy-BLE-Lab"
> ```
>
> BlueZ worked out that both would not fit and split them automatically.

---

## 5. Scanning

The central does the opposite job. It listens on the advertising channels.

Two numbers control it:

| Setting | Meaning |
|---|---|
| **Scan window** | How long it listens each time |
| **Scan interval** | How often it starts listening |

```
scan interval ──────────────────────►
┌──────────┬─────────────────────────┐
│ listening│      radio off          │
└──────────┴─────────────────────────┘
  ▲ scan window
```

If the window equals the interval, the central listens continuously — fastest discovery,
most power. If the window is small, it saves power but may miss advertisements.

Every advertisement heard is reported upward as an **advertising report**, containing the
sender's address, the data, and the **RSSI**.

### RSSI

**RSSI** means Received Signal Strength Indicator. It is measured in dBm and it is always
negative for BLE.

| RSSI | Meaning |
|---|---|
| −40 dBm | Very close, maybe touching |
| −70 dBm | Same room |
| −90 dBm | Far away, or through walls |

Closer to zero means stronger.

RSSI is a rough distance estimate at best — a wall, a hand, or the device's orientation all
change it. You can use it to filter out distant devices, but never to measure distance
accurately.

> The BLE scanner described in this project's parent work used a **−85 dBm threshold** —
> anything weaker than that was ignored as too far away to be relevant.

---

## 6. Connecting

When a central decides to connect, it sends a **CONNECT_IND** packet in reply to an
advertisement. That packet contains everything needed to set up the link: the hop sequence,
the channel map, the timing.

From that moment:

- Both devices leave the advertising channels
- They begin hopping across the 37 data channels
- They wake up together at agreed moments called **connection events**

```
connection interval
├───────────────┼───────────────┼───────────────┤
▼               ▼               ▼               ▼
[event]         [event]         [event]         [event]
 both wake       both wake       both wake       both wake
 exchange        exchange        exchange        exchange
 sleep           sleep           sleep           sleep
```

**Even with nothing to say, they still wake up.** An empty packet is exchanged just to prove
the link is alive. This is why connection parameters matter so much for battery life.

---

## 7. Connection parameters

Three numbers control the entire behaviour of a connection. Understanding them is one of the
most practically useful things in BLE.

### Connection interval

How often the two devices wake up and talk.

- Range: **7.5 ms to 4 seconds**
- Set in units of 1.25 ms

Short interval = fast response, more battery. Long interval = slow response, less battery.

### Peripheral latency

How many connection events the **peripheral is allowed to skip** when it has nothing to say.

- Range: 0 to 499

This is the clever one. Consider a wireless mouse:

| | Interval | Latency | Result |
|---|---|---|---|
| Without latency | 15 ms | 0 | Responsive but always awake |
| With latency | 15 ms | 30 | **Responds in 15 ms when moving, sleeps ~450 ms when still** |

The mouse can *always* transmit at the next event if it has data. It just does not have to
wake for the ones where it has nothing. You get low latency and low power at the same time.

### Supervision timeout

How long without a single successful exchange before both sides declare the link dead.

- Range: **100 ms to 32 seconds**
- Set in units of 10 ms

### The rule that catches people out

These three cannot be chosen independently:

```
supervision timeout  >  (1 + peripheral latency) × connection interval × 2
```

If you break this rule, here is what happens: the peripheral legitimately skips events using
its latency, no exchange happens for longer than the supervision timeout, and **the link
drops**.

It works perfectly on your desk, where the peripheral always has data. It fails in the field
when the device goes quiet. That is a genuinely nasty bug, and knowing the formula is what
prevents it.

### Who decides?

**Only the central sets these values.** The peripheral can send a **Connection Parameter
Update Request** asking for different ones, and the central is free to refuse.

This is why a sensor may behave differently with an Android phone than an iPhone — the two
central implementations choose different defaults.

---
---

# Part 3 — Exchanging data

## 8. The layer cake

BLE is built in layers. Each layer only talks to the ones directly above and below it.

```
┌────────────────────────────────────────────┐
│  Your application                          │
├────────────────────────────────────────────┤
│  GATT   organises data into services       │  ┐
│  ATT    reads and writes attributes        │  │
│  SMP    security and pairing               │  │  THE HOST
│  GAP    finding and connecting             │  │  (software)
│  L2CAP  multiplexing and fragmentation     │  ┘
├────────────────────────────────────────────┤
│  HCI    the standard interface between     │  ← the boundary
│         software and the chip              │
├────────────────────────────────────────────┤
│  Link Layer   timing, hopping, packets     │  ┐  THE CONTROLLER
│  Physical Layer   the actual radio         │  ┘  (the chip)
└────────────────────────────────────────────┘
```

### Host and controller

**The controller** is the Bluetooth chip. It handles the radio, the precise timing, the
channel hopping. It is usually firmware you never see.

**The host** is software running on your main processor. On Linux that means the kernel's
Bluetooth code plus a program called `bluetoothd`.

**HCI** is the standardised conversation between them. Because it is standardised, any host
software can drive any Bluetooth chip. That is why Linux can talk to a Broadcom chip, an
Intel chip or a Qualcomm chip with the same code.

> **Where does your work sit?** If you write applications, you work at the GATT layer. If you
> write drivers, you work at HCI and below. Knowing which side of the HCI line you have
> worked on is a question you will be asked.

---

## 9. L2CAP

**L2CAP** stands for Logical Link Control and Adaptation Protocol.

It does two jobs.

### Job 1: multiplexing

Several different protocols need to share one radio link. L2CAP gives each one a **channel
identifier**, or CID, so they do not get mixed up.

| CID | Carries |
|---|---|
| **`0x0004`** | **ATT** — all GATT traffic |
| `0x0005` | Signalling — connection parameter update requests |
| **`0x0006`** | **SMP** — pairing |

Think of it as apartment numbers on one building's address. Everything arrives at the same
place; the CID says which door it goes to.

**Every read, write and notification in this project travels on CID `0x0004`.**

### Job 2: fragmentation

The link layer can only carry small packets — 27 bytes originally. If a higher layer wants
to send more, L2CAP splits it into pieces and reassembles them at the far end.

You do not normally see this happening. It matters when you are chasing a throughput
problem.

---

## 10. ATT — attributes and handles

**ATT** stands for Attribute Protocol. It is the layer that actually moves data.

Its model is very simple. Everything a device offers is an **attribute**, and an attribute
has four fields:

| Field | What it is |
|---|---|
| **Handle** | A 16-bit number, `0x0001` to `0xFFFF`. The attribute's address |
| **Type** | A UUID saying what kind of thing this is |
| **Value** | The data itself |
| **Permissions** | Who may read or write it, and whether encryption is required |

All the attributes together form the **attribute database** — a flat table.

```
Handle | Type                 | Value        | Permissions
0x0001 | Primary Service      | Sensor Svc   | Read
0x0002 | Characteristic Decl  | Temperature  | Read
0x0003 | Temperature UUID     | 23.5         | Read, Notify
0x0004 | CCCD                 | 0x0000       | Read, Write
```

**That flat table is all ATT knows about.** It has no concept of services or characteristics
— those come from the next layer up.

### The handle is the address

When a phone wants your temperature, it does not say "give me the temperature." It says
**"read handle 0x0003."**

This is why handles matter, and why discovery exists — the phone has to find out which
handle holds what before it can ask for anything.

> **In this project, you never assign a handle.** You describe your services as D-Bus
> objects and BlueZ builds this table for you, choosing the handle numbers itself.

---

## 11. GATT — giving it structure

A flat table of numbered attributes would be miserable to work with. **GATT** — Generic
Attribute Profile — organises it into a hierarchy.

```
Service                        a group of related things
├── Characteristic             one value
│   ├── Value                  the data
│   └── Descriptor             extra information about it
└── Characteristic
    └── Value
```

An analogy that holds up reasonably well:

| GATT | Like |
|---|---|
| Service | A folder |
| Characteristic | A file in it |
| Descriptor | A property of that file |

### How it maps onto the flat table

This is the part that surprises people. **A characteristic is not one attribute. It is at
least two.**

```
Handle | Type                    | Value
0x0002 | 0x2803 Characteristic   | properties + value handle + UUID    ← the DECLARATION
0x0003 | your characteristic UUID| 23.5                                ← the VALUE
```

The **declaration** at `0x0002` describes the characteristic: what may be done with it,
which handle holds the value, and what its UUID is. The **value** at `0x0003` holds the
actual data.

**This is why handle numbers jump in twos** as you walk a GATT database — and it is why
interviewers ask "how many attributes does one characteristic use?" The answer is two, plus
one for each descriptor.

### The properties byte

Inside the declaration is a byte saying what is allowed:

| Bit | Property | Meaning |
|---|---|---|
| `0x02` | Read | The client may read it |
| `0x04` | Write Without Response | Client may write, no confirmation sent |
| `0x08` | Write | Client may write, confirmation sent |
| `0x10` | **Notify** | Server may push updates, unconfirmed |
| `0x20` | **Indicate** | Server may push updates, confirmed |
| `0x40` | Authenticated Signed Writes | Signed rather than encrypted |

### Descriptors

Extra information attached to a characteristic.

| UUID | Name | Purpose |
|---|---|---|
| **`0x2902`** | **CCCD** | **The subscription switch** |
| `0x2901` | User Description | A human-readable label like "Room temperature" |
| `0x2904` | Presentation Format | Units, decimal exponent, data type |

### The CCCD — read this twice

**CCCD** stands for Client Characteristic Configuration Descriptor. It is by far the most
important descriptor, and it comes up in nearly every BLE interview.

**The problem it solves:** a server may be connected to several clients. When the temperature
changes, which clients want to be told? The server cannot guess.

**The solution:** each client writes to the CCCD to say what it wants.

| Value written | Meaning |
|---|---|
| `0x0001` | Send me notifications |
| `0x0002` | Send me indications |
| `0x0000` | Stop sending |

**The server sends nothing until a client writes that value.**

> This is the single most common cause of "my notifications don't work." The server is
> correct, the client simply never subscribed. Always check the CCCD first.

The subscription is **per client**, and for bonded devices it should survive a disconnection
— the phone expects to still be subscribed when it comes back.

### Discovery

A client that has just connected knows nothing. It has to explore:

```
1. "What services are there?"          → gets service UUIDs and handle ranges
2. "What characteristics in this one?" → gets declarations within that range
3. "What descriptors on this one?"     → finds the CCCD
```

This takes several round trips, which is why the first connection to a device feels slow.
For bonded devices a client should **cache** the results so it does not have to repeat this.

---

## 12. UUIDs

A **UUID** is a Universally Unique Identifier — a number big enough that you can invent one
and be confident nobody else will ever use the same one.

BLE UUIDs are **128 bits**, written as hex in a 8-4-4-4-12 pattern:

```
0000180D-0000-1000-8000-00805F9B34FB
```

Typing that everywhere would be painful, so the Bluetooth SIG — the organisation that owns
the standard — assigns **short 16-bit UUIDs** for standard things.

A 16-bit UUID is shorthand. It expands by slotting into the **Bluetooth Base UUID**:

```
0000xxxx-0000-1000-8000-00805F9B34FB
    ▲
    └── your 16 bits go here
```

So:

| Short | Full | What it is |
|---|---|---|
| `0x180D` | `0000180D-0000-...` | Heart Rate Service |
| `0x2A37` | `00002A37-0000-...` | Heart Rate Measurement |
| `0x2902` | `00002902-0000-...` | CCCD |

### Your own UUIDs must be 128-bit

The 16-bit space belongs to the SIG. If you invent a service, you need a full 128-bit UUID.

The practical cost: **18 bytes of your 31-byte advertisement**, as we worked out earlier.

### Structured versus random

You can generate a random 128-bit UUID, and many people do. But you can also give them a
pattern:

```
f1d0XXXX-YYYY-4a5b-9c3d-0e1f2a3b4c5d
     │      │
     │      └── characteristic number (0000 = the service itself)
     └───────── service number
```

Now `f1d00001-0002-...` is readable at a glance: service 1, characteristic 2.

**This project does that deliberately.** When you are staring at a packet trace, being able
to read the UUIDs saves real time. Random UUIDs work equally well and are unreadable.

---

## 13. The five ways to move data

| Operation | Direction | Confirmed? | When to use it |
|---|---|---|---|
| **Read** | Client asks server | Yes | Fetching a value on demand |
| **Write** | Client to server | Yes | Commands where you need to know it arrived |
| **Write Without Response** | Client to server | **No** | High-rate data where speed matters more |
| **Notify** | **Server to client** | **No** | Streaming sensor updates |
| **Indicate** | **Server to client** | Yes | Rare events that must not be lost |

### Notify versus indicate

This gets asked constantly.

**Notification** is fire and forget. The server sends it and moves on. Fast, and you can
send several back to back.

**Indication** is acknowledged at the ATT layer. The client must confirm before the server
may send another. Slower, but you know it arrived.

> Rule of thumb: **notify** for a stream of readings where losing one does not matter —
> temperature every second. **Indicate** for something that must not be missed — an alarm, a
> configuration change.

### Notifications are not requests

A common misunderstanding: a notification is not a reply to anything. The client subscribes
once by writing the CCCD, then the server sends whenever it likes, unprompted, until the
client unsubscribes or disconnects.

---

## 14. MTU — how much fits in one message

**MTU** stands for Maximum Transmission Unit. In BLE it means the largest ATT message.

**The default is 23 bytes.** Three of those are the ATT header. So:

```
23 − 3 = 20 bytes of actual data per notification
```

Twenty bytes. That is the number to remember.

### Raising it

Both sides can negotiate a bigger MTU with an **MTU Exchange** right after connecting.
Common values are 185 or 247.

But there is a second limit underneath. The **link layer** originally carried only 27-byte
packets. **Data Length Extension**, added in Bluetooth 4.2, raises that to 251.

**You need both.** A large ATT MTU with a small link-layer packet just means L2CAP fragments
your message into many small radio packets, and you gain much less than you expected.

### Sending more than the MTU

If a value is longer than the MTU, the client uses **Read Blob** to fetch it in pieces, or
the server uses a **Prepare Write / Execute Write** sequence for long writes.

---
---

# Part 4 — Security

## 15. Why radio security is hard

Anything you transmit, anyone nearby can receive. There is no cable to tap — they only need
to be in the room.

Three separate problems:

| Problem | Meaning |
|---|---|
| **Eavesdropping** | Someone listens to your data |
| **Man in the middle** | Someone sits between two devices, relaying and altering |
| **Tracking** | Someone follows you by recognising your device's address |

BLE addresses all three: encryption for the first, authentication for the second, and
changing addresses for the third.

---

## 16. Pairing, step by step

**Pairing** is the process of agreeing on encryption keys. It is handled by **SMP** — the
Security Manager Protocol — which runs on L2CAP CID `0x0006`.

It happens in three phases.

### Phase 1 — agreeing what to do

The two devices exchange a **Pairing Request** and **Pairing Response**. They tell each
other:

- **IO capabilities** — can I display a number? can the user type one?
- **Do I want to bond?** — should we save the keys for next time?
- **Do I require MITM protection?** — do I need to be sure who I'm talking to?
- **Do I support Secure Connections?** — the newer, stronger method
- **Which keys will we exchange?**

From the two sets of IO capabilities, both sides work out which pairing method to use. They
always reach the same answer, because the rules are in the specification.

### Phase 2 — creating the keys

Two possible methods.

**LE Legacy Pairing** (Bluetooth 4.0 and 4.1) uses a temporary key to protect the exchange.
It is **vulnerable to passive eavesdropping** — someone recording the pairing can work out
the keys.

**LE Secure Connections** (Bluetooth 4.2 onwards) uses **ECDH** — Elliptic Curve
Diffie-Hellman — with the P-256 curve. Both sides exchange public keys and each computes a
shared secret that an eavesdropper cannot derive from what was transmitted.

**Always require Secure Connections on anything new.**

### Phase 3 — sharing long-term keys

Only if bonding was requested. The devices exchange:

| Key | Purpose |
|---|---|
| **LTK** — Long Term Key | Encrypts future connections |
| **IRK** — Identity Resolving Key | Lets the peer recognise your changing address |
| **CSRK** — Connection Signature Resolving Key | Signs data instead of encrypting it |

### The four pairing methods

Which one is used depends entirely on what the two devices can do.

| Method | How it works | Protects against MITM? |
|---|---|---|
| **Just Works** | No user interaction at all | **No** |
| **Passkey Entry** | One shows a 6-digit number, the other types it | Yes |
| **Numeric Comparison** | Both show a number, user confirms they match | Yes — **Secure Connections only** |
| **Out of Band** | Keys exchanged another way — NFC, QR code | Depends on that channel |

### Why Just Works is not secure

If neither device has a screen or keyboard, there is nothing tying the exchange to the
physical devices in front of you.

An attacker can pair with your sensor, and separately pair with your phone, and sit in the
middle relaying everything. Both ends think they succeeded. Neither can tell.

**Just Works gives you encryption but not authentication.** Your data is hidden from a
passive listener but not from an active attacker.

### IO capabilities

| Capability | Meaning |
|---|---|
| DisplayOnly | Can show a number |
| DisplayYesNo | Can show a number and accept a yes/no |
| KeyboardOnly | Can accept typed input |
| **NoInputNoOutput** | **Neither — forces Just Works** |
| KeyboardDisplay | Both |

> The project's parent work used a **static passkey** with an agent — the device holds a
> fixed 6-digit number the user types on the phone. Simple, and it gives MITM protection
> without needing a screen.

### Security levels

| Level | Meaning |
|---|---|
| 1 | No security at all |
| 2 | Encrypted, but unauthenticated (Just Works) |
| 3 | Encrypted and authenticated (MITM protected) |
| 4 | Authenticated **Secure Connections** with a 128-bit key |

You can require a level **per characteristic**. That is what flags like `encrypt-read` mean
— the client must have paired before it may read that value.

### When pairing fails

SMP reports a reason code. You will see these in a packet trace.

| Code | Meaning |
|---|---|
| `0x01` | Passkey Entry Failed |
| `0x02` | OOB data not available |
| **`0x03`** | **Authentication Requirements not met** — the two sides could not agree on a method |
| `0x04` | Confirm Value Failed — usually a wrong passkey |
| `0x05` | Pairing Not Supported |
| `0x06` | Encryption key size too small |
| **`0x09`** | **Repeated Attempts** — locked out after too many failures |
| `0x0B` | DHKey Check Failed |
| `0x0C` | Numeric Comparison Failed |

`0x03` is the one you meet most. It usually means one device demanded MITM protection while
the other's IO capabilities made that impossible.

---

## 17. Bonding

**Pairing** creates keys for this session. **Bonding** stores them.

Once bonded:

- The next connection encrypts immediately using the saved LTK
- No passkey, no user interaction
- Much faster, since you skip the whole pairing exchange

This is why your earbuds connect silently after the first time.

### Managing the bond table

Devices have limited storage, so a bond table has a maximum size. When it is full and a new
device wants to bond, something must be removed.

A common policy is **LRU — Least Recently Used**: evict whichever bond has gone longest
without connecting.

### Losing a bond

If one side deletes the bond and the other does not, connections fail confusingly — one side
tries to encrypt with a key the other no longer has. The fix is to remove the bond on both
sides and pair again. This is exactly what "forget this device" does on a phone.

---

## 18. Privacy and changing addresses

Every BLE device has an address, like a MAC address. If it never changed, anyone could track
you: your phone shouting the same identifier every 100 ms is a beacon following you around.

### Address types

| Type | Behaviour |
|---|---|
| Public | A real, permanent, registered address |
| Random Static | Fixed until the device reboots |
| **Resolvable Private (RPA)** | **Changes roughly every 15 minutes** |
| Non-resolvable Private | Random, meaningless to everyone |

### How an RPA works

The address is generated from the **IRK** plus a random number.

```
RPA = [ random part (24 bits) ][ hash of (IRK, random part) (24 bits) ]
```

A device holding your IRK can recompute the hash and confirm "yes, this is her." Anyone
without the IRK sees an address that keeps changing and can make nothing of it.

**This is why the IRK is exchanged during bonding.** Without it, your phone could not
recognise your own earbuds after their address rotated.

---
---

# Part 5 — Under the hood

## 19. HCI

**HCI** stands for Host Controller Interface — the standardised conversation between the
software and the Bluetooth chip.

### Four kinds of packet

| Type | Direction | Contains |
|---|---|---|
| **Command** | Host → Controller | "Start advertising", "Connect to this address" |
| **Event** | Controller → Host | "Connection complete", "Advertising report" |
| **ACL Data** | Both ways | Ordinary data — your GATT traffic |
| **ISO Data** | Both ways | LE Audio streams |

### How it physically connects

| Transport | Linux driver | Where you find it |
|---|---|---|
| **H4** | `hci_uart` | Plain UART, 4 wires. Most embedded boards |
| **H5** | `hci_uart` | UART with retransmission, for unreliable lines |
| **USB** | `btusb` | Dongles, laptops |
| SDIO | `btsdio` | Some combined Wi-Fi/Bluetooth chips |

> On a Raspberry Pi the Bluetooth chip is attached over UART, so `hci_uart` is the driver
> underneath everything. That makes a Pi a good place to read real driver code.

### Events worth recognising

| Event | Tells you |
|---|---|
| LE Connection Complete | Success or failure, connection handle, role, peer address |
| **Disconnection Complete** | **A reason code** — see below |
| Encryption Change | The link is now encrypted |
| LE Advertising Report | Someone was heard advertising |

### Disconnect reason codes

These are the first thing to look at when a connection drops.

| Code | Meaning | What it usually indicates |
|---|---|---|
| **`0x08`** | **Connection Timeout** | The link died — out of range, interference, or a bad supervision timeout |
| **`0x13`** | **Remote User Terminated** | The other device chose to disconnect |
| `0x16` | Local Host Terminated | Your own side disconnected |
| `0x3E` | Failed to Establish | The connection never completed |

**`0x08` versus `0x13` is the most useful distinction in BLE debugging.** One is a fault,
the other is normal behaviour.

---

## 20. BlueZ on Linux

**BlueZ** is the official Linux Bluetooth stack. It has two halves.

```
  your application
        │  D-Bus
        ▼
  bluetoothd                 userspace daemon — GATT database, profiles, pairing
        │  AF_BLUETOOTH sockets
        ▼
  kernel Bluetooth core      net/bluetooth — HCI, L2CAP, SMP
        │
        ▼
  hci_uart or btusb          the transport driver
        │
        ▼
  the Bluetooth chip
```

### Talking to it from your program

You do not call a BlueZ library. You talk to `bluetoothd` over **D-Bus**, a message bus that
lets programs call each other's methods.

**Interfaces BlueZ gives you — you call these:**

| Interface | Where | What you call |
|---|---|---|
| **`org.bluez.GattManager1`** | `/org/bluez/hci0` | **`RegisterApplication()`** |
| `org.bluez.LEAdvertisingManager1` | `/org/bluez/hci0` | `RegisterAdvertisement()` |
| `org.bluez.AgentManager1` | `/org/bluez` | `RegisterAgent()` |
| `org.bluez.Adapter1` | `/org/bluez/hci0` | `StartDiscovery()`, power on/off |
| `org.bluez.Device1` | per remote device | `Connect()`, `Pair()` |

**Interfaces you give BlueZ — it calls these:**

| Interface | Purpose |
|---|---|
| **`org.freedesktop.DBus.ObjectManager`** | **`GetManagedObjects()` — how BlueZ discovers your tree** |
| `org.bluez.GattService1` | Describes a service |
| `org.bluez.GattCharacteristic1` | ReadValue, WriteValue, StartNotify, StopNotify |
| `org.bluez.GattDescriptor1` | Descriptors including the CCCD |
| `org.bluez.LEAdvertisement1` | What to advertise |
| `org.bluez.Agent1` | Pairing — RequestPasskey, RequestConfirmation |

### The registration sequence

This is the heart of writing a GATT server on BlueZ:

```
1. Connect to the SYSTEM D-Bus       (not the session bus — BlueZ is not there)
2. Create a D-Bus object for each service, characteristic and descriptor
3. Create the application root, implementing ObjectManager
4. Call GattManager1.RegisterApplication(root_path)
        ↓
5. BlueZ calls back into YOUR program: GetManagedObjects()
        ↓
6. You return the whole tree in one reply
        ↓
7. BlueZ builds the ATT attribute database and assigns all the handles
8. Call LEAdvertisingManager1.RegisterAdvertisement(advert_path)
```

**Steps 4 to 7 are the answer to "how did you register your handlers without a database?"**

You do not build a database. You describe a tree of objects, hand over the root, and BlueZ
comes back and reads it out of you.

### Sending a notification on BlueZ

You never construct an ATT packet. You emit a D-Bus signal:

```c
g_dbus_connection_emit_signal(conn, NULL, characteristic_path,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1",
                      &changed, &invalidated), &err);
```

You are saying "this value changed." BlueZ sees the signal, checks which clients subscribed
via the CCCD, and sends them the real notification.

---

## 21. Debugging

| Tool | What it does |
|---|---|
| **`btmon`** | **Shows all HCI traffic, decoded.** The single most useful tool |
| `bluetoothctl` | Interactive — scan, pair, connect, browse attributes |
| `hciconfig -a` | Adapter state, address, supported features |
| `btmgmt` | Adapter management — power, advertising, bonding |
| `dmesg` | Driver-level messages from `hci_uart` or `btusb` |
| **nRF Connect** | **A phone app** — inspect your server from the client side |

### Reading a btmon trace

```
>       host sending to controller (a command)
<       controller sending to host (an event)
@       management (MGMT) traffic
ATT:    a GATT operation
SMP:    pairing
```

A typical debugging sequence:

1. Does an **LE Connection Complete** appear? If not, it never connected.
2. What is the **Disconnection Complete** reason code? `0x08` is a dead link; `0x13` is
   deliberate.
3. Is there an **Encryption Change**? If not, pairing did not finish.
4. Any **SMP failure code**? `0x03` means the two sides could not agree on a method.
5. Are there **ATT** operations at all? If not, the client never discovered your services.

### btmon versus an air sniffer

This distinction matters and is often asked.

**`btmon` shows the HCI boundary** — everything between your host software and your
controller. It cannot see what is actually transmitted.

**An air sniffer captures the radio itself.** It sees the peer's transmissions,
retransmissions, link-layer control packets, channel map updates — things your own
controller never reports to you.

You need an air sniffer when the *other* device is misbehaving, or for interoperability and
timing problems.

Tools: Ellisys, Teledyne LeCroy/Frontline, TI SmartRF, and the free **Nordic nRF Sniffer**
with an nRF52840 dongle feeding Wireshark.

---
---

# Part 6 — Putting it together

## 22. A complete walkthrough

Everything above, in order, as one story. A phone finds a temperature sensor, connects,
subscribes, and receives a reading.

### Step 1 — the sensor advertises

The sensor's application registers its GATT tree with BlueZ, then registers an
advertisement. BlueZ tells the controller to start advertising.

Every 100 ms, on channels 37, 38 and 39, the sensor transmits an `ADV_IND` packet containing
its service UUID. The name did not fit, so it went into the scan response.

Between packets, the radio is off.

### Step 2 — the phone scans

The phone listens on the advertising channels. It hears the packet and reports it: address,
data, RSSI −67 dBm.

Because it is doing an active scan, it also sends a `SCAN_REQ`, and the sensor replies with
`SCAN_RSP` carrying the name. The phone now shows "Nancy-BLE-Lab" in its list.

### Step 3 — connecting

The user taps the device. The phone sends `CONNECT_IND` with the connection parameters —
say a 50 ms interval, latency 0, supervision timeout 5 seconds.

Both sides leave the advertising channels and start hopping across the data channels. An
**LE Connection Complete** event appears in `btmon`.

### Step 4 — the MTU exchange

The phone immediately asks for a bigger MTU. The sensor agrees to 185. Now a notification
can carry 182 bytes instead of 20.

### Step 5 — discovery

The phone knows nothing yet, so it explores:

- "What primary services do you have?" → the Sensor Service, handles `0x0001` to `0x0008`
- "What characteristics are in that range?" → Temperature at value handle `0x0003`, and its
  properties say Read and Notify
- "What descriptors does that characteristic have?" → a CCCD at `0x0004`

This takes several round trips. The phone caches the result.

### Step 6 — a read

The phone sends an **ATT Read Request** for handle `0x0003`.

BlueZ receives it, sees the handle belongs to the application's characteristic object, and
calls **`ReadValue`** on it over D-Bus. The application returns two bytes: `0xEB 0x00` —
235 in little-endian, meaning 23.5 °C.

BlueZ wraps that in an **ATT Read Response** and sends it. All of it travelled on L2CAP CID
`0x0004`.

### Step 7 — subscribing

The phone wants updates, so it writes `0x0001` to the CCCD at handle `0x0004`.

BlueZ handles the write and calls **`StartNotify`** on the application's characteristic. The
application sets its "notifying" flag.

### Step 8 — a notification

Two seconds later the temperature changes. The application emits a **`PropertiesChanged`**
signal on the characteristic's `Value` property.

BlueZ sees it, checks that a client subscribed, and sends an **ATT Handle Value
Notification** for handle `0x0003`. No acknowledgement is expected.

### Step 9 — pairing

The user tries to read a protected characteristic. Its permissions require encryption, so
BlueZ starts pairing on L2CAP CID `0x0006`.

Feature exchange, then key generation with ECDH, then key distribution — LTK, IRK, CSRK.
The user types the sensor's static passkey. An **Encryption Change** event appears, and the
link is now encrypted.

Because bonding was requested, both sides store the keys.

### Step 10 — disconnecting

The user walks away. No packet is successfully exchanged for longer than the supervision
timeout, so both sides declare the link dead.

`btmon` shows a **Disconnection Complete** with reason **`0x08`** — connection timeout.

The sensor's controller starts advertising again automatically, and the whole cycle can
begin once more. Next time, because they are bonded, there will be no passkey.

---
---

# Part 7

## 23. Interview questions and answers

### The basics

**Q: What is BLE and how does it differ from Classic Bluetooth?**
BLE is designed for devices that send small amounts of data occasionally and must run for a
long time on a tiny battery. Classic keeps a link continuously active for streaming, which
is why it carries audio. BLE has 40 channels of 2 MHz with three reserved for advertising,
and is built around connecting briefly and sleeping. Classic uses milliamps; BLE microamps
between events. They are separate protocols that share a band, and they are not compatible.

**Q: Why are there three advertising channels?**
Channels 37, 38 and 39 are positioned in the gaps between Wi-Fi channels 1, 6 and 11, which
are the ones most Wi-Fi networks use. So in a busy environment at least one is usually
clear.

**Q: Explain the GAP and GATT roles.**
GAP roles are about connecting — a peripheral advertises and accepts, a central scans and
initiates. GATT roles are about data — a server holds it, a client requests it. They are
independent. A peripheral is usually a server but does not have to be.

### Advertising and connecting

**Q: How much data fits in an advertising packet?**
31 bytes, made of AD structures of length, type and data. A 128-bit UUID costs 18 of them,
so you run out quickly. If you need more, the scan response gives you another 31 bytes,
which an actively scanning central will request.

**Q: Explain connection interval, peripheral latency and supervision timeout.**
The interval is how often the two devices wake and exchange, from 7.5 ms to 4 seconds.
Peripheral latency is how many of those events the peripheral may skip when it has nothing
to send, which gives you low latency when active and low power when idle. Supervision
timeout is how long without a successful exchange before the link is declared dead. The
constraint is that the timeout must exceed one plus the latency, times the interval, times
two — otherwise the link drops as soon as the peripheral uses its latency.

**Q: Who sets the connection parameters?**
The central. The peripheral can request a change through a connection parameter update
request, but the central may refuse. That is why the same peripheral behaves differently
with different phones.

### GATT

**Q: What is a service, a characteristic and a descriptor?**
A service groups related characteristics. A characteristic is one value plus properties
saying what may be done with it. A descriptor is extra information about a characteristic,
most importantly the CCCD.

**Q: How many attributes does one characteristic occupy?**
At least two — a declaration attribute holding the properties, the value handle and the
UUID, plus the value attribute itself. Then one more for each descriptor. That is why
handles advance in twos as you walk a database.

**Q: What is the CCCD and why does it exist?**
The Client Characteristic Configuration Descriptor, UUID `0x2902`. A client writes `0x0001`
to subscribe to notifications or `0x0002` for indications. It exists because a server may
have several clients and cannot guess which want updates. If notifications are not arriving,
the CCCD is the first thing I check.

**Q: Notify versus indicate?**
A notification is unacknowledged and fast. An indication is confirmed at the ATT layer, and
only one may be outstanding at a time. Notify for streaming sensor data; indicate when
delivery must be certain.

**Q: What is the default MTU and why does it matter?**
23 bytes, of which 3 are the ATT header, leaving 20 bytes of payload. You can negotiate
higher with an MTU exchange, but you also need Data Length Extension at the link layer,
which raises the packet from 27 to 251 bytes. Raising one without the other gains you little
because L2CAP just fragments.

**Q: How does a client find out what a server offers?**
It discovers primary services to get their handle ranges, then discovers characteristics
within each range, then descriptors on each characteristic. It is several round trips, which
is why first connections are slow and why clients cache the result for bonded devices.

### Security

**Q: Walk me through pairing.**
Three phases. First a feature exchange — IO capabilities, whether bonding is wanted, whether
MITM protection is required, and whether Secure Connections is supported. Second, key
generation: Legacy pairing derives a short-term key, while LE Secure Connections uses ECDH
on P-256 to produce a long-term key. Third, if bonding was requested, key distribution — the
LTK for encryption, the IRK for resolving private addresses, and the CSRK for signing.

**Q: What are the four association models?**
Just Works, Passkey Entry, Numeric Comparison and Out of Band. The two devices' IO
capabilities determine which is used. Just Works gives no MITM protection because nothing
ties the exchange to the physical devices. Numeric Comparison requires Secure Connections.

**Q: Why is Just Works insecure?**
It encrypts but does not authenticate. An attacker can pair separately with each side and
relay between them, and neither end can tell. You get protection from passive listening but
not from an active man in the middle.

**Q: Legacy pairing versus LE Secure Connections?**
Legacy is vulnerable to passive eavesdropping — someone recording the pairing can derive the
keys. Secure Connections, from Bluetooth 4.2, uses ECDH key agreement and adds Numeric
Comparison. New designs should require it.

**Q: Pairing versus bonding?**
Pairing establishes keys for the current session. Bonding stores them so the next connection
encrypts immediately without any user interaction.

**Q: What is a Resolvable Private Address?**
An address that changes roughly every fifteen minutes so the device cannot be tracked. It is
generated from the IRK plus a random value, and a bonded peer holding the IRK can resolve it
back to the real identity. That is why the IRK is distributed during bonding.

### Under the hood

**Q: What is HCI and why does it exist?**
The Host Controller Interface — a standardised protocol of commands, events and data between
the host software and the Bluetooth controller. Because it is standardised, any host stack
can drive any controller. Transports are H4 over plain UART, H5 for unreliable lines, USB
and SDIO.

**Q: How does BlueZ connect to the hardware?**
The application talks D-Bus to `bluetoothd`. `bluetoothd` talks to the kernel over
`AF_BLUETOOTH` sockets — the management channel for adapter control and L2CAP for data. The
kernel Bluetooth core implements HCI, L2CAP and SMP. Below that the transport driver,
`hci_uart` or `btusb`, talks to the chip.

**Q: How do you register a GATT server with BlueZ?**
Export a D-Bus object for each service, characteristic and descriptor, with the application
root implementing `org.freedesktop.DBus.ObjectManager`. Then call
`GattManager1.RegisterApplication()` with the root path. BlueZ calls back with
`GetManagedObjects()`, reads the whole tree, and builds the ATT attribute database and the
handles itself. The application never assigns a handle.

**Q: Does your device have its own database?**
Not a SQL database. The database is the GATT attribute table, and on BlueZ it lives in
`bluetoothd`. I declare the structure as D-Bus objects and BlueZ builds the table from it.
On MCU stacks like Zephyr or Nordic's it works differently — there you declare a static
attribute array and the stack assigns handles at registration time.

**Q: How do you send a notification on BlueZ?**
Emit a `PropertiesChanged` signal on the characteristic object with the new value. BlueZ
sees it, checks which clients subscribed via the CCCD, and sends the ATT notification. The
application never builds a packet.

**Q: What is L2CAP for?**
Two things. Multiplexing — several protocols share one link, each with a channel identifier.
ATT uses `0x0004`, SMP uses `0x0006`, signalling uses `0x0005`. And fragmentation — splitting
larger packets to fit the link layer and reassembling at the far end.

### Debugging

**Q: How would you debug a BLE problem?**
Start with `btmon`. Check whether an LE Connection Complete appeared at all. If it
disconnected, read the reason code — `0x08` means the link died from range, interference or
a bad supervision timeout, while `0x13` means the peer chose to leave. Check for an
Encryption Change to confirm pairing completed, and for an SMP failure code if it did not —
`0x03` is an authentication requirements mismatch. Then check whether any ATT operations
occurred, which tells you if discovery succeeded.

**Q: My notifications are not arriving. What do you check?**
First the CCCD — has the client actually written `0x0001` to subscribe? That is the most
common cause by a wide margin. Then whether the characteristic's properties include notify.
Then whether the server is emitting an update at all. Then the trace, to see whether the
notification leaves the controller.

**Q: Have you used an air sniffer?**
Be honest either way. `btmon` shows the HCI boundary — my host and my controller. An air
sniffer captures the radio itself, so it also shows the peer's transmissions,
retransmissions and link-layer control packets. You need one when the other device is
misbehaving, or for interoperability and timing problems. The tools are Ellisys, Frontline,
TI SmartRF, and the free Nordic nRF Sniffer with an nRF52840 dongle into Wireshark.

---

## Where to go next

- **[The project in this repository](../README.md)** — every concept above, in working C
- **Nordic DevAcademy, BLE Fundamentals** — free and well paced
- **The Bluetooth Core Specification** — free from bluetooth.com. Dense, but definitive
- **BlueZ source**, `src/gatt-database.c` — how the attribute table is really built
