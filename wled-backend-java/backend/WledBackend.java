// WledBackend — the Java half of wled-pc-rgb (Phase 3).
// -----------------------------------------------------------------------------
// Responsibilities:
//   * talk to WLED: read /json/info, subscribe to the live-view WebSocket
//     ({"lv":true}) and track the strip's current average colour;
//   * control WLED: accept commands from the C++ app and POST /json/state;
//   * bridge to C++ over a loopback TCP socket using newline-delimited JSON.
//
// Protocol (one JSON object per line):
//   backend -> app :  {"type":"hello","wled":"<name>","leds":N,"reachable":true|false}
//                     {"type":"frame","avg":"#rrggbb"}            (~10/s, the mirror feed)
//   app -> backend :  {"type":"wled","on":true|false,"color":"#rrggbb","bri":0..255}
//
// Pure JDK (11+), single-file launch:
//   java wled-backend-java/backend/WledBackend.java [wledHost] [ipcPort]
// -----------------------------------------------------------------------------

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.WebSocket;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class WledBackend {

    static volatile String  avgColor      = "#000000";
    static volatile String  bucketsJson   = "[]";
    static final int NB = 16;                 // downsampled colour buckets across the strip
    static volatile int     ledCount      = 0;
    static volatile String  wledName      = "WLED";
    static volatile boolean wledReachable = false;
    static String  wledHost = "wled.local";
    static HttpClient http;

    public static void main(String[] args) throws Exception {
        wledHost   = args.length > 0 ? args[0] : "wled.local";
        int ipcPort = args.length > 1 ? Integer.parseInt(args[1]) : 47900;
        http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

        fetchInfo();
        startLiveView();

        ServerSocket server = new ServerSocket();
        server.setReuseAddress(true);
        server.bind(new InetSocketAddress("127.0.0.1", ipcPort));
        System.out.println("[backend] listening 127.0.0.1:" + ipcPort + "  wled=" + wledHost
                + " (" + wledName + ", " + ledCount + " leds, reachable=" + wledReachable + ")");

        while (true) {
            Socket c = server.accept();
            System.out.println("[backend] app connected");
            new Thread(() -> handleClient(c)).start();
        }
    }

    static void fetchInfo() {
        try {
            HttpResponse<String> r = http.send(
                HttpRequest.newBuilder(URI.create("http://" + wledHost + "/json/info")).GET().build(),
                HttpResponse.BodyHandlers.ofString());
            String b = r.body();
            wledName  = group(b, "\"name\"\\s*:\\s*\"([^\"]*)\"", wledName);
            ledCount  = Integer.parseInt(group(b, "\"count\"\\s*:\\s*(\\d+)", "0"));
            wledReachable = (r.statusCode() == 200);
        } catch (Exception e) {
            wledReachable = false;
            System.out.println("[backend] WLED not reachable: " + e.getMessage());
        }
    }

    // Subscribe to WLED's live view and keep `avgColor` current.
    static void startLiveView() {
        try {
            http.newWebSocketBuilder()
                .buildAsync(URI.create("ws://" + wledHost + "/ws"), new WebSocket.Listener() {
                    final ByteArrayOutputStream acc = new ByteArrayOutputStream();
                    @Override public void onOpen(WebSocket ws) { ws.sendText("{\"lv\":true}", true); ws.request(1); }
                    @Override public CompletionStage<?> onBinary(WebSocket ws, ByteBuffer data, boolean last) {
                        byte[] chunk = new byte[data.remaining()]; data.get(chunk); acc.write(chunk, 0, chunk.length);
                        if (last) { decode(acc.toByteArray()); acc.reset(); }
                        ws.request(1); return null;
                    }
                    @Override public CompletionStage<?> onText(WebSocket ws, CharSequence d, boolean last) { ws.request(1); return null; }
                    @Override public void onError(WebSocket ws, Throwable err) { System.out.println("[backend] ws error: " + err); }
                });
        } catch (Exception e) {
            System.out.println("[backend] live-view failed: " + e.getMessage());
        }
    }

    static void decode(byte[] f) {
        if (f.length < 2 || (f[0] & 0xff) != 0x4C) return;      // 'L'
        int version = f[1] & 0xff, off = (version == 2) ? 4 : 2;
        int n = Math.max(0, (f.length - off) / 3);
        if (n == 0) { avgColor = "#000000"; return; }
        long r = 0, g = 0, b = 0;
        for (int i = 0; i < n; i++) { r += f[off+i*3]&0xff; g += f[off+i*3+1]&0xff; b += f[off+i*3+2]&0xff; }
        avgColor = String.format("#%02x%02x%02x", (int)(r/n), (int)(g/n), (int)(b/n));

        StringBuilder sb = new StringBuilder("[");
        for (int bkt = 0; bkt < NB; bkt++) {
            int lo = bkt * n / NB, hi = (bkt + 1) * n / NB;
            if (hi <= lo) hi = Math.min(lo + 1, n);
            long rr = 0, gg = 0, bb = 0; int cnt = 0;
            for (int i = lo; i < hi; i++) { rr += f[off+i*3]&0xff; gg += f[off+i*3+1]&0xff; bb += f[off+i*3+2]&0xff; cnt++; }
            if (cnt == 0) cnt = 1;
            if (bkt > 0) sb.append(",");
            sb.append(String.format("\"#%02x%02x%02x\"", (int)(rr/cnt), (int)(gg/cnt), (int)(bb/cnt)));
        }
        bucketsJson = sb.append("]").toString();
    }

    static void handleClient(Socket c) {
        try (Socket s = c;
             PrintWriter out = new PrintWriter(new OutputStreamWriter(s.getOutputStream(), StandardCharsets.UTF_8), true);
             BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8))) {

            out.println("{\"type\":\"hello\",\"wled\":\"" + esc(wledName) + "\",\"leds\":" + ledCount
                    + ",\"reachable\":" + wledReachable + "}");

            // Reader thread: handle app -> backend commands.
            Thread reader = new Thread(() -> {
                try { String line; while ((line = in.readLine()) != null) handleCommand(line); } catch (Exception ignore) {}
            });
            reader.setDaemon(true);
            reader.start();

            // Writer loop: stream the mirror colour ~10/s.
            String last = null;
            while (!s.isClosed()) {
                String a = avgColor;
                out.println("{\"type\":\"frame\",\"avg\":\"" + a + "\",\"cols\":" + bucketsJson + "}");
                if (out.checkError()) break;
                Thread.sleep(100);
            }
        } catch (Exception ignore) {
        }
        System.out.println("[backend] app disconnected");
    }

    // app -> backend: control WLED via its JSON API.
    static void handleCommand(String line) {
        if (!line.contains("\"wled\"")) return;
        String on    = group(line, "\"on\"\\s*:\\s*(true|false)", "");
        String color = group(line, "\"color\"\\s*:\\s*\"?#?([0-9a-fA-F]{6})\"?", "");
        String bri   = group(line, "\"bri\"\\s*:\\s*(\\d+)", "");
        StringBuilder body = new StringBuilder("{");
        boolean first = true;
        if (!on.isEmpty())    { body.append("\"on\":").append(on); first = false; }
        if (!bri.isEmpty())   { if (!first) body.append(","); body.append("\"bri\":").append(bri); first = false; }
        if (!color.isEmpty()) {
            int r = Integer.parseInt(color.substring(0,2),16), g = Integer.parseInt(color.substring(2,4),16), b = Integer.parseInt(color.substring(4,6),16);
            if (!first) body.append(",");
            body.append("\"seg\":[{\"col\":[[").append(r).append(",").append(g).append(",").append(b).append("]]}]");
        }
        body.append("}");
        try {
            http.send(HttpRequest.newBuilder(URI.create("http://" + wledHost + "/json/state"))
                          .header("Content-Type", "application/json")
                          .POST(HttpRequest.BodyPublishers.ofString(body.toString())).build(),
                      HttpResponse.BodyHandlers.ofString());
            System.out.println("[backend] wled <- " + body);
        } catch (Exception e) { System.out.println("[backend] wled command failed: " + e.getMessage()); }
    }

    static String esc(String s) { return s.replace("\\", "\\\\").replace("\"", "\\\""); }
    static String group(String s, String re, String dflt) { Matcher m = Pattern.compile(re).matcher(s); return m.find() ? m.group(1) : dflt; }
}
