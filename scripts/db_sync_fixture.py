#!/usr/bin/env python3
"""Small deterministic REST fixture for the positron_db sync contract.

This is a test server, not product code and not a remote SQL gateway.  It
keeps typed row JSON in memory, assigns monotonic row versions, remembers
operation IDs for idempotency, emits tombstones, and exposes enough failure
boundaries for a host worker to test schema, auth, paging and conflicts.
"""

from __future__ import print_function

import argparse
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


MAX_BODY_BYTES = 1024 * 1024
MAX_PUSH = 64
MAX_PULL = 64


class FixtureError(Exception):
    def __init__(self, status, code, message):
        Exception.__init__(self, message)
        self.status = status
        self.code = code
        self.message = message


class SyncState(object):
    def __init__(self, schema_version, schema_hash, token):
        self.schema_version = schema_version
        self.schema_hash = schema_hash
        self.token = token
        self.lock = threading.Lock()
        self.next_version = 0
        self.rows = {}
        self.changes = []
        self.operations = {}

    @staticmethod
    def _key(entity, key):
        return entity + "\x00" + key

    @staticmethod
    def _copy(value):
        return json.loads(json.dumps(value, ensure_ascii=False,
                                     separators=(",", ":")))

    @staticmethod
    def _decimal(value, field):
        if isinstance(value, bool) or not isinstance(value, str):
            raise FixtureError(400, "invalid_field", field + " must be text")
        if not value or (value[0] == "+") or (value[0] == "-" and
                                               len(value) == 1):
            raise FixtureError(400, "invalid_field", field + " is not a version")
        digits = value[1:] if value[0] == "-" else value
        if not digits.isdigit() or int(value) < 0:
            raise FixtureError(400, "invalid_field", field + " is not a version")
        return int(value)

    def _validate_request(self, request):
        if (not isinstance(request, dict) or
                isinstance(request.get("protocol"), bool) or
                request.get("protocol") != 1):
            raise FixtureError(400, "invalid_protocol", "protocol must be 1")
        if not isinstance(request.get("client_id"), str) or not request["client_id"]:
            raise FixtureError(400, "invalid_client", "client_id is required")
        if (isinstance(request.get("schema_version"), bool) or
                request.get("schema_version") != self.schema_version) or \
                request.get("schema_hash") != self.schema_hash:
            raise FixtureError(409, "schema_mismatch", "schema contract differs")
        cursor = self._decimal(request.get("cursor", "0"), "cursor")
        pull_limit = request.get("pull_limit", MAX_PULL)
        if isinstance(pull_limit, bool) or not isinstance(pull_limit, int) or \
                pull_limit <= 0 or pull_limit > MAX_PULL:
            raise FixtureError(400, "invalid_pull_limit", "pull_limit is out of range")
        push = request.get("push", [])
        if not isinstance(push, list) or len(push) > MAX_PUSH:
            raise FixtureError(400, "invalid_push", "push is out of range")
        return cursor, pull_limit, push

    def _row_state(self, entity, key):
        return self.rows.get(self._key(entity, key), {
            "version": "0", "deleted": True, "values": None
        })

    def _new_version(self):
        self.next_version += 1
        return str(self.next_version)

    def _change(self, entity, key, record):
        change = {
            "entity": entity,
            "key": key,
            "version": record["version"],
            "deleted": record["deleted"],
            "values": self._copy(record["values"])
        }
        self.changes.append(change)

    def _validate_operation(self, operation):
        if not isinstance(operation, dict):
            raise FixtureError(400, "invalid_operation", "push item must be an object")
        for field in ("op_id", "entity", "key", "action", "base_version"):
            if not isinstance(operation.get(field), str) or not operation[field]:
                raise FixtureError(400, "invalid_operation", field + " is required")
        if operation["action"] not in ("upsert", "delete"):
            raise FixtureError(400, "invalid_operation", "action is invalid")
        self._decimal(operation["base_version"], "base_version")
        if operation["action"] == "upsert" and not isinstance(
                operation.get("values"), dict):
            raise FixtureError(400, "invalid_operation", "upsert values are required")

    def _operation_fingerprint(self, operation):
        return json.dumps(operation, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":"))

    def _process_push(self, operation, accepted, conflicts):
        self._validate_operation(operation)
        op_id = operation["op_id"]
        fingerprint = self._operation_fingerprint(operation)
        previous = self.operations.get(op_id)
        if previous is not None:
            if previous["fingerprint"] != fingerprint:
                raise FixtureError(409, "op_id_reused", "op_id payload changed")
            if previous["kind"] == "accepted":
                accepted.append(self._copy(previous["result"]))
            else:
                conflicts.append(self._copy(previous["result"]))
            return

        entity = operation["entity"]
        key = operation["key"]
        current = self._row_state(entity, key)
        base_version = self._decimal(operation["base_version"], "base_version")
        current_version = self._decimal(current["version"], "server_version")
        if base_version != current_version:
            result = {
                "op_id": op_id,
                "entity": entity,
                "key": key,
                "server_version": current["version"],
                "deleted": current["deleted"],
                "values": self._copy(current["values"])
            }
            self.operations[op_id] = {
                "fingerprint": fingerprint, "kind": "conflict",
                "result": self._copy(result)
            }
            conflicts.append(result)
            return

        version = self._new_version()
        deleted = operation["action"] == "delete"
        record = {
            "version": version,
            "deleted": deleted,
            "values": None if deleted else self._copy(operation["values"])
        }
        self.rows[self._key(entity, key)] = record
        self._change(entity, key, record)
        result = {"op_id": op_id, "version": version}
        self.operations[op_id] = {
            "fingerprint": fingerprint, "kind": "accepted",
            "result": self._copy(result)
        }
        accepted.append(result)

    def sync(self, request):
        cursor, pull_limit, push = self._validate_request(request)
        accepted = []
        conflicts = []
        with self.lock:
            for operation in push:
                self._process_push(operation, accepted, conflicts)
            available = [change for change in self.changes
                         if int(change["version"]) > cursor]
            changes = available[:pull_limit]
            next_cursor = cursor
            if changes:
                next_cursor = int(changes[-1]["version"])
            response = {
                "schema_version": self.schema_version,
                "schema_hash": self.schema_hash,
                "accepted": accepted,
                "conflicts": conflicts,
                "changes": self._copy(changes),
                "next_cursor": str(next_cursor),
                "has_more": len(available) > len(changes)
            }
            return response


class SyncHandler(BaseHTTPRequestHandler):
    server_version = "PositronDbFixture/1"

    def _send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False,
                          separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/sync":
            self._send_json(404, {"error": "not_found"})
            return
        expected = self.server.state.token
        if expected is not None and self.headers.get("Authorization") != \
                "Bearer " + expected:
            self._send_json(401, {"error": "unauthorized"})
            return
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY_BYTES:
            self._send_json(413, {"error": "body_too_large"})
            return
        raw = self.rfile.read(length)
        if len(raw) != length:
            self._send_json(400, {"error": "truncated_body"})
            return
        try:
            request = json.loads(raw.decode("utf-8"))
            response = self.server.state.sync(request)
        except (UnicodeDecodeError, ValueError):
            self._send_json(400, {"error": "malformed_json"})
            return
        except FixtureError as error:
            self._send_json(error.status, {"error": error.code,
                                            "message": error.message})
            return
        self._send_json(200, response)

    def log_message(self, format_string, *args):
        if not self.server.quiet:
            sys.stderr.write("%s - %s\n" % (self.address_string(),
                                             format_string % args))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--token", default=None)
    parser.add_argument("--schema-version", type=int, default=1)
    parser.add_argument("--schema-hash", default="schema-v1")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    if args.schema_version < 0 or not args.schema_hash:
        parser.error("schema version/hash must be valid")
    server = ThreadingHTTPServer((args.host, args.port), SyncHandler)
    server.state = SyncState(args.schema_version, args.schema_hash,
                              args.token)
    server.quiet = args.quiet
    print("LISTEN %s %d" % (args.host, server.server_address[1]), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
