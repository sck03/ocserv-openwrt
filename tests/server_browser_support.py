"""Build coherent v3 summary/list replies for browser fault injection."""
import hashlib
import json


def online_payload(payload):
    data = dict(payload['data'])
    rows = data.get('online', []) if data.get('running') else []
    raw = json.dumps([rows, bool(data.get('online_error'))], sort_keys=True).encode()
    data.update(online=rows, online_count=len(rows),
                online_generation=hashlib.sha256(raw).hexdigest()[:32] if data.get('running') else 'stopped')
    return dict(payload, data=data)


def summary_payload(payload):
    result = online_payload(payload)
    result['data'] = {key: value for key, value in result['data'].items()
                      if key in ('running', 'online_count', 'online_generation', 'online_error', 'guard', 'recovery_pending')}
    return result
