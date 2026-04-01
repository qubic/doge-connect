#!/usr/bin/env python3
"""
Upload dispatcher + pool stats to Cloudflare KV.
Reads the local dispatcher stats file and fetches pool stats via HTTP,
then pushes both to Cloudflare KV. Runs in a loop.
"""

import json
import time
import os
import requests
from datetime import datetime

# ============================================================
# CONFIGURATION - edit these values
# ============================================================
ACCOUNT_ID = "your-cloudflare-account-id"
NAMESPACE_ID = "your-kv-namespace-id"
API_TOKEN = "your-cloudflare-api-token"
INTERVAL_SECONDS = 10

# Dispatcher stats (local JSON file written by the dispatcher)
DISPATCHER_STATS_FILE = "/tmp/dispatcher-stats.json"
DISPATCHER_KV_KEY = "DOGE_STATS"

# Pool stats (HTTP endpoint from foundation-v2-dogecoin)
POOL_STATS_URL = "http://localhost:8080/stats"
POOL_KV_KEY = "DOGE_POOL"
# ============================================================


def upload_to_kv(data, account_id, namespace_id, api_token, kv_key):
    """Upload JSON string to Cloudflare KV via API."""
    url = f"https://api.cloudflare.com/client/v4/accounts/{account_id}/storage/kv/namespaces/{namespace_id}/values/{kv_key}"
    headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": "application/json",
    }
    resp = requests.put(url, headers=headers, data=data, timeout=10)
    return resp.ok, resp.status_code, resp.text


def upload_dispatcher_stats(last_mtime):
    """Upload dispatcher stats from local file. Returns new mtime."""
    try:
        mtime = os.path.getmtime(DISPATCHER_STATS_FILE)
        if mtime == last_mtime:
            return last_mtime  # file unchanged

        with open(DISPATCHER_STATS_FILE) as f:
            stats_json = f.read()
        json.loads(stats_json)  # validate

        ok, status, body = upload_to_kv(stats_json, ACCOUNT_ID, NAMESPACE_ID, API_TOKEN, DISPATCHER_KV_KEY)
        ts = datetime.now().strftime("%H:%M:%S")

        if ok:
            print(f"[{ts}] Dispatcher: uploaded ({len(stats_json)}B)")
        else:
            print(f"[{ts}] Dispatcher: upload failed HTTP {status} - {body[:200]}")

        return mtime

    except FileNotFoundError:
        pass  # silent if dispatcher not running
    except json.JSONDecodeError:
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] Dispatcher: invalid JSON in stats file")
    except requests.RequestException as e:
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] Dispatcher: network error: {e}")

    return last_mtime


def upload_pool_stats(last_hash):
    """Fetch pool stats via HTTP, strip workers, upload to KV. Returns new hash."""
    try:
        resp = requests.get(POOL_STATS_URL, timeout=5)
        if not resp.ok:
            return last_hash

        data = resp.json()

        # Remove workers for privacy
        data.pop("workers", None)

        stats_json = json.dumps(data)

        # Only upload if changed
        data_hash = hash(stats_json)
        if data_hash == last_hash:
            return last_hash

        ok, status, body = upload_to_kv(stats_json, ACCOUNT_ID, NAMESPACE_ID, API_TOKEN, POOL_KV_KEY)
        ts = datetime.now().strftime("%H:%M:%S")

        if ok:
            print(f"[{ts}] Pool: uploaded ({len(stats_json)}B)")
        else:
            print(f"[{ts}] Pool: upload failed HTTP {status} - {body[:200]}")

        return data_hash

    except requests.RequestException:
        pass  # silent if pool not running
    except Exception as e:
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] Pool: error: {e}")

    return last_hash


def main():
    print(f"Uploading stats to Cloudflare KV every {INTERVAL_SECONDS}s")
    print(f"  Dispatcher: {DISPATCHER_STATS_FILE} -> {DISPATCHER_KV_KEY}")
    print(f"  Pool:       {POOL_STATS_URL} -> {POOL_KV_KEY}")
    print(f"  Account: {ACCOUNT_ID[:8]}... | Namespace: {NAMESPACE_ID[:8]}...")

    last_dispatcher_mtime = 0
    last_pool_hash = 0

    while True:
        try:
            last_dispatcher_mtime = upload_dispatcher_stats(last_dispatcher_mtime)
            last_pool_hash = upload_pool_stats(last_pool_hash)
        except KeyboardInterrupt:
            print("\nStopped.")
            break
        except Exception as e:
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"[{ts}] Error: {e}")

        time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
