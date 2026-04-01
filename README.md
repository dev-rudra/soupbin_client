# SoupBinTCP ITCH Client

SoupBinTCP client for receiving binary market data (ITCH protocol) over TCP.
Companion to the MoldUDP64 client — same ITCH decoder and JSON spec format.

## Build

```
make
```

## Config

Edit `config/config.ini`:

```ini
[SOUPBINTCP]
server_ip: 10.68.0.63
server_port: 12100
username: user01
password: pass123456
requested_session:
requested_sequence: 1
heartbeat_interval_sec: 15
protocol_spec: specs/XrossingMD.json
```

- `requested_session`: leave blank for any session
- `requested_sequence`: 1 = start of session, 0 = next available

## Usage

```bash
# Live stream (start from seq 1)
./soupbintcp_client

# Start from specific sequence (re-request / replay)
./soupbintcp_client -s 5000

# Limit to N messages
./soupbintcp_client -n 100

# Filter by message type
./soupbintcp_client --type P --type R

# Verbose (print field names)
./soupbintcp_client -v

# Combine: replay 500 Trade messages from seq 1000
./soupbintcp_client -s 1000 -n 500 --type P -v
```

## Protocol

SoupBinTCP 3.0 — TCP framing with:
- Login / Login Accepted / Login Rejected
- Sequenced Data (carries ITCH messages)
- Client/Server Heartbeats
- End of Session
- Debug packets

Re-request is done via the Login Request `requested_sequence` field —
reconnect with the desired start sequence to replay from that point.
