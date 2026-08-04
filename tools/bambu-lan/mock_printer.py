#!/usr/bin/env python3
"""A fake Bambu printer for the LAN backend self test.

Speaks the three protocols the real machine does, well enough to prove the
client end to end on a build host:

  * MQTT 3.1.1 over TLS (default :8883)   - CONNECT/CONNACK with access-code
                                            checking, SUBSCRIBE/SUBACK, a pushed
                                            report, PINGREQ/PINGRESP, and it
                                            records everything published to it
  * FTP over implicit TLS (default :990)  - USER/PASS/PBSZ/PROT/TYPE/PASV/STOR
  * SSDP announcements (default :2021)    - the NOTIFY a printer broadcasts

Everything it receives is written under --state-dir for the C++ side to assert
against. Not a general purpose broker: it implements what this client sends.
"""

import argparse
import json
import os
import socket
import ssl
import struct
import sys
import threading
import time

STOP = threading.Event()


def log(msg):
    sys.stderr.write("[mock] %s\n" % msg)
    sys.stderr.flush()


# ---------------------------------------------------------------------------
# MQTT
# ---------------------------------------------------------------------------

CONNECT, CONNACK, PUBLISH, PUBACK = 1, 2, 3, 4
SUBSCRIBE, SUBACK, PINGREQ, PINGRESP, DISCONNECT = 8, 9, 12, 13, 14


def encode_varint(value):
    out = bytearray()
    while True:
        byte = value % 128
        value //= 128
        if value > 0:
            byte |= 0x80
        out.append(byte)
        if value == 0:
            return bytes(out)


def decode_varint(data, offset):
    multiplier = 1
    value = 0
    for i in range(4):
        if offset + i >= len(data):
            return None, None
        byte = data[offset + i]
        value += (byte & 0x7F) * multiplier
        if not byte & 0x80:
            return value, offset + i + 1
        multiplier *= 128
    raise ValueError("malformed remaining length")


def read_string(data, offset):
    (length,) = struct.unpack_from("!H", data, offset)
    offset += 2
    return data[offset:offset + length].decode("utf-8", "replace"), offset + length


class MqttServer(threading.Thread):
    def __init__(self, port, access_code, state_dir, ctx, report_payload):
        super().__init__(daemon=True)
        self.port = port
        self.access_code = access_code
        self.state_dir = state_dir
        self.ctx = ctx
        self.report_payload = report_payload
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(8)
        self.sock.settimeout(0.5)

    def run(self):
        while not STOP.is_set():
            try:
                conn, addr = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn, addr), daemon=True).start()
        self.sock.close()

    def _record(self, name, obj):
        path = os.path.join(self.state_dir, name)
        with open(path, "a") as fh:
            fh.write(json.dumps(obj) + "\n")

    def _serve(self, conn, addr):
        try:
            if self.ctx is not None:
                conn = self.ctx.wrap_socket(conn, server_side=True)
            conn.settimeout(30)
            buf = b""
            subscribed = False
            while not STOP.is_set():
                try:
                    chunk = conn.recv(4096)
                except socket.timeout:
                    continue
                except (ssl.SSLError, OSError):
                    break
                if not chunk:
                    break
                buf += chunk

                while True:
                    if len(buf) < 2:
                        break
                    try:
                        remaining, body_start = decode_varint(buf, 1)
                    except ValueError:
                        log("malformed packet, dropping connection")
                        conn.close()
                        return
                    if remaining is None or len(buf) < body_start + remaining:
                        break
                    ptype = buf[0] >> 4
                    flags = buf[0] & 0x0F
                    body = buf[body_start:body_start + remaining]
                    buf = buf[body_start + remaining:]
                    if not self._handle(conn, ptype, flags, body):
                        conn.close()
                        return
                    if ptype == SUBSCRIBE and not subscribed:
                        subscribed = True
                        # A real printer answers a fresh subscription with a
                        # full status push.
                        time.sleep(0.1)
                        self._publish(conn, self.report_topic, self.report_payload)
        except Exception as exc:  # pragma: no cover - diagnostics only
            log("mqtt session error: %r" % (exc,))
        finally:
            try:
                conn.close()
            except OSError:
                pass

    report_topic = "device/UNSET/report"

    def _publish(self, conn, topic, payload):
        body = struct.pack("!H", len(topic)) + topic.encode() + payload.encode()
        conn.sendall(bytes([PUBLISH << 4]) + encode_varint(len(body)) + body)

    def _handle(self, conn, ptype, flags, body):
        if ptype == CONNECT:
            offset = 0
            proto, offset = read_string(body, offset)
            level = body[offset]
            offset += 1
            cflags = body[offset]
            offset += 1
            (keepalive,) = struct.unpack_from("!H", body, offset)
            offset += 2
            client_id, offset = read_string(body, offset)
            username = password = ""
            if cflags & 0x80:
                username, offset = read_string(body, offset)
            if cflags & 0x40:
                password, offset = read_string(body, offset)
            self._record("mqtt_connect.jsonl", {
                "protocol": proto, "level": level, "flags": cflags,
                "keepalive": keepalive, "client_id": client_id,
                "username": username, "password": password,
            })
            if proto != "MQTT" or level != 4:
                conn.sendall(bytes([CONNACK << 4, 2, 0, 1]))
                return False
            if username != "bblp" or password != self.access_code:
                log("rejecting bad credentials %r/%r" % (username, password))
                conn.sendall(bytes([CONNACK << 4, 2, 0, 5]))
                return False
            conn.sendall(bytes([CONNACK << 4, 2, 0, 0]))
            return True

        if ptype == SUBSCRIBE:
            (packet_id,) = struct.unpack_from("!H", body, 0)
            topic, offset = read_string(body, 2)
            qos = body[offset]
            self._record("mqtt_subscribe.jsonl", {"topic": topic, "qos": qos})
            conn.sendall(bytes([SUBACK << 4, 3]) + struct.pack("!H", packet_id) + bytes([qos]))
            return True

        if ptype == PUBLISH:
            qos = (flags >> 1) & 0x03
            topic, offset = read_string(body, 0)
            packet_id = None
            if qos > 0:
                (packet_id,) = struct.unpack_from("!H", body, offset)
                offset += 2
            payload = body[offset:].decode("utf-8", "replace")
            self._record("mqtt_published.jsonl", {"topic": topic, "qos": qos, "payload": payload})
            if qos == 1:
                conn.sendall(bytes([PUBACK << 4, 2]) + struct.pack("!H", packet_id))
            return True

        if ptype == PINGREQ:
            self._record("mqtt_ping.jsonl", {"t": time.time()})
            conn.sendall(bytes([PINGRESP << 4, 0]))
            return True

        if ptype == DISCONNECT:
            self._record("mqtt_disconnect.jsonl", {"t": time.time()})
            return False

        log("ignoring MQTT packet type %d" % ptype)
        return True


# ---------------------------------------------------------------------------
# FTP over implicit TLS
# ---------------------------------------------------------------------------

class FtpServer(threading.Thread):
    def __init__(self, port, access_code, upload_dir, ctx):
        super().__init__(daemon=True)
        self.port = port
        self.access_code = access_code
        self.upload_dir = upload_dir
        self.ctx = ctx
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(8)
        self.sock.settimeout(0.5)

    def run(self):
        while not STOP.is_set():
            try:
                conn, addr = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()
        self.sock.close()

    def _serve(self, conn):
        data_listener = None
        authenticated = False
        user_ok = False
        cwd = "/"
        try:
            if self.ctx is not None:
                conn = self.ctx.wrap_socket(conn, server_side=True)
            conn.settimeout(30)
            conn.sendall(b"220 Bambu Lab FTP\r\n")
            buf = b""
            while not STOP.is_set():
                try:
                    chunk = conn.recv(1024)
                except socket.timeout:
                    continue
                except (ssl.SSLError, OSError):
                    break
                if not chunk:
                    break
                buf += chunk
                while b"\r\n" in buf:
                    line, buf = buf.split(b"\r\n", 1)
                    text = line.decode("utf-8", "replace").strip()
                    if not text:
                        continue
                    parts = text.split(" ", 1)
                    cmd = parts[0].upper()
                    arg = parts[1] if len(parts) > 1 else ""
                    log("ftp <- %s %s" % (cmd, "***" if cmd == "PASS" else arg))

                    if cmd == "USER":
                        user_ok = arg == "bblp"
                        conn.sendall(b"331 password required\r\n")
                    elif cmd == "PASS":
                        if user_ok and arg == self.access_code:
                            authenticated = True
                            conn.sendall(b"230 logged in\r\n")
                        else:
                            conn.sendall(b"530 not logged in\r\n")
                    elif not authenticated:
                        conn.sendall(b"530 not logged in\r\n")
                    elif cmd in ("PBSZ", "PROT", "TYPE", "MODE", "STRU", "NOOP", "OPTS"):
                        conn.sendall(b"200 ok\r\n")
                    elif cmd == "SYST":
                        conn.sendall(b"215 UNIX Type: L8\r\n")
                    elif cmd == "FEAT":
                        conn.sendall(b"211-features\r\n PASV\r\n211 end\r\n")
                    elif cmd == "PWD":
                        conn.sendall(('257 "%s"\r\n' % cwd).encode())
                    elif cmd == "CWD":
                        cwd = arg if arg.startswith("/") else cwd.rstrip("/") + "/" + arg
                        conn.sendall(b"250 ok\r\n")
                    elif cmd == "MKD":
                        conn.sendall(('257 "%s" created\r\n' % arg).encode())
                    elif cmd == "EPSV":
                        # The real firmware does not implement it either.
                        conn.sendall(b"500 not understood\r\n")
                    elif cmd == "PASV":
                        if data_listener is not None:
                            data_listener.close()
                        data_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                        data_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                        data_listener.bind(("127.0.0.1", 0))
                        data_listener.listen(1)
                        data_listener.settimeout(20)
                        port = data_listener.getsockname()[1]
                        conn.sendall(("227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n"
                                      % (port >> 8, port & 0xFF)).encode())
                    elif cmd in ("STOR", "APPE"):
                        if data_listener is None:
                            conn.sendall(b"425 use PASV first\r\n")
                            continue
                        conn.sendall(b"150 ready\r\n")
                        try:
                            data_conn, _ = data_listener.accept()
                            if self.ctx is not None:
                                data_conn = self.ctx.wrap_socket(data_conn, server_side=True)
                            target = os.path.join(self.upload_dir, os.path.basename(arg))
                            with open(target, "wb") as fh:
                                while True:
                                    block = data_conn.recv(65536)
                                    if not block:
                                        break
                                    fh.write(block)
                            data_conn.close()
                            log("stored %s" % target)
                            conn.sendall(b"226 transfer complete\r\n")
                        except Exception as exc:
                            log("stor failed: %r" % (exc,))
                            conn.sendall(b"426 transfer failed\r\n")
                        finally:
                            data_listener.close()
                            data_listener = None
                    elif cmd == "SIZE":
                        target = os.path.join(self.upload_dir, os.path.basename(arg))
                        if os.path.exists(target):
                            conn.sendall(("213 %d\r\n" % os.path.getsize(target)).encode())
                        else:
                            conn.sendall(b"550 not found\r\n")
                    elif cmd == "QUIT":
                        conn.sendall(b"221 bye\r\n")
                        return
                    else:
                        conn.sendall(b"502 not implemented\r\n")
        except Exception as exc:  # pragma: no cover - diagnostics only
            log("ftp session error: %r" % (exc,))
        finally:
            if data_listener is not None:
                data_listener.close()
            try:
                conn.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# SSDP
# ---------------------------------------------------------------------------

class SsdpAnnouncer(threading.Thread):
    def __init__(self, port, serial, ip):
        super().__init__(daemon=True)
        self.port = port
        self.serial = serial
        self.ip = ip

    def run(self):
        payload = "\r\n".join([
            "NOTIFY * HTTP/1.1",
            "HOST: 239.255.255.250:1990",
            "Server: UPnP/1.0",
            "Cache-Control: max-age=1800",
            "Location: %s" % self.ip,
            "NT: urn:bambulab-com:device:3dprinter:1",
            "NTS: ssdp:alive",
            "USN: %s" % self.serial,
            "DevModel.bambu.com: N2S",
            "DevName.bambu.com: Mock A1",
            "DevSignal.bambu.com: -44",
            "DevConnect.bambu.com: lan",
            "DevBind.bambu.com: free",
            "Devseclink.bambu.com: secure",
            "DevVersion.bambu.com: 00.00.00.01",
            "", "",
        ]).encode()

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while not STOP.is_set():
            try:
                sock.sendto(payload, ("127.0.0.1", self.port))
            except OSError as exc:
                log("ssdp send failed: %r" % (exc,))
            time.sleep(0.5)
        sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mqtt-port", type=int, default=8883)
    parser.add_argument("--ftp-port", type=int, default=990)
    parser.add_argument("--ssdp-port", type=int, default=2021)
    parser.add_argument("--access-code", default="12345678")
    parser.add_argument("--serial", default="00M09A351100999")
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--state-dir", required=True)
    parser.add_argument("--no-tls", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.state_dir, exist_ok=True)
    upload_dir = os.path.join(args.state_dir, "uploads")
    os.makedirs(upload_dir, exist_ok=True)

    ctx = None
    if not args.no_tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
        # The printer's stack is old; allow whatever the client offers.
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2

    report = json.dumps({
        "print": {
            "command": "push_status",
            "gcode_state": "IDLE",
            "nozzle_temper": 24.0,
            "bed_temper": 23.0,
            "sequence_id": "1",
        }
    })

    mqtt = MqttServer(args.mqtt_port, args.access_code, args.state_dir, ctx, report)
    MqttServer.report_topic = "device/%s/report" % args.serial
    ftp = FtpServer(args.ftp_port, args.access_code, upload_dir, ctx)
    ssdp = SsdpAnnouncer(args.ssdp_port, args.serial, "127.0.0.1")

    mqtt.start()
    ftp.start()
    ssdp.start()
    log("listening: mqtt=%d ftp=%d ssdp->%d" % (args.mqtt_port, args.ftp_port, args.ssdp_port))

    # Signal readiness to the test driver.
    with open(os.path.join(args.state_dir, "ready"), "w") as fh:
        fh.write("ok\n")

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        STOP.set()


if __name__ == "__main__":
    main()
