#include "ipc_client.h"
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractSocket>

IpcClient::IpcClient(QObject* parent)
    : QObject(parent), sock_(new QTcpSocket(this)), retry_(new QTimer(this)) {
    retry_->setSingleShot(true);

    connect(sock_, &QTcpSocket::readyRead, this, [this] {
        buf_ += sock_->readAll();
        int nl;
        while ((nl = buf_.indexOf('\n')) >= 0) {
            const QByteArray line = buf_.left(nl);
            buf_.remove(0, nl + 1);
            onLine(line);
        }
    });
    connect(sock_, &QTcpSocket::connected,    this, [this] { emit connectionChanged(true); });
    connect(sock_, &QTcpSocket::disconnected, this, [this] { emit connectionChanged(false); retry_->start(1000); });
    connect(sock_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { retry_->start(1000); });
    connect(retry_, &QTimer::timeout, this, [this] { tryConnect(); });
}

void IpcClient::start(quint16 port) { port_ = port; tryConnect(); }

void IpcClient::tryConnect() {
    if (sock_->state() == QAbstractSocket::ConnectedState ||
        sock_->state() == QAbstractSocket::ConnectingState) return;
    sock_->abort();
    sock_->connectToHost("127.0.0.1", port_);
}

void IpcClient::onLine(const QByteArray& line) {
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(line, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto o = doc.object();
    const QString type = o.value("type").toString();
    if (type == "hello")
        emit hello(o.value("wled").toString(), o.value("leds").toInt(), o.value("reachable").toBool());
    else if (type == "frame")
        emit frame(QColor(o.value("avg").toString()));
}

void IpcClient::sendWledColor(const QColor& c, bool on) {
    if (sock_->state() != QAbstractSocket::ConnectedState) return;
    const QString msg = QString("{\"type\":\"wled\",\"on\":%1,\"color\":\"%2\"}\n")
                            .arg(on ? "true" : "false", c.name());
    sock_->write(msg.toUtf8());
    sock_->flush();
}
