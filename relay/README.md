# DOGE Live Feed Relay

Standalone WebSocket relay service that broadcasts live mining events (shares, blocks)
from a Redis pub/sub channel to connected WebSocket clients.

## Architecture

```
foundation-pool ──▶ Redis pub/sub (doge:shares) ──▶ Relay ──WS──▶ clients
```

## Endpoints

- `GET /health` — JSON health info
- `GET /ws` — WebSocket endpoint for clients

## Event Format

```json
{
  "type": "share",
  "ts": 1775500000000,
  "valid": true,
  "address": "DRcdWw...LG3",
  "worker": "miner01",
  "difficulty": 12800000,
  "shareDiff": 83886080,
  "isBlock": false,
  "height": 6147250
}
```

```json
{
  "type": "block",
  "ts": 1775500000000,
  "height": 6147250,
  "hash": "a3f9b7c2...",
  "worker": "DRcdWw...LG3",
  "confirmed": true
}
```

## Configuration

Via `appsettings.json` or env vars with `__` separator:

| Setting | Default | Description |
|---------|---------|-------------|
| `Redis:ConnectionString` | `localhost:6379` | Redis URL |
| `Redis:Channel` | `doge:shares` | Pub/sub channel name |
| `Relay:RingBufferSize` | `100` | Recent events sent to new clients |
| `Relay:MaxClients` | `5000` | Max simultaneous connections |
| `Kestrel:Endpoints:Http:Url` | `http://0.0.0.0:8090` | Listen address |

## Run

```bash
dotnet run
# or
docker build -t doge-relay .
docker run -p 8090:8090 -e Redis__ConnectionString=redis:6379 doge-relay
```

## Client Example

```javascript
const ws = new WebSocket('wss://live.doge-stats.qubic.org/ws');
ws.onmessage = (e) => {
    const event = JSON.parse(e.data);
    console.log(event.type, event);
};
```
