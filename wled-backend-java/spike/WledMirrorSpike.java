// WledMirrorSpike — Phase-0 spike for wled-pc-rgb
// -----------------------------------------------------------------------------
// Proves the load-bearing seam of the project's data-flow: an external program
// can read WLED's LIVE rendered output and will see whatever is currently on the
// strip -- a WLED effect, a color set in the app, OR an external takeover like
// LedFx making it audio-reactive. This is the "-> back to program" leg of the
// pipeline: WLED is the master, the PC mirrors it.
//
// Mechanism (verified against wled/WLED main, wled00/ws.cpp):
//   * open a WebSocket to ws://<ip>/ws
//   * send TEXT {"lv":true} to subscribe to the "live view" (Peek) feed
//   * receive BINARY frames ~25fps: byte0='L'(0x4C), byte1=version
//     (1=1D strip -> pixels at offset 2 ; 2=2D matrix -> w,h at [2],[3], pixels
//     at offset 4), then 3 bytes R,G,B per (downsampled) LED.
//
// IMPORTANT: In WLED, Config -> Sync Interfaces -> "Live data override" must be
// OFF (default) for LedFx/realtime output to appear here. If it is ON, WLED
// re-renders its own effects and this feed shows those instead.
//
// Pure JDK (11+). No build step, no dependencies:
//     java wled-backend-java/spike/WledMirrorSpike.java <wled-ip> [seconds]
// -----------------------------------------------------------------------------

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.WebSocket;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class WledMirrorSpike {

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.err.println("usage: java WledMirrorSpike.java <wled-ip-or-host> [seconds]");
            return;
        }
        final String host = args[0];
        final int seconds = args.length > 1 ? Integer.parseInt(args[1]) : 30;

        HttpClient http = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(5)).build();

        // 1) Control-plane sanity check (the "program -> wled" direction): read /json/info.
        try {
            HttpResponse<String> info = http.send(
                    HttpRequest.newBuilder(URI.create("http://" + host + "/json/info")).GET().build(),
                    HttpResponse.BodyHandlers.ofString());
            String b = info.body();
            System.out.println("WLED /json/info: HTTP " + info.statusCode());
            System.out.println("  name : " + group(b, "\"name\"\\s*:\\s*\"([^\"]*)\""));
            System.out.println("  ver  : " + group(b, "\"ver\"\\s*:\\s*\"([^\"]*)\""));
            System.out.println("  LEDs : " + group(b, "\"count\"\\s*:\\s*(\\d+)"));
            System.out.println("  live : " + group(b, "\"live\"\\s*:\\s*(true|false)")
                             + "  (lm=" + group(b, "\"lm\"\\s*:\\s*\"?([^,\"}]*)\"?") + ")");
        } catch (Exception e) {
            System.out.println("! could not reach http://" + host + "/json/info : " + e.getMessage());
        }

        // 2) Live-view tap (the "wled -> back to program" direction).
        System.out.println("\nsubscribing to live view at ws://" + host + "/ws  ({\"lv\":true}) for " + seconds + "s ...");
        System.out.println("(reminder: WLED 'Live data override' must be OFF to mirror LedFx here)\n");

        WebSocket ws = http.newWebSocketBuilder()
                .buildAsync(URI.create("ws://" + host + "/ws"), new LiveListener())
                .join();

        Thread.sleep(seconds * 1000L);
        try { ws.sendText("{\"lv\":false}", true).join(); } catch (Exception ignored) {}
        ws.sendClose(WebSocket.NORMAL_CLOSURE, "done").join();
        System.out.println("\nstopped after " + seconds + "s.");
    }

    static String group(String s, String regex) {
        Matcher m = Pattern.compile(regex).matcher(s);
        return m.find() ? m.group(1) : "?";
    }

    /** Decodes WLED live-view binary frames and prints a throttled, human-readable summary. */
    static final class LiveListener implements WebSocket.Listener {
        private final ByteArrayOutputStream acc = new ByteArrayOutputStream();
        private long lastPrint = 0;
        private long frames = 0;

        @Override public void onOpen(WebSocket ws) {
            System.out.println("ws open; requesting live view");
            ws.sendText("{\"lv\":true}", true);
            ws.request(1);
        }

        @Override public CompletionStage<?> onBinary(WebSocket ws, ByteBuffer data, boolean last) {
            byte[] chunk = new byte[data.remaining()];
            data.get(chunk);
            acc.write(chunk, 0, chunk.length);
            if (last) { decode(acc.toByteArray()); acc.reset(); }
            ws.request(1);
            return null;
        }

        @Override public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            ws.request(1); // WLED may push JSON state here; ignored for the spike.
            return null;
        }

        @Override public void onError(WebSocket ws, Throwable error) {
            System.out.println("! ws error: " + error);
        }

        @Override public CompletionStage<?> onClose(WebSocket ws, int status, String reason) {
            System.out.println("ws closed: " + status + " " + reason);
            return null;
        }

        private void decode(byte[] f) {
            frames++;
            if (f.length < 2 || (f[0] & 0xff) != 0x4C) return;   // 'L' magic
            int version = f[1] & 0xff;
            int offset  = (version == 2) ? 4 : 2;                // 2D matrix has w,h at [2],[3]
            int n = Math.max(0, (f.length - offset) / 3);
            long r = 0, g = 0, b = 0;
            for (int i = 0; i < n; i++) {
                r += f[offset + i * 3]     & 0xff;
                g += f[offset + i * 3 + 1] & 0xff;
                b += f[offset + i * 3 + 2] & 0xff;
            }
            long now = System.currentTimeMillis();
            if (now - lastPrint < 250) return;                   // throttle console to ~4/sec
            lastPrint = now;
            if (n == 0) { System.out.println("frame: 0 LEDs (strip off / brightness 0?)"); return; }
            int ar = (int) (r / n), ag = (int) (g / n), ab = (int) (b / n);
            System.out.printf("frame #%-5d v%d  %4d LEDs  avg #%02X%02X%02X  %s%n",
                    frames, version, n, ar, ag, ab, bar(ar, ag, ab));
        }

        private static String bar(int r, int g, int b) {
            int lvl = ((r + g + b) / 3) * 20 / 255;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 20; i++) sb.append(i < lvl ? '#' : '.');
            return sb.toString();
        }
    }
}
