// WledBackend — the Java half of wled-pc-rgb.
// -----------------------------------------------------------------------------
// * WLED live source: subscribes to the live-view WebSocket ({"lv":true}) and
//   tracks the strip's average colour + 16 buckets. Self-heals (watchdog).
// * Optional low-latency sources: a DDP listener on UDP 4048 and an E1.31/sACN
//   listener on UDP 5568 — point LedFx at this PC (DDP device -> 4048, or E1.31
//   device -> 5568) and it's used instead of the WebSocket while active.
// * WLED state: polls /json/state so the app knows reachable / on / brightness.
// * Control: on an app command POSTs ONLY the segment colour (never bri/power).
// * Bridge: newline-delimited JSON over loopback TCP.
//
//   backend -> app :  {"type":"hello",...}
//                     {"type":"frame","avg":..,"cols":[..],"src":"live|ddp|sacn",
//                      "reachable":b,"on":b,"bri":n}
//   app -> backend :  {"type":"wled","color":"#rrggbb"}
//                     {"type":"host","host":"..."}   (reconnect to a new WLED)
//
//   java WledBackend.java [wledHost] [ipcPort]
// -----------------------------------------------------------------------------

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.MulticastSocket;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.WebSocket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class WledBackend {

    // The colour payload (avg + buckets + src) is published as ONE atomic string so a
    // client never stitches together fields from two different frames. reachable/on/bri
    // stay separate (they're updated by the state poller and must refresh via keepalive).
    static volatile String    framePayload  = "\"avg\":\"#000000\",\"cols\":[],\"src\":\"live\"";
    static volatile int       ledCount      = 0;
    static volatile String    wledName      = "WLED";
    static volatile boolean   wledReachable = false;
    static volatile boolean   wledOn        = true;
    static volatile int       wledBri       = 255;
    static volatile WebSocket liveWs        = null;
    static volatile long      lastFrameMs   = 0;
    static volatile long      lastDdpMs     = 0;
    static volatile long      lastSacnMs    = 0;
    // ACN packet identifier "ASC-E1.17\0\0\0" — bytes 4..15 of every E1.31 packet.
    static final byte[] ACN_ID = {0x41,0x53,0x43,0x2d,0x45,0x31,0x2e,0x31,0x37,0x00,0x00,0x00};
    static final Object frameLock = new Object();   // event-driven push: wake clients on each decoded frame
    static volatile long frameSeq = 0;
    static final int NB = 64;   // strip buckets sent per frame (finer = smoother spread/wrap)
    static volatile String wledHost = "wled.local";
    static HttpClient http;

    public static void main(String[] args) throws Exception {
        wledHost    = args.length > 0 ? args[0] : "wled.local";
        int ipcPort = args.length > 1 ? Integer.parseInt(args[1]) : 47900;
        http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

        fetchInfo();
        connectLiveView();
        startWatchdog();
        startDdpListener();
        startSacnListener();

        ServerSocket server = new ServerSocket();
        server.setReuseAddress(true);
        server.bind(new InetSocketAddress("127.0.0.1", ipcPort));
        System.out.println("[backend] listening 127.0.0.1:" + ipcPort + "  wled=" + wledHost);
        while (true) {
            try { Socket c = server.accept(); Thread t = new Thread(() -> handleClient(c)); t.setDaemon(true); t.start(); }
            catch (Exception e) { System.out.println("[backend] accept error: " + e.getMessage()); }
        }
    }

    static void fetchInfo() {
        try {
            HttpResponse<String> r = http.send(
                HttpRequest.newBuilder(URI.create("http://" + wledHost + "/json/info")).GET().build(),
                HttpResponse.BodyHandlers.ofString());
            wledName      = group(r.body(), "\"name\"\\s*:\\s*\"([^\"]*)\"", wledName);
            ledCount      = Integer.parseInt(group(r.body(), "\"count\"\\s*:\\s*(\\d+)", "0"));
            wledReachable = (r.statusCode() == 200);
        } catch (Exception e) { wledReachable = false; }
    }

    static void fetchState() {
        try {
            HttpResponse<String> r = http.send(
                HttpRequest.newBuilder(URI.create("http://" + wledHost + "/json/state")).GET().build(),
                HttpResponse.BodyHandlers.ofString());
            String b = r.body();
            wledReachable = (r.statusCode() == 200);
            wledOn  = "true".equals(group(b, "\"on\"\\s*:\\s*(true|false)", "true"));
            wledBri = Integer.parseInt(group(b, "\"bri\"\\s*:\\s*(\\d+)", "255"));
        } catch (Exception e) { wledReachable = false; }
    }

    static void connectLiveView() {
        try {
            WebSocket old = liveWs;
            if (old != null) { try { old.abort(); } catch (Exception ignore) {} }
            http.newWebSocketBuilder().connectTimeout(Duration.ofSeconds(5))
                .buildAsync(URI.create("ws://" + wledHost + "/ws"), new WebSocket.Listener() {
                    final ByteArrayOutputStream acc = new ByteArrayOutputStream();
                    @Override public void onOpen(WebSocket ws) { liveWs = ws; lastFrameMs = System.currentTimeMillis(); ws.sendText("{\"lv\":true}", true); ws.request(1); System.out.println("[backend] live-view connected"); }
                    @Override public CompletionStage<?> onBinary(WebSocket ws, ByteBuffer data, boolean last) {
                        byte[] chunk = new byte[data.remaining()]; data.get(chunk); acc.write(chunk, 0, chunk.length);
                        if (last) { decodeLive(acc.toByteArray()); acc.reset(); }
                        ws.request(1); return null;
                    }
                    @Override public CompletionStage<?> onText(WebSocket ws, CharSequence d, boolean last) { ws.request(1); return null; }
                    @Override public void onError(WebSocket ws, Throwable err) { System.out.println("[backend] ws error: " + err); }
                    @Override public CompletionStage<?> onClose(WebSocket ws, int code, String reason) { return null; }
                });
        } catch (Exception e) { System.out.println("[backend] live-view connect failed: " + e.getMessage()); }
    }

    static void startWatchdog() {
        Thread wd = new Thread(() -> {
            while (true) {
                try { Thread.sleep(3000); } catch (InterruptedException e) { return; }
                fetchState();
                long now = System.currentTimeMillis();
                if (now - lastFrameMs > 5000 && now - lastDdpMs > 5000 && now - lastSacnMs > 5000) {
                    System.out.println("[backend] no frames — reconnecting live-view");
                    fetchInfo(); connectLiveView();
                }
            }
        });
        wd.setDaemon(true); wd.start();
    }

    // Lower-latency source: LedFx (or anything) can stream DDP to this PC:4048.
    static void startDdpListener() {
        Thread t = new Thread(() -> {
            try (DatagramSocket sock = new DatagramSocket(4048)) {
                byte[] buf = new byte[1500];
                while (true) {
                    DatagramPacket pkt = new DatagramPacket(buf, buf.length);
                    sock.receive(pkt);
                    decodeDdp(buf, pkt.getLength());
                }
            } catch (Exception e) { System.out.println("[backend] DDP listener off: " + e.getMessage()); }
        });
        t.setDaemon(true); t.start();
    }

    // Lower-latency source: LedFx (or anything) can stream E1.31/sACN to this PC:5568.
    // Handles both unicast (LedFx E1.31 device pointed at this IP) and multicast
    // (best-effort join of universe 1, 239.255.0.1). RGB starts after the 126-byte header.
    static void startSacnListener() {
        Thread t = new Thread(() -> {
            try (MulticastSocket sock = new MulticastSocket(5568)) {
                try { sock.joinGroup(InetAddress.getByName("239.255.0.1")); } catch (Exception ignore) {}
                byte[] buf = new byte[1500];
                while (true) {
                    DatagramPacket pkt = new DatagramPacket(buf, buf.length);
                    sock.receive(pkt);
                    decodeSacn(buf, pkt.getLength());
                }
            } catch (Exception e) { System.out.println("[backend] sACN listener off: " + e.getMessage()); }
        });
        t.setDaemon(true); t.start();
    }

    // E1.31 data packet: 126-byte header (root+framing+DMP), then a DMX START code
    // (byte 125) and the DMX slots (RGB) from byte 126. Validate the ACN identifier
    // and a 0x00 (normal-DMX) start code so we ignore sync/other packets.
    static void decodeSacn(byte[] f, int len) {
        if (len < 126) return;
        for (int i = 0; i < 12; i++) if (f[4 + i] != ACN_ID[i]) return;
        if (f[125] != 0x00) return;
        lastSacnMs = System.currentTimeMillis();
        byte[] rgb = new byte[len - 126];
        System.arraycopy(f, 126, rgb, 0, rgb.length);
        computeFrom(rgb, 0, "sacn");
    }

    // WLED live-view binary frame: 'L', version, [w,h], then RGB per LED.
    static void decodeLive(byte[] f) {
        lastFrameMs = System.currentTimeMillis();
        long now = System.currentTimeMillis();
        if (now - lastDdpMs < 1500 || now - lastSacnMs < 1500) return;   // an external tap (DDP/sACN) is active → it wins
        if (f.length < 2 || (f[0] & 0xff) != 0x4C) return;
        int version = f[1] & 0xff, off = (version == 2) ? 4 : 2;
        computeFrom(f, off, "live");
    }

    // DDP packet: 10-byte header then RGB payload.
    static void decodeDdp(byte[] f, int len) {
        if (len <= 10) return;
        lastDdpMs = System.currentTimeMillis();
        byte[] rgb = new byte[len - 10];
        System.arraycopy(f, 10, rgb, 0, rgb.length);
        computeFrom(rgb, 0, "ddp");
    }

    static void computeFrom(byte[] f, int off, String src) {
        int n = Math.max(0, (f.length - off) / 3);
        String avg, cols;
        if (n == 0) {
            avg = "#000000"; cols = "[]";
        } else {
            long r = 0, g = 0, b = 0;
            for (int i = 0; i < n; i++) { r += f[off+i*3]&0xff; g += f[off+i*3+1]&0xff; b += f[off+i*3+2]&0xff; }
            avg = String.format("#%02x%02x%02x", (int)(r/n), (int)(g/n), (int)(b/n));
            StringBuilder sb = new StringBuilder("[");
            for (int bkt = 0; bkt < NB; bkt++) {
                int lo = bkt*n/NB, hi = (bkt+1)*n/NB; if (hi <= lo) hi = Math.min(lo+1, n);
                long rr=0,gg=0,bb=0; int cnt=0;
                for (int i = lo; i < hi; i++) { rr += f[off+i*3]&0xff; gg += f[off+i*3+1]&0xff; bb += f[off+i*3+2]&0xff; cnt++; }
                if (cnt == 0) cnt = 1;
                if (bkt > 0) sb.append(",");
                sb.append(String.format("\"#%02x%02x%02x\"", (int)(rr/cnt), (int)(gg/cnt), (int)(bb/cnt)));
            }
            cols = sb.append("]").toString();
        }
        // Build the whole colour payload locally, then publish it as one reference.
        final String payload = "\"avg\":\"" + avg + "\",\"cols\":" + cols + ",\"src\":\"" + src + "\"";
        synchronized (frameLock) { framePayload = payload; frameSeq++; frameLock.notifyAll(); }
    }

    static String frameJson() {
        return "{\"type\":\"frame\"," + framePayload
             + ",\"reachable\":" + wledReachable + ",\"on\":" + wledOn + ",\"bri\":" + wledBri + "}";
    }

    static void handleClient(Socket c) {
        try (Socket s = c;
             PrintWriter out = new PrintWriter(new OutputStreamWriter(s.getOutputStream(), StandardCharsets.UTF_8), true);
             BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8))) {
            try { s.setTcpNoDelay(true); } catch (Exception ignore) {}   // low latency: no Nagle batching
            out.println("{\"type\":\"hello\",\"wled\":\"" + esc(wledName) + "\",\"leds\":" + ledCount + ",\"reachable\":" + wledReachable + "}");
            Thread reader = new Thread(() -> { try { String line; while ((line = in.readLine()) != null) handleCommand(line); } catch (Exception ignore) {} });
            reader.setDaemon(true); reader.start();
            // Event-driven: push a frame the instant a new WLED frame is decoded, so the PC
            // tracks the source's real frame rate (not a fixed cap). The 500ms wait timeout is
            // a keepalive so on/off/brightness still refresh when the colour is static.
            long seen = -1;
            while (!s.isClosed()) {
                synchronized (frameLock) {
                    if (frameSeq == seen) { try { frameLock.wait(500); } catch (InterruptedException e) { return; } }
                    seen = frameSeq;
                }
                out.println(frameJson());
                if (out.checkError()) break;
            }
        } catch (Exception ignore) {}
    }

    static void handleCommand(String line) {
        if (line.contains("\"host\"")) {   // reconnect to a different WLED
            String h = group(line, "\"host\"\\s*:\\s*\"([^\"]+)\"", "");
            if (!h.isEmpty()) { wledHost = h; System.out.println("[backend] wled host -> " + h); fetchInfo(); connectLiveView(); }
            return;
        }
        if (!line.contains("\"wled\"")) return;
        String color = group(line, "\"color\"\\s*:\\s*\"?#?([0-9a-fA-F]{6})\"?", "");
        if (color.isEmpty()) return;
        int r = Integer.parseInt(color.substring(0,2),16), g = Integer.parseInt(color.substring(2,4),16), b = Integer.parseInt(color.substring(4,6),16);
        String body = "{\"seg\":[{\"col\":[[" + r + "," + g + "," + b + "]]}]}";
        try { http.send(HttpRequest.newBuilder(URI.create("http://" + wledHost + "/json/state"))
                    .header("Content-Type","application/json").POST(HttpRequest.BodyPublishers.ofString(body)).build(),
                    HttpResponse.BodyHandlers.ofString()); }
        catch (Exception e) { System.out.println("[backend] wled cmd failed: " + e.getMessage()); }
    }

    static String esc(String s) { return s.replace("\\", "\\\\").replace("\"", "\\\""); }
    static String group(String s, String re, String dflt) { Matcher m = Pattern.compile(re).matcher(s); return m.find() ? m.group(1) : dflt; }
}
