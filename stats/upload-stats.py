#!/usr/bin/env python3
"""
Upload dispatcher stats to Cloudflare KV.
Reads the local stats JSON file and pushes it to Cloudflare KV
using the Cloudflare API. Runs in a loop.
"""

import json
import time
import os
import requests
from datetime import datetime

# ============================================================
# CONFIGURATION - edit these values
# ============================================================
STATS_FILE = "/tmp/dispatcher-stats.json"
ACCOUNT_ID = "your-cloudflare-account-id"
NAMESPACE_ID = "your-kv-namespace-id"
API_TOKEN = "your-cloudflare-api-token"
KV_KEY = "DOGE_STATS"
INTERVAL_SECONDS = 10
# ============================================================


def upload_to_kv(stats_json, account_id, namespace_id, api_token, kv_key):
    """Upload JSON string to Cloudflare KV via API."""
    url = f"https://api.cloudflare.com/client/v4/accounts/{account_id}/storage/kv/namespaces/{namespace_id}/values/{kv_key}"
    headers = {
        "Authorization": f"Bearer {api_token}",
        "Content-Type": "application/json",
    }
    resp = requests.put(url, headers=headers, data=stats_json, timeout=10)
    return resp.ok, resp.status_code, resp.text


def main():
    print(f"Uploading {STATS_FILE} -> KV {KV_KEY} every {INTERVAL_SECONDS}s")
    print(f"Account: {ACCOUNT_ID[:8]}... | Namespace: {NAMESPACE_ID[:8]}...")

    last_mtime = 0
    consecutive_errors = 0

    while True:
        try:
            # Only upload if the file changed
            mtime = os.path.getmtime(STATS_FILE)
            if mtime == last_mtime:
                time.sleep(INTERVAL_SECONDS)
                continue
            last_mtime = mtime

            with open(STATS_FILE) as f:
                stats_json = f.read()

            # Validate JSON
            json.loads(stats_json)

            ok, status, body = upload_to_kv(stats_json, ACCOUNT_ID, NAMESPACE_ID, API_TOKEN, KV_KEY)
            ts = datetime.now().strftime("%H:%M:%S")

            if ok:
                consecutive_errors = 0
                print(f"[{ts}] Uploaded ({len(stats_json)}B)")
            else:
                consecutive_errors += 1
                print(f"[{ts}] Upload failed: HTTP {status} - {body[:200]}")

        except FileNotFoundError:
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"[{ts}] Stats file not found: {STATS_FILE}")
        except json.JSONDecodeError:
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"[{ts}] Invalid JSON in stats file")
        except requests.RequestException as e:
            consecutive_errors += 1
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"[{ts}] Network error: {e}")
        except KeyboardInterrupt:
            print("\nStopped.")
            break
        except Exception as e:
            consecutive_errors += 1
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"[{ts}] Error: {e}")

        time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
