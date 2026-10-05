#!/usr/bin/env python3
"""Independent AirPlay 2 sender used to smoke-test ShairportQt's receiver.

Start the receiver harness first:
    ShairportQtTest --gtest_also_run_disabled_tests --gtest_filter=*ServeForManualTesting*
then run:  python3 test/tools/ap2_client.py [port]   (requires: pip install cryptography)
"""
import hashlib, os, plistlib, socket, struct, sys, time
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes

HOST, PORT = "127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 5000

N = int("FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864D87602733EC86A64521F2B18177B200CBBE117577A615D6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF", 16)
G = 5
NLEN = 384
H = lambda *a: hashlib.sha512(b"".join(a)).digest()
i2b = lambda n, l=None: n.to_bytes(l or (n.bit_length() + 7) // 8, "big")
b2i = lambda b: int.from_bytes(b, "big")


def tlv_encode(items):
    out = b""
    for t, v in items:
        if not v:
            out += bytes([t, 0])
        for i in range(0, len(v), 255):
            c = v[i:i + 255]
            out += bytes([t, len(c)]) + c
    return out


def tlv_decode(data):
    res, i, last = {}, 0, None
    while i < len(data):
        t, l = data[i], data[i + 1]
        v = data[i + 2:i + 2 + l]
        res[t] = res[t] + v if t == last and t in res else v
        last, i = t, i + 2 + l
    return res


def hkdf(secret, salt, info):
    return HKDF(hashes.SHA512(), 32, salt.encode(), info.encode()).derive(secret)


class Rtsp:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT))
        self.cseq = 0
        self.wkey = self.rkey = None
        self.wctr = self.rctr = 0
        self.rbuf = b""

    def _send(self, data):
        if not self.wkey:
            return self.s.sendall(data)
        out = b""
        for i in range(0, len(data), 1024):
            c = data[i:i + 1024]
            aad = struct.pack("<H", len(c))
            nonce = b"\0" * 4 + struct.pack("<Q", self.wctr)
            self.wctr += 1
            out += aad + ChaCha20Poly1305(self.wkey).encrypt(nonce, c, aad)
        self.s.sendall(out)

    def _recv_plain(self, n):
        while len(self.rbuf) < n:
            if not self.rkey:
                d = self.s.recv(65536)
                if not d:
                    raise EOFError
                self.rbuf += d
            else:
                hdr = self._recv_raw(2)
                l = struct.unpack("<H", hdr)[0]
                body = self._recv_raw(l + 16)
                nonce = b"\0" * 4 + struct.pack("<Q", self.rctr)
                self.rctr += 1
                self.rbuf += ChaCha20Poly1305(self.rkey).decrypt(nonce, body, hdr)
        r, self.rbuf = self.rbuf[:n], self.rbuf[n:]
        return r

    def _recv_raw(self, n):
        b = b""
        while len(b) < n:
            d = self.s.recv(n - len(b))
            if not d:
                raise EOFError
            b += d
        return b

    def _readline(self):
        line = b""
        while not line.endswith(b"\r\n"):
            line += self._recv_plain(1)
        return line[:-2].decode()

    def request(self, method, path, body=b"", ctype=None, headers=None):
        self.cseq += 1
        h = f"{method} {path} RTSP/1.0\r\nCSeq: {self.cseq}\r\nUser-Agent: AirPlay/550.10\r\n"
        h += "DACP-ID: 1A2B3C4D5E6F7788\r\nActive-Remote: 12345\r\n"
        for k, v in (headers or {}).items():
            h += f"{k}: {v}\r\n"
        if ctype:
            h += f"Content-Type: {ctype}\r\n"
        h += f"Content-Length: {len(body)}\r\n\r\n"
        self._send(h.encode() + body)
        status = self._readline()
        hdrs = {}
        while True:
            l = self._readline()
            if not l:
                break
            k, v = l.split(":", 1)
            hdrs[k.strip().lower()] = v.strip()
        rb = self._recv_plain(int(hdrs.get("content-length", 0)))
        code = int(status.split()[1])
        print(f"  {method} {path} -> {code}")
        return code, hdrs, rb


def alac_uncompressed_frame(samples):
    # stereo CPE element, not compressed, 16 bit big-endian, 352 frames
    bits = "001" + "0000" + "0" * 12 + "0" + "00" + "1"
    for l, r in samples:
        bits += format(l & 0xffff, "016b") + format(r & 0xffff, "016b")
    bits += "111"
    bits += "0" * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits) // 8, "big")


def main():
    c = Rtsp()

    print("GET /info")
    code, h, body = c.request("GET", "/info")
    assert code == 200, code
    info = plistlib.loads(body)
    print("  name:", info.get("name"), "features:", hex(info.get("features", 0)), "deviceID:", info.get("deviceID"))
    assert info["features"] & (1 << 38) and info["features"] & (1 << 40), "buffered audio / PTP bits missing"

    print("pair-setup (transient)")
    code, _, body = c.request("POST", "/pair-setup", tlv_encode([(6, b"\x01"), (0, b"\x00"), (19, b"\x10")]), "application/octet-stream")
    m2 = tlv_decode(body)
    salt, B = m2[2], m2[3]
    a = b2i(os.urandom(32))
    A = i2b(pow(G, a, N))
    k = b2i(H(i2b(N), i2b(G, NLEN)))
    u = b2i(H(i2b(b2i(A), NLEN), i2b(b2i(B), NLEN)))
    x = b2i(H(salt, H(b"Pair-Setup:3939")))
    S = pow((b2i(B) - k * pow(G, x, N)) % N, a + u * x, N)
    K = H(i2b(S))
    hn, hg = H(i2b(N)), H(i2b(G))
    M1 = H(bytes(p ^ q for p, q in zip(hn, hg)), H(b"Pair-Setup"), salt, A, B, K)
    code, _, body = c.request("POST", "/pair-setup", tlv_encode([(6, b"\x03"), (3, A), (4, M1)]), "application/octet-stream")
    m4 = tlv_decode(body)
    assert 7 not in m4, f"pair-setup error {m4.get(7)}"
    assert m4[4] == H(A, M1, K), "server proof mismatch"
    print("  server proof OK, enabling encryption")
    c.wkey = hkdf(K, "Control-Salt", "Control-Write-Encryption-Key")
    c.rkey = hkdf(K, "Control-Salt", "Control-Read-Encryption-Key")

    print("SETUP phase 1 (encrypted)")
    setup1 = plistlib.dumps({"timingProtocol": "PTP", "groupUUID": "A-B", "isMultiSelectAirPlay": True,
                             "deviceID": "AA:BB:CC:DD:EE:FF", "sessionUUID": "S-1", "timingPeerInfo": {"Addresses": ["127.0.0.1"], "ID": "x"}},
                            fmt=plistlib.FMT_BINARY)
    code, _, body = c.request("SETUP", "rtsp://127.0.0.1/1", setup1, "application/x-apple-binary-plist")
    assert code == 200, code
    r1 = plistlib.loads(body)
    print("  ", r1)
    ev = socket.create_connection((HOST, r1["eventPort"]))

    shk = os.urandom(32)
    print("SETUP phase 2 (buffered ALAC stream)")
    setup2 = plistlib.dumps({"streams": [{"type": 103, "ct": 2, "audioFormat": 0x40000, "spf": 352, "sr": 44100,
                                          "shk": shk, "audioMode": "default", "controlPort": 0,
                                          "clientID": "x", "streamConnectionID": 1, "supportsDynamicStreamID": True}]},
                            fmt=plistlib.FMT_BINARY)
    code, _, body = c.request("SETUP", "rtsp://127.0.0.1/1", setup2, "application/x-apple-binary-plist")
    assert code == 200, code
    r2 = plistlib.loads(body)
    print("  ", r2)
    data_port = r2["streams"][0]["dataPort"]

    code, _, _ = c.request("RECORD", "rtsp://127.0.0.1/1")
    assert code == 200
    anchor = plistlib.dumps({"rate": 1, "rtpTime": 0, "networkTimeSecs": 0, "networkTimeFrac": 0, "networkTimeTimelineID": 0},
                            fmt=plistlib.FMT_BINARY)
    code, _, _ = c.request("SETRATEANCHORTIME", "rtsp://127.0.0.1/1", anchor, "application/x-apple-binary-plist")
    assert code == 200

    print("streaming ~2s of 440 Hz tone over the buffered data channel")
    import math
    d = socket.create_connection((HOST, data_port))
    aead = ChaCha20Poly1305(shk)
    ts = 0
    for seq in range(250):
        samples = []
        for i in range(352):
            v = int(8000 * math.sin(2 * math.pi * 440 * (ts + i) / 44100))
            samples.append((v, v))
        payload = alac_uncompressed_frame(samples)
        header = struct.pack(">III", 0x80000000 | seq, ts, 0x0000FACE)
        nonce8 = struct.pack("<Q", seq)
        ct = aead.encrypt(b"\0" * 4 + nonce8, payload, header[4:12])
        pkt = header + ct + nonce8
        d.sendall(struct.pack(">H", len(pkt) + 2) + pkt)
        ts += 352
    time.sleep(2)

    code, _, _ = c.request("FLUSHBUFFERED", "rtsp://127.0.0.1/1",
                           plistlib.dumps({"flushUntilSeq": 250, "flushUntilTS": ts}, fmt=plistlib.FMT_BINARY),
                           "application/x-apple-binary-plist")
    code, _, _ = c.request("TEARDOWN", "rtsp://127.0.0.1/1",
                           plistlib.dumps({"streams": [{"type": 103}]}, fmt=plistlib.FMT_BINARY), "application/x-apple-binary-plist")
    assert code == 200
    code, _, _ = c.request("TEARDOWN", "rtsp://127.0.0.1/1")
    assert code == 200
    d.close(); ev.close(); c.s.close()
    print("E2E OK")


main()
