// IpcClient — the C++ end of the loopback link to the Java WLED backend.
// Connects (with auto-retry) to 127.0.0.1:<port>, parses newline-delimited JSON,
// and surfaces the backend's messages as Qt signals.
#pragma once
#include <QObject>
#include <QColor>
#include <QByteArray>
#include <QList>

class QTcpSocket;
class QTimer;

class IpcClient : public QObject {
    Q_OBJECT
public:
    explicit IpcClient(QObject* parent = nullptr);
    void start(quint16 port = 47900);
    void sendWledColor(const QColor& c, bool on = true);   // app -> backend -> WLED

signals:
    void hello(const QString& wledName, int leds, bool reachable);
    void frame(const QColor& avg, const QList<QColor>& cols);   // room live colour + N buckets
    void connectionChanged(bool connected);

private:
    void tryConnect();
    void onLine(const QByteArray& line);
    QTcpSocket* sock_;
    QTimer*     retry_;
    quint16     port_ = 47900;
    QByteArray  buf_;
};
