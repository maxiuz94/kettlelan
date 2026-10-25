#include "engine_ipc.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace kettle::gui::ipc {

namespace {

QString str(const std::string& s) { return QString::fromStdString(s); }
std::string std8(const QJsonValue& v) { return v.toString().toStdString(); }

QJsonObject messageToJson(const Message& m) {
    QJsonArray args;
    for (const auto& a : m.args) args.append(str(a));
    return {{"id", int(m.id)}, {"args", args}};
}

Message messageFromJson(const QJsonObject& o) {
    Message m;
    m.id = Msg(o.value("id").toInt());
    for (const auto& a : o.value("args").toArray()) m.args.push_back(std8(a));
    return m;
}

int statusToInt(EngineStatus s) { return int(s); }

QByteArray line(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; }

} // namespace

QByteArray encodeSnapshot(const Snapshot& snap) {
    QJsonArray networks;
    for (const auto& n : snap.networks) {
        QJsonArray peers;
        for (const auto& p : n.peers) {
            peers.append(QJsonObject{{"steamId", QString::number(p.steamId)}, {"name", str(p.name)},
                                     {"address", str(p.address.toString())}, {"self", p.self}, {"owner", p.owner},
                                     {"verified", p.verified}, {"conflict", p.conflict}, {"ping", p.pingMs},
                                     {"relayed", p.relayed}, {"friend", p.steamFriend}, {"steamName", str(p.steamName)}});
        }
        networks.append(QJsonObject{{"id", int(n.id)}, {"connected", n.state == steam::State::Connected},
                                    {"code", str(n.code)}, {"name", str(n.name)}, {"owner", n.owner}, {"peers", peers}});
    }
    return line({{"type", "snapshot"},
                 {"status", statusToInt(snap.status)},
                 {"error", messageToJson(snap.error)},
                 {"persona", str(snap.personaName)},
                 {"address", str(snap.selfAddress)},
                 {"blocked", QString::number(snap.blockedPackets)},
                 {"networks", networks}});
}

QByteArray encodeEvent(const Message& message) { return line({{"type", "event"}, {"message", messageToJson(message)}}); }

std::optional<Incoming> decodeIncoming(const QByteArray& data) {
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    const QString type = o.value("type").toString();
    Incoming in;
    if (type == "event") {
        in.kind = Incoming::Kind::Event;
        in.event = messageFromJson(o.value("message").toObject());
        return in;
    }
    if (type != "snapshot") return std::nullopt;
    in.kind = Incoming::Kind::Snapshot;
    Snapshot& s = in.snapshot;
    const int status = o.value("status").toInt(-1);
    if (status < int(EngineStatus::Offline) || status > int(EngineStatus::Ready)) return std::nullopt;
    s.status = EngineStatus(status);
    s.error = messageFromJson(o.value("error").toObject());
    s.personaName = std8(o.value("persona"));
    s.selfAddress = std8(o.value("address"));
    s.blockedPackets = o.value("blocked").toString().toULongLong();
    for (const auto& nv : o.value("networks").toArray()) {
        const QJsonObject no = nv.toObject();
        steam::NetworkInfo n;
        n.id = steam::NetworkHandle(no.value("id").toInt());
        n.state = no.value("connected").toBool() ? steam::State::Connected : steam::State::Connecting;
        n.code = std8(no.value("code"));
        n.name = std8(no.value("name"));
        n.owner = no.value("owner").toBool();
        for (const auto& pv : no.value("peers").toArray()) {
            const QJsonObject po = pv.toObject();
            steam::PeerInfo p;
            p.steamId = po.value("steamId").toString().toULongLong();
            p.name = std8(po.value("name"));
            if (const auto a = ip::Address4::parse(std8(po.value("address")))) p.address = *a;
            p.self = po.value("self").toBool();
            p.owner = po.value("owner").toBool();
            p.verified = po.value("verified").toBool();
            p.conflict = po.value("conflict").toBool();
            p.pingMs = po.value("ping").toInt(-1);
            p.relayed = po.value("relayed").toBool();
            p.steamFriend = po.value("friend").toBool();
            p.steamName = std8(po.value("steamName"));
            n.peers.push_back(std::move(p));
        }
        s.networks.push_back(std::move(n));
    }
    return in;
}

QByteArray encodeCommand(const QJsonObject& command) { return line(command); }

std::optional<QJsonObject> decodeCommand(const QByteArray& data) {
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject() || !doc.object().value("cmd").isString()) return std::nullopt;
    return doc.object();
}

} // namespace kettle::gui::ipc
